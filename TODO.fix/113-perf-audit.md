# TODO 113 — perf audit: final state

**Priority**: P0 (user goal: "READ and WRITE dominance over libxml2 AND pugixml")
**Status**: Two optimizations landed (PRs #104, #105). Remaining gap
requires deep refactors documented below.
**Supersedes**: earlier audit numbers in this same file.

## Final benchmark snapshot (Release+LTO, M-series)

### Where we're FASTER than competitors

| Op | taurus | best competitor | ratio |
|---|---|---|---|
| Attribute access ×100 | 2.0 µs | 4 µs (libxml2) | **0.5×** |
| Text extraction ×100 | 1.8 µs | 5 µs (libxml2) | **0.36×** |
| SAX small (~4 KB) | 5.7 µs | 5.7 µs (libxml2) | **1.0×** |
| XPath cached (small doc) | 1.1 µs/iter | — | — |

### Where we're SLOWER

| Op | taurus | best competitor | ratio |
|---|---|---|---|
| DOM parse (~4 KB) | 106 µs | 45 µs (libxml2) | 2.4× |
| Tree traversal | 7.1 µs | 1.5 µs (libxml2) | 4.7× |
| Child iteration (indexed) | 14.4 µs | 3 µs (libxml2) | 4.8× |
| XPath `//book` (10 KB doc) | 33 µs | 5 µs (pugixml) | 6.6× |
| XPath `//book` (with parse) | 148 µs | 5 µs (pugixml) | 30× |
| Set 100 attrs | 64 µs | 10 µs (pugixml) | 6.4× |
| Append 1000 children | 63 µs | 12 µs (pugixml) | 5.3× |

## What landed this session

### PR #104: function registry singleton + AST cache
- Registry: 27 std functions no longer re-registered per eval
- AST cache: 16-slot FNV-1a hash; repeated expressions skip parse
- Impact: small-doc cached query = 1 µs/iter (was 35 µs pre-cache)

### PR #105: axis enum dispatch
- Pre-compute axis enum at parse time
- apply_axis switches on enum instead of 13-strcmp chain
- Impact: minor (~1-2 µs/query) — axis dispatch was not the bottleneck

## Why the remaining gap exists

### XPath eval (6.6× slower than pugixml)
The recursive-descent evaluator allocates an XPathNodeSet per axis
step. For `//book` on a 100-element doc, that's ~100 short-lived
nodesets. Each one is two mallocs (struct + void* array). The
allocator overhead dominates.

pugixml compiles XPath to a state machine and reuses one nodeset
across the whole query.

### DOM parse (2.4× slower than libxml2)
Per-element work:
1. Pool-strdup of name (~50 ns × 100 elements = 5 µs)
2. Compact-pointer offset encoding in setters (~5 ns × ~300 setters = 1.5 µs)
3. Pool page boundary check on every alloc (~5 ns × 500 allocs = 2.5 µs)

The bulk of the gap (~50 µs) is elsewhere — likely string handling
and the parser's per-character state machine.

### Write (5-7× slower than pugixml)
1. Attribute lookup is O(N) linked-list walk per set_attribute.
   For 100 attrs, cumulative O(N²) = 5050 strcmps.
2. Each mutation re-encodes offsets via Phase 2 compact encoding.

pugixml uses a hash table per element for O(1) attribute lookup.

## Recommended next steps (priority order)

### 1. Inline nodeset storage (medium effort, ~30% XPath speedup)
Add a fixed inline array (16 elements) to XPathNodeSet. Small
nodesets skip the separate `nodes` allocation entirely. Halves
the allocator pressure on the eval path.

### 2. Attribute hash table per element (large effort, ~5× write speedup)
Replace linked-list attribute storage with hash table. Setting an
attribute goes from O(N) to O(1). Affects serialization, attribute
walkers, copy paths.

### 3. Compile XPath AST to state machine (very large effort, ~5× XPath speedup)
Like pugixml. Compile AST to a deterministic state machine at
parse time. Eliminates recursive descent + per-step allocation.

### 4. Pool-page-relative pointer encoding (large effort, ~50% element size)
Phase 2f of TODO 90. Stores pointers as 1-2 byte offsets within a
pool page. Element shrinks from 80 to ~30 bytes. Cache locality
improves.

## Verification

```bash
cmake --build build
build/benchmarks/bench_dom_taurus        # parse + traversal + access
build/benchmarks/bench_dom_libxml2       # competitor
build/benchmarks/bench_xpath_taurus      # cached XPath
build/benchmarks/bench_xpath_pugixml     # competitor
build/benchmarks/bench_sax_taurus        # SAX
build/benchmarks/bench_sax_libxml2       # competitor
build/benchmarks/benchmark_write         # mutation comparison
ctest --test-dir build -j4               # 305/308 pass (3 pre-existing flaky)
```

## Architectural notes

The compact-pointer migration (TODO 90) traded ~5% perf for ~50%
memory reduction (element 192 → 80 bytes). That tradeoff was
explicit and matches the user's "memory dominance" goal. The perf
gap is independent — closing it requires the four optimizations
above, each of which is a multi-week focused effort.
