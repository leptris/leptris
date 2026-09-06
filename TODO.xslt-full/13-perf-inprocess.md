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

## Status 2026-09-05 (later) — #682 phase B: dispatch indexes shipped

Three levers, exact-semantics-preserving:
- named-template hash (open-addressed, inserts in sheet order so a
  hit IS the last declaration = the §11.6 winner the linear scan
  produced);
- mode buckets (per-mode candidate arrays in sheet order);
- bare-Name dispatch fast path (lone expr_name_only alternative =
  child::NAME: name + no-ns check, no doc climb, no ladder).

Numbers (same machine): heavy 10.21 -> 5.56 ms (1.83x; vs lxml
3.12 = 1.78x behind, from 3.27x); light dispatch 10.5 -> 8.97 ms;
transform/valueof unchanged. Full ctest 1285/1285.

Post-fix profile (sample): tlv_get_addr 335 (TLS thunks - the
free-lists), select_template self 167 + strcmp 162 (30-candidate
scans - name-bucketing blocked by priority interplay), attr churn
set_attribute 148 + probe 95 + rehash 92 (result-tree attrs),
get_document 99 + get_pool 51 (per-result-element doc climbs),
malloc/free ~244. NEXT SLICES in expected-value order: (1) result
attr-index churn, (2) TLS/allocator consolidation - needs the
design conversation (document-scoped ownership, TODO 13 item 1).

## Measured-and-discarded 2026-09-05: attr-index lazy registration

Below-4-attrs elements skipping doc-level attr-index registration
(walk instead of sentinel+entry): heavy bench 5.56 -> 5.60ms =
noise. The attr cluster in the profile is mut_str_carve + sv
copies, not index maintenance. Reverted; do not re-try. The
residual 1.78x gap is the diffuse remainder: TLS thunks (the
nodeset/result free-lists), per-result-element doc/pool climbs,
serializer, allocator - i.e. the consolidation item below, which
needs the design conversation (document-scoped ownership).

## 2x bar status (user directive 2026-09-06) + measured scorecard

Bar: every XSLT bench >= 2x libxslt AND Saxon. References on
byte-identical fixtures (best of 9): lxml in-process t=1.35ms
l=2.72ms h=2.88ms p=32.1ms; xsltproc wall 7.3-9.9ms; Saxon-HE 12.5
CLI wall ~500-600ms (JVM startup dominates; amortized-only
comparisons need a persistent-JVM harness).

Current (v1.9.93): transform 0.27ms = 5.0x AHEAD; pred 2.06ms =
15.6x ahead; light 3.93ms = 0.69x BEHIND; heavy 5.13ms = 0.56x
behind. Need ~3x on light+heavy.

Light-bench profile today: node_get_next_sibling 143 (append tail
walks on alternating fresh parents - the doc 64-slot cache still
misses ~1/64 of ~6000 appends into a 1000-long chain), tlv_get_addr
56 (TLS), malloc/free ~43. MEASURED-AND-REVERTED same day: exec-
local two-entry append hint - neutral perf BUT orphaned nodes on
include/attribute-heavy sheets (unrouted comment/PI/attr appends
invalidate the hint; bug-107/129/195/196 in the 205-suite caught
it; the 197-test unit suite did NOT). Any retry must route EVERY
result append through the hint or invalidate on non-routed
appends. Next levers: TLS consolidation (context-carried
free-lists, authorized), serializer, per-result-element
doc/pool climbs.

## 2x arc instrumentation (2026-09-06, session tail)

Measured with walk-counters + fresh profile: the light bench's
next_sibling samples are NOT chain walks - 100k "walks" per 10
transforms are all O(1) EMPTY-chain lookups (3 fresh-parent appends
per book). The cost is CALL OVERHEAD + tlv_get_addr 437/3756 (11.6%)
+ malloc/free ~400 (10.6%). A doc-level repeated-parent hot entry
(16B/doc, suite-green) measured NEUTRAL and was reverted to keep
main pure - the thrash theory is DEAD; do not retry cache-shaped
fixes. The remaining light/heavy gap = the consolidation arc and
ONLY that: (1) per-call overhead in append/get_next_sibling (inline
the empty-chain fast path: if !first_child return NULL - one branch
in the CALLER), (2) TLS consolidation (context-carried free-lists),
(3) malloc churn (result-tree text nodes). Next session starts with
lever (1) - it is a 2-line change per call site with no invariants.

## Lever-1 measured-and-discarded (same day): inline empty-chain fast path

Patched both append miss paths to skip leptris_elem_last_child via a
first_child_off check: suite green, light 3.99-4.11ms vs 3.89
baseline = NEUTRAL. Both micro-levers (hot entry, inline fast path)
are dead; the sampler's next_sibling weight is leaf-attribution
noise. The light/heavy gap is TLS (11.6%) + malloc/free (10.6%) +
diffuse eval serialization - the consolidation arc ONLY. Do not
attempt further call-site micro-fixes; start at TLS.
