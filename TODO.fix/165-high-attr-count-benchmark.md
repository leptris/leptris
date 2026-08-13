# TODO 165 — High-attr-count parse benchmark (test coverage gap)

## Status

Pending. Identified while writing TODO 159 Phase G regression tests.

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
