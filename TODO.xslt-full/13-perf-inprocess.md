# 13 — Performance: in-process parity + superiority (#682)

#682: dispatch ~2.6x behind in-process libxslt (the v1.9.31/32
comparison used xsltproc WALL time — in-process libx2 xsltApplyStylesheet
is the honest reference). Work:
- twin bench: bench_xslt_transform_libxml2 (in-process libxslt via
  libxsltApplyStylesheet; the libxslt checkout + brew lib are
  available) for s1/s2/s3 + XSLTMark corpus
- profile s2 (13.5ms) against libxslt (~5ms): per-invoke template
  overhead (frames, current_* save/restore), result-tree element
  creation (out_append_elem path), attribute copies
- levers: frame-free invoke fast path when no params/ns; pooled
  result nodes reuse across iterations; sort index reuse
- gates: fresh Release dirs, min-of-8 interleaved (benchmark-
  discipline memory); PerfRegression budget updates; the harness
  becomes the release scorecard (site G2/G3)
Gate: s1/s2/s3 >= libxslt in-process on this machine, documented.

## Status 2026-09-03 (v1.9.71-74) — three quadratics killed, gap
## 12.83 → 10.56 ms vs lxml 4.69 ms (2.25x)

Shipped:
- v1.9.71 AVT compile cache (XsltExec.avt_cache — one compile per
  transform, not per evaluation) + direct-mapped 8-slot mutation
  tail cache (doc->mut_tail[(addr>>4)&7]; the single slot thrashed
  on interleaved result-tree parents, ~2M sibling hops/transform).
- v1.9.72 fragment tail caches (XsltExec.root_sib_tail/frag_tail —
  fragment-level appends walked the chain per node; 50k top-level
  value-of texts 1424ms → 10.6ms = 135x) + value-of select="."
  parse-time fast path (select_is_dot → get_node_text).
- v1.9.74 subtree duplicate: copy_subtree_detached threads the
  dest pool (C 0.111→0.048ms; Ruby dup 2.27x Nokogiri).
- Measured-and-DISCARDED: out_place_elem internal append (doc
  resolution already O(1) via the name backpointer).

Remaining (profile is DIFFUSE — no single >10% lever):
- allocator churn ~7% (per-eval nodeset/result wrappers; a scratch
  pool risks the clean document-scoped ownership model)
- interpreter self: xslt_exec_instrs 7.4%, invoke_template 4.5%,
  op_result_elem 4.5% — frame-free invoke fast path when a
  template has no params/sorts is the next measurable lever
- template dispatch: name-keyed candidate index (libxslt-style)
  for stylesheets with many templates
- harness: promote /tmp/dispatch_bench.c + vo_bench.c into
  benchmarks/ as the release scorecard
Open measurement protocol: best-of-batches, never mean.



## Status 2026-09-04 (v1.9.79 candidate)

- One-pass template selection shipped (PR #833): both selectors
  matched every pattern alternative TWICE (any-match test + the
  per-alternative priority loop); now one walk. Honest bench note:
  inside the noise band on the 4-template scorecard (9.25-9.51ms
  best-of-9 either way) — single-alternative ladders fail fast on
  name mismatch; the win scales with alternative/template counts.
- invoke_template is ALREADY stack-frame-free (stack arrays, var
  pointer save/restore); bind_param_defaults breaks on the first
  non-param instruction — the "frame-free invoke" lever from the
  earlier profile is effectively free already.

Remaining, in expected-value order:
1. allocator churn ~7%: per-eval nodeset/result wrappers; a scratch
   pool risks the document-scoped ownership model — needs a design
   conversation before touching the memory model.
2. op_call_template named-call scan is linear in template count —
   a name-keyed index matters for template-heavy stylesheets, not
   for the current scorecard (4 templates).
3. profile again with a template-heavy fixture before claiming the
   next lever (the 2000-book profile is diffuse by design).

## Status 2026-09-05 — #682 phase A: template-heavy fixture shipped

bench_xslt_dispatch_heavy (120 templates: 30 names x 3 modes +
30-link named chain, 2400 elements) + heavy_lxml.py reference.

Baseline (this machine, best of 9):
- leptris 10.21 ms vs in-process lxml 3.12 ms = 3.27x
  (the diffuse 2000-book profile reads 2.25x) — template-count-
  scaled overhead confirmed. Next: sample this bench (not the
  diffuse one) — expected suspects: op_call_template named-call
  linear scan (60 templates), pattern alternative scan per
  element (90 alternatives), mode-switch machinery.
