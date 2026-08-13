# TODO 161 — pugixml-gap closure: realistic remaining work

## Purpose

Single consolidated survey of what's actually left to close the
gap vs pugixml, with realistic assessments. New work should
reference this doc rather than re-deriving where the gap lives.

## Where taurus is ahead of pugixml

- **XPath element index** (TODO 132): O(K) descendant lookups
  vs pugixml's O(N) tree walk.
- **XPath attribute index** (TODO 133): O(1) `[@id='x']` lookup.
- **Bytecode VM with fusion** (TODO 120 + 159 Phase D): one
  opcode for `//book[price > 30]` vs pugixml's AST walk.
- **Compact pointer encoding** (TODO 90): 4-byte offsets vs
  pugixml's 8-byte pointers. Smaller memory footprint.
- **Element struct size**: 64 B (one cache line) vs pugixml's
  88 B `xml_node_struct`.

## Where pugixml is still ahead

### Parse: ~3× gap

pugixml parses the 24 KB medium doc at ~18 µs; taurus at ~57 µs.
Measured per-element gap is ~100 ns, dominated by per-attr work
(see TODO 149 for the breakdown).

The remaining per-attr work that drives the gap:
- attr_init: setting 4 string views + name pointer + has_entities
  flag is ~30 ns. pugixml inlines the equivalent via aggregate init.
- FNV-1a hash inline: ~5 ns. pugixml doesn't hash attr names
  because it doesn't have an attr index.
- name_view NUL-termination handling: ~5 ns. pugixml uses the
  same in-place technique.

**What would actually help:** none of these is independently
large enough to justify the complexity of removing it. The gap
is structural — pugixml ships fewer features per attr.

### Full-cycle XPath (`bench_xpath_pugixml`): 2–4× gap

`bench_xpath_pugixml` measures parse + xpath + free per
iteration. The XPath layer is competitive (`bench_xpath_taurus`
is mostly sub-µs); the gap is dominated by the parse cost above.

### Per-call XPath overhead

Each `taurus_xpath_eval` call pays:
- `xpath_ast_cache_get` hash + scan: ~30 ns (was ~60 ns before
  TODO 159 Phase E drive-by)
- `xpath_context_new` malloc: ~100 ns
- VM run: 200–500 ns
- Result wrap (`xpath_result_new` + nodeset alloc): ~150 ns
  (was ~250 ns before TODO 159 Phase B free-list)
- Result free: ~100 ns

Total ~600 ns per call plus VM run. pugixml's overhead is similar.

## Realistic remaining work

### High-leverage, low-risk

(none currently identified — TODOs 154, 155, 159 Phase A0/D/D2/B/E/F
+ drive-bys captured the obvious wins.)

### Medium-leverage, medium-risk

- **Direct-pointer tree walk in VM** (TODO 159 Phase C): replace
  the compact-pointer decoder (function call + 2 branches) with
  direct pointer arithmetic in the VM hot path. Est 1.1–1.3× on
  traversal-heavy queries. Risk: compact-pointer decoder is
  correct in cases the inline version would miss (int32 overflow).

- **Result struct free-list** (like the nodeset free-list in
  Phase B but for `taurus_xpath_result`): saves one malloc/free
  per eval. Est ~5% on bench_xpath_taurus.

- **Stack-allocated XPathContext** (Phase G candidate): the
  context is ~256 bytes. `taurus_xpath_eval` could stack-allocate
  it instead of malloc'ing. Saves ~100 ns per call. Risk: callers
  that stash the context pointer would break — needs an audit.

### Low-leverage / not worth pursuing

- **Computed-goto VM dispatch** (TODO 159 Phase A original): GCC
  only. PGO covers most of the same ground without portability
  cost. Not pursuing.

- **SIMD parse loops** (TODO 157): investigated, deferred —
  overhead exceeded benefit for short XML tokens. Re-test only
  if profile shifts dramatically.

- **Compact attr list** (TODO 156): 32 call sites to migrate for
  a 4-byte-per-attr saving. The migration risk outweighs the gain.

- **Field reordering based on access profiling**: taurus's element
  struct is already 64 B (one cache line). Reordering wouldn't
  move the needle.

## Strategic read

taurus is already faster than pugixml on pure XPath workloads
where the element index kicks in. The remaining gap is in parse,
where pugixml ships a deliberately minimal per-attr
implementation. Closing it further would require giving up
features we want (line tracking, namespace prep, attr hashing
for the index). Not worth it.

For users who want maximum throughput on parse-dominated
workloads, the recommendation is: enable PGO
(`-DTAURUS_ENABLE_PGO=GENERATE` → run workload → `USE`) and
benchmark against the actual production XML, not the synthetic
medium doc.

## Status

Survey complete. No new phases currently scoped — see
[[TODO 159 Phase G]] for the most recent landed work.
