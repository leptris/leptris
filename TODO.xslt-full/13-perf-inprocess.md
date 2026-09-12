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

## Consolidation arc round 2 (2026-09-06, v1.9.94→95 session)

Frame-pointer profile build (buildprof, -fno-omit-frame-pointer +
RelWithDebInfo) got real stacks where the default build collapsed
to main. Heavy-bench self-time ladder BEFORE this round:
set_attribute 229 + attr_index_rehash 73 (largest block),
serialize_element_internal 67, node_parent 55, select_template 45.

LANDED (PR #887): (1) TLS consolidation - the 4 xpath free-list
__thread vars collapse into one g_xpath_tls struct; light 3.83→3.57,
heavy 4.96→4.45 best. (2) Walk-first attr dup check - elements with
<=8 attrs walk the hash-prefiltered list instead of doc-index
register+probe+put (XSLT result elements carry 1-3 attrs; the index
was pure overhead). Note: the earlier "lazy registration" discard
predates the #866/#873 dispatch indexes - its neutral verdict was
stale; walk-first measured 4.45→3.83 heavy. Elements >8 attrs still
lazily bulk-register (2000-attr O(N^2) specs green).

LANDED (PR #889): ns-fixup walk skip - result_ns_in_scope + the
bug-130 default-ns climb gated on result-doc has_namespaces (the
exact any-declaration gate). Heavy 3.83→3.57.

MEASURED-AND-DISCARDED this round (do not re-try):
- Two-slot (root, doc) memo ring in get_document: heavy 3.57→3.98
  (8% REGRESSION) despite the source/result alternation theory.
  Single-slot memo stays.
- Contiguous text-create swap in leptris_text_node_create: C-side
  already 15-18ns/op in BOTH shapes (borrowed+strdup vs contiguous).
  ruby#149's 884ns is ~98% ffi-gem seam. Binding-side fix required
  (batch entry or C-ext hot path); findings posted on leptris-ruby
  #147/#149.

Scorecard after v1.9.95 + #889 (best of 9): light 3.57 (lxml 2.72
= 0.76x), heavy 3.57 (lxml 2.88 = 0.81x), transform 0.27 (5.0x),
pred 2.06 (15.6x). Heavy profile now diffuse: serialize 111,
select_template 87, op_result_elem 87, get_next_sibling 59,
get_document 56, exec_instrs 51, element_create 49, eval_avt 47.
Next slices: serializer result path, result-element creation
cluster, eval_avt. All call-site micro-levers remain dead (banked
below).

## Round 3 status + the honest 2x-bar math (2026-09-07)

ruby#150 answered engine-side (direct attr read = 23-28ns/op,
borrowed pointer, zero C allocs; 568ns is the ffi-gem seam; options
posted on the binding issue). Heavy profile on current main
(v1.9.97): serialize 122, set_attribute 109 (sampler attribution
swings between builds; the index is GONE - walk-first holds),
op_result_elem 91, select_template 82, node_parent 71,
get_next_sibling 59, get_document 59, element_create 57,
apply_templates 55, eval_avt 53, exec_instrs 46, ast_cache 40+24,
append 38. NOTHING above 5%. version bump = plain field++, not a
lever.

2x-BAR CEILING ANALYSIS (the design conversation the bank keeps
deferring): light 3.57 / heavy 3.57 vs lxml 2.72 / 2.88 = 0.76x /
0.81x. The bar (>=2x = light <=1.36ms, heavy <=1.44ms) needs a 60%
runtime cut. Remaining coherent levers and their quantified
ceilings: (a) result-tree STREAMING for apply_string (skip DOM
materialization when the sheet holds no RTFs/keys-on-result -
serializes during construction): replaces ~18% of samples (create
+ set_attribute + append + serialize cluster) minus the emit cost
it adds ~= 10-15% net. (b) eval-side (select_template/apply/instrs/
avt/ast_cache) rewrites ~= 10% at heroic effort. (c) remaining
micro levers ~= 5-8%. TOTAL PLAUSIBLE ~= 25-30% => ~2.6ms ~= 1.1x
lxml. THE 2x BAR ON THE DISPATCH FIXTURES IS NOT REACHABLE by
optimization alone while libxslt does the identical work at 2.88ms
- we are 24% behind a 20-year-tuned C engine on ITS core shape.
Where we ARE >=2x: transform 5.0x, pred 15.6x (the shapes users
actually transform with). DECISION FOR THE USER: (1) pursue
streaming anyway (buys ~15%, lands us ~1.1-1.2x - still "behind"
by the letter of the bar), or (2) re-scope the bar for dispatch
shapes to ">=1x in-process libxslt + >=2x xsltproc wall" (already
true: xsltproc wall 7.3-9.9ms vs our 3.57), or (3) accept the
fixture verdict and close #682. Do NOT silently grind micro-levers
expecting the bar to appear.

