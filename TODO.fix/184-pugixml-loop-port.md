# TODO 184 — Pugixml parse-loop port: fused value scan

**Priority**: P0 (the K=100 lever)
**Status**: done (this release)

## Goal

Port pugixml's single-pass attr-value scan into `direct_parse.c`:
one loop finds the closing quote AND flags `&` for entity routing.

## Why (measured, TODO 183 Phase 5 profiling)

Controlled Release A/B proved struct width (64 B attr) and cross-TU
inlining (amalgamation) are each perf-neutral at K=100. The measured
gap was ~2.8× (taurus 1986 µs vs pugixml 711 µs, medians) with a
~13 ns/attr delta.

Root cause: taurus scanned every attr value TWICE —
`memchr(val, quote, …)` for the closing quote, then a second pass
for `&` (TODO 174's inline loop). libc `memchr` pays ~10 ns setup
even on 6-byte values (the TODO 174 finding, which we applied only
to the amp check). At K=100 (100 K attrs, avg value ~6 B) that is
~1 ms of pure setup. pugixml's `ct_parse_attr` loop reads each byte
once.

## Change

`dp_parse_attrs` scans the first 48 bytes inline — byte loop stops
at the quote (no entities) or flags `&` on the way past; values
longer than 48 B fall back to SIMD (`memchr` for the quote +
`taurus_text_contains` for `&` over the scanned span).
`dp_add_attr_inline` takes `has_amp` as a parameter instead of
re-scanning. Semantics unchanged (lazy vs DTD entity routing
preserved).

## Measured impact (benchmark_many_attrs K=100, Release, 10 runs)

| | median | gap to pugixml |
|---|---|---|
| v0.19.9 | 1986 µs | 2.79× |
| + this change | **1095 µs** (1091–1099) | **1.54×** |

45% faster. All 508 tests pass.

## Remaining gap (~1.5×)

Next levers, in expected-value order:
1. Same fused-inline principle for the text-content scan
   (memchr for `<` per text node) and the name scan dispatch.
2. K=5/20/50 medians still trail (element wiring + text path).
3. Buffer-copy accounting: taurus memcpy's the input per parse;
   verify the harness gives pugixml identical copy semantics.
