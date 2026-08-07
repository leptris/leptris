# TODO 105 — performance baseline vs libxml2 / pugixml (post-PR #66)

**Priority**: P1 (user goal: "beat BOTH libraries in ALL modes")
**Status**: real baseline established; gap analysis below

## The benchmark LEN bug (now fixed in PR #66)

Until PR #66 the benchmark suite claimed to compare taurus vs libxml2
but was actually overreading into adjacent memory:
`BENCH_XML_MEDIUM_LEN` declared 5787, actual 4932 (855-byte overread);
`BENCH_XML_LARGE_LEN` declared 10420, actual 8122 (2298-byte overread).

The libxml2 benches produced hundreds of "Extra content at end of
document" errors per run and reported nonsense numbers. The "libxml2
DOM parse = 47 µs" figure that was floating around in PR descriptions
was a guess, not a measurement.

With the bug fixed, real numbers are below.

## Real baseline (local macOS, M-series)

### SAX (small ~1KB, medium ~5KB in-memory)

| Benchmark | Taurus | libxml2 | Ratio |
|---|---|---|---|
| SAX small | 6.28 µs | 5.39 µs | taurus **1.16x slower** |
| SAX medium | 24.89 µs | 17.44 µs | taurus **1.43x slower** |
| SAX small throughput | 140 MB/s | 163 MB/s | |
| SAX medium throughput | 222 MB/s | 316 MB/s | |

SAX is the closest axis to libxml2 after PRs #58/#59 (scratch arena +
vectorized scans + switch dispatch).

### DOM parse (~5KB medium fixture)

| Benchmark | Taurus | libxml2 | Ratio |
|---|---|---|---|
| Parse + Root | 119 µs | 47 µs | taurus **2.53x slower** |
| Tree Traversal | 5.45 µs | 1.52 µs | taurus 3.59x slower |
| Attribute Access (100x) | 1.57 µs | 2.98 µs | taurus **1.90x faster** |
| Text Extraction (100x) | 1.35 µs | 3.46 µs | taurus **2.56x faster** |
| Child Iteration (100x) | 10.93 µs | 2.50 µs | taurus 4.37x slower |

DOM access is mixed — taurus wins on attribute access and text
extraction (lazy StringView), loses badly on tree traversal and child
iteration (linked-list walk vs libxml2's array-backed children).

### XPath

Existing bench files (`bench_xpath_taurus`, `bench_xpath_libxml2`, etc.)
run but their comparison has not been analyzed yet.  Numbers exist in
the CI benchmark artifact (`benchmark-results-<os>/summary.md`).

## Where the DOM parse gap is (TODO 103 update)

119 µs for 200 elements = ~600ns per element. libxml2 at 47 µs is
~235ns per element. The per-element work taurus does that libxml2
doesn't:

1. **Eager name conversion** (`taurus_sv_to_cstr_pooled` at element.c
   create time). Necessary for thread-safety (lazy conversion has a
   race on first access from multiple threads). ~50ns × 200 = 10 µs.

2. **Per-attribute pool allocations**. ~3 pool_alloc calls per attr
   (struct + name copy + value copy). 600 attrs × 3 calls × ~10ns
   pool overhead = ~18 µs.

3. **Strict-mode validation guards**. 8 sites × function-call overhead
   per element. PR #62 cached this in Parser struct — was ~5 µs,
   now neutral.

4. **UTF-8 byte-by-byte validation in name parser**. The name parser
   validates UTF-8 continuation bytes manually. SIMD-vectorized UTF-8
   validation would save ~5 µs.

5. **Recursive descent**. Each element recurses; ~50ns per frame × 200
   = 10 µs of pure stack-frame setup. An iterative parser with an
   explicit stack would eliminate this.

Total addressable: ~38 µs out of 119 µs (32%). That would bring taurus
to ~80 µs, still 1.7x slower than libxml2. To close the rest requires
either:
- A shared XML lexer (TODO 103 Phase 4) that libxml2 already has
  internally — multi-week refactor
- Or accept that libxml2 has 25 years of perf tuning we won't match

## Recommended path forward

**Phase 1 (done — PR #62)**: cache strict_mode in Parser. Neutral perf,
cleaner architecture.

**Phase 2 (deferred)**: lazy name conversion. Risk: thread-safety.
Reward: ~10 µs. Not worth the correctness complexity unless we add
atomics.

**Phase 3 (next, 3-5 days)**: bulk-allocate attribute structs. Pool
free-list of N pre-allocated taurus_attribute structs. Reward: ~18 µs.

**Phase 4 (1-2 weeks)**: iterative parser (no recursion). Reward:
~10 µs. Plus prepares the codebase for the shared-lexer refactor.

**Phase 5 (multi-week)**: extract shared XML lexer used by both SAX
and DOM. Eliminates ~2600 lines of duplication. Centralizes all perf
work going forward.

## SAX remaining gap

SAX medium is 1.43x slower than libxml2. The recursive descent is the
biggest remaining cost (~10 µs of the 24.89 µs total). TODO 102
Phase 3 covers this.

## Acceptance

User goal "beat BOTH libraries in ALL modes" — aggressive target.
Realistic interim targets:

- SAX: within 10% of libxml2 (Phase 3 iterative parse)
- DOM parse: within 30% of libxml2 (Phases 3+4)
- DOM access (traversal, child iter): within 2x of libxml2
  (architectural change — array-backed children)
- XPath: measure first, then plan