LATENT HAZARD found + hardened (same session): leptris_elem_
split_qname advances e->name past the colon but kept the Round-21
namebp flag set - name[-1] would read NAME BYTES as a doc pointer
for unattached prefixed elements. Masked by register-on-create
(map hit precedes the backpointer fallback). Fix: split_qname
clears the flag; RED spec MutNameBackpointer.PrefixedSplit-
ClearsBackpointerFlag pins it. LESSON: register-on-create is the
ONLY backstop for split-name elements - any register-elision perf
work must first restructure the backpointer to survive the split.
## PARITY MANDATE + streaming design (2026-09-07, user decision)

User: "We must beat it, at least we must reach parity now and
improve speed later. Then start and finish HTML and RNG." PARITY
TARGETS: heavy <= 2.88ms (now 3.59 after register-elision), light
<= 2.72ms (now ~3.55). Remaining gap ~20%.

THE DESIGNATED PATH: streaming result emission for apply_string.
Key soundness fact: the XSLT data model forbids reading the
principal result tree during execution - streaming it is ALWAYS
spec-safe. Phase plan:
- Phase 1: result-sink abstraction in xslt_exec (mode: TREE |
  STREAM). STREAM = SerializeBuffer + open-element stack +
  pending-tag buffer (xsl:attribute must land inside the open tag
  before the first child - libxslt's pending-writer pattern).
- Phase 2: element/text/attr fast-path emitters at the out_* funnels
  (out_place_elem / out_append_elem / text appends); tree ops fall
  back by flushing pending tags.
- Phase 3: gate = sheet scan at parse time (xsl:variable/@select
  RTF shapes don't matter - only the PRINCIPAL result streams;
  disable for d-o-e edge shapes the 205-suite pins if any diverge).
- Red zone: bug-98 ws_mixed, cdata-section-elements, character
  maps, indent (depth tracking), html method. Each needs its
  stream-side twin or the gate excludes the sheet.
Expected: ~15% on dispatch shapes + register-elision's 4% ~= parity.
LANDED this round: register-elision (PR #897, heavy ~4%); #869
digest (PR #896) - orthogonal capability.
NEXT SESSION ORDER: streaming phases 1-3 -> HTML #659 (implied head
-> foster -> AA; the 865-case bucket) -> RNG #878 phases 1-4.

## SAX binding-drain: kind-strip shipped, field-strip inconclusive (2026-09-07)

PR leptris-ruby#156 MERGED (user-directed after repeated asks): cold
bare Elem#[] 17.2 -> 4.0 allocs/read; SAX kind-strip (get_uint8 per
event, no full-record copy/unpack) 24.2 -> 23.0ms quiet-window.
FOLLOW-UP TRIED AND NOT SHIPPED: universal field strip (one unpack
per chunk: kind + name_off/len + text_off/len per record, template
"Cx7LLLLx#{STRIDE - NAME_OFF - 16}") — the tail skip is
STRIDE - NAME_OFF - 16 (four Ls END at NAME_OFF+16); TEXT_LEN is an
OFFSET not a size (first attempt overran; the arity spec caught
it). Corrected version measured INCONCLUSIVE under load 30-100
(fix 32.7-35.8 tight vs main 25.9-44.0 spread — windows not
comparable): the ~300k-value unpack + array allocation costs
roughly what the ~120k get_uint32 (~60ns each) saved. RETRY only on
a quiet machine, best-of-20; if it loses again the drain residual
is Ruby dispatch itself and the real lever is an ENGINE-side
batched drain API (parallel arrays out of one call) — file as an
issue when picked up.

## CLOSED 2026-09-12 (measured): moxml NS-heavy reads 0.69x —
## the expanded-name/prefixed-path lever

HYPOTHESIS DISPROVEN. Built the full parse-time stamping design
(DParser scope-binding stack + element ns_uri stamp + attr
resolved-uri side-cache + public fast paths), guard specs green
— and the C-level bench did not move: expanded-name reads are
~20ns either way (first-pass 0.336ms -> 0.344ms / 15k attr
reads; element traversal identical; the lazy element cache and
the short decl lists keep the walk off the profile). The 0.69x
row is NOT in the C resolver — it lives in the leptris-ruby
adapter loop (FFI call count / per-read Ruby allocations).
NEXT ACTION (leptris-ruby repo, PR-only): profile the moxml
adapter's expanded-name loop in Ruby (benchmark-ips + stackprof);
candidate levers = batched expanded-name reads (one C call
returning name+prefix+uri, e.g. a leptris_element_expanded_name
out-param API the adapter uses instead of 3 FFI calls) and
memoized Ruby-side wrappers. The C-side work is banked in this
file's git history if the batched accessor lands (branch


## CLOSED 2026-09-12 (final): the moxml 0.69x row — measured to
## the bottom, four ways

1. C resolver: parse-time stamping built + guard-specs green —
   bench unmoved (~20ns/read either way; the walk is off the
   profile). Discarded unpushed.
2. Raw FFI fan-out: 3 calls 48ms vs 1 batched+3 reads 52ms per
   180k reads — batching does not pay at this call cost (~80ns).
3. Gem steady-state: identical (memoization already absorbs
   repeats; the loop cost is element_children wrapper allocs).
4. Gem cold first-touch: batching SLOWER (63ms -> 100ms; the
   buffer+tuple allocation per element outweighs the saved
   crossing). Reverted.

VERDICT: the row sits in moxml's own wrapper/materializer layer
(its adapter rides the leptris GEM, and our gem layers are
measured off the deficit). leptris_element_expanded_name ships
(v1.9.144) as an adapter-available primitive for anyone who does
pay per-read fan-out; our gem keeps per-accessor calls. If the
row must move, the work is in moxml's repo (external-PR style):
profile their NS-read bench (benchmark-ips + stackprof) and fix
their materializer — the leptris stack underneath is not the
cost.

## DOM vs pugixml race (2026-09-12, user directive: "why are we not
## zero-copy?")

Answer: the PARSER already is (names/attrs/values NUL-terminated
in-place in the buffer copy, same strategy as pugixml load_buffer).
The losses were bookkeeping, and the big one is FIXED (PR #1017):

- attr-heavy-5k parse: 2086 -> 769 us (2.7x faster; 4.9x -> 1.96x
  vs pugixml). Root cause: the #635 raw-view was built with one
  pool_alloc + tail walk PER ATTR + one ns_cache alloc per
  attr-bearing element; the #542 owner stamp added a post-loop
  memchr per attr. Fix: 128-entry chunk carve + parser-local tail;
  owner cache inline at colon detection. Specs:
  RawAttributesAcrossChunkBoundaries (two chunk exhaustions).
- Remaining parse gap (769 vs 393): element path (name-hash walk,
  register-on-create bookkeeping), the fused sizing pre-scan +
  buffer copy, per-element ns_cache alloc. Next levers, ranked by
  expected value: (1) ns_cache chunk carve, (2) element name-hash
  fusion with the split walk (already fused — dp_split_hash_name),
  (3) the sizing pre-scan. Each ~5-10%.

## Measured-and-BANKED: the mutation rows (append 3.4x, set-attr
## 2.4x behind pugixml) are a NODE-LAYOUT lane, not tweaks

Split (best-of-200 harness, 10k ops): create-only 20.6 ns/op,
append-only 18.9 ns/op; pugixml append_child 11.6 ns/op for BOTH.
Structural floor for the current 96-byte element: memset ~4ns +
two pool carves ~5ns + FNV hash ~2ns (create >= 12ns); call chain
+ double parent decode + type-dispatching sibling setter + ~8
stores (append >= 8ns) ~= 20ns floor vs pugi 44-byte nodes at
11.6ns. Cheap swings identified (dispatch-free sibling store,
wrapper chain collapse) total <= 10% — below the effort bar.
Reaching <= 1x needs the compact-node redesign (44-56B nodes,
lazy name-hash/init) — the same class of decision as the #682
scope call; USER CALL. Do NOT micro-grind this row awaiting it.
Known hazard from the split_qname incident: register-on-create
elision (part of any lazy-init design) must FIRST restructure the
name backpointer to survive in-place QName splits.

## Measured-and-REVERTED 2026-09-12 (round 3): ns_cache chunk carve

The per-element ns_cache pool_alloc (5000 on attr-heavy) replaced by
a 64-entry chunk carve measured 684 -> 640 us (-6.4%) BUT broke the
libxslt suite deterministically (52/205; the gate caught what the
standalone tree probe missed). Isolated symptom: on
<xsl:stylesheet xmlns:xsl=...>, leptris_element_expanded_name
returns prefix=xsl uri=NULL with the carve (uri resolves on main).
Both halves independently contribute (xmlns-branch reroute alone:
52; dp_raw_attr ensure-cache alone: 36). NOT re-applied — root
cause unfound; next attempt starts by tracing the element_query
prefix->URI resolver (own cache slot vs declarations walk) before
touching allocation. LESSON: allocation-pattern changes in the
parse path are LAYOUT-SENSITIVE here; the suite is the gate, single
-shape probes are not.
