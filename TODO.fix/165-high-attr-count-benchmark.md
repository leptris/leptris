# TODO 165 — High-attr-count parse benchmark (test coverage gap)

## Status

**DONE.** `benchmarks/comprehensive/benchmark_many_attrs.cpp`
added; registered in `benchmarks/CMakeLists.txt`.

Benchmark output (Release + LTO, clang arm64, 1000 elements):

| K attrs | taurus (µs) | pugixml (µs) | Ratio |
|---------|-------------|--------------|-------|
| 5       | 199         | 49           | 4.07× |
| 20      | 629         | 245          | 2.57× |
| 50      | 1320        | 450          | 2.93× |
| 100     | 4391        | 830          | 5.29× |

Per-attr cost computed: taurus ≈ 38 ns/attr, pugixml ≈ 8 ns/attr.
The 30 ns/attr delta is structural (FNV-1a hash + entity memchr +
string-view setup + per-attr bookkeeping) — see TODO 161 survey
for why we don't strip these features to match pugixml.

The K=100 case is the most sensitive regression target — any
per-attr cost increase should show up here first.

## Why

The existing parse benchmarks (`benchmark_parse`,
`bench_xpath_pugixml`) use XML with 2–3 attrs per element. This
hides per-attr-wiring costs. The TODO 159 Phase G fix (parser-
local last-attr cache) addressed an O(K²) per-element cost, but
we have no benchmark that would have surfaced the regression
_before_ it became obvious from code inspection.

## Plan

Add `benchmarks/comprehensive/benchmark_many_attrs.cpp` that
generates XML with 20, 50, and 100 attrs per element across 1000
elements. Compare taurus vs pugixml on each.

This serves two purposes:
1. Catches future regressions in per-attr wiring.
2. Documents where taurus is genuinely faster (the new O(K)
   per-element wiring + bulk-allocated attr structs should make
   us competitive with pugixml even at K=100).

## Risk

None. Pure test/benchmark addition.

## Expected impact

No production code change. Future regression coverage only.

## Status

Pending. Useful next benchmark work, but lower priority than the
medium-leverage TODOs 162/163.
