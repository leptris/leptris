# TODO 185 — K<=50 element/attr path vs pugixml

**Priority**: P0 (the remaining gap)
**Status**: round 1 done (quadratic kill, perf-neutral)

## Measured state (2026-08-15, Release, best-of interleaved)

Gap by K (median): K=5 1.60x, K=20 2.05x, K=50 2.24x, K=100 1.55x.
Per-attr cost curve (taurus, best-of): K=1 21ns, K=5 11.6,
K=20 15.0, K=50 14.3, K=100 10.7 — non-monotonic dip at K=100.
pugixml: flat ~7 ns/attr at every K.

## Eliminated this round

- finalize O(K^2) indexed walks (~5M at K=100) — real quadratic,
  immaterial cost (sub-ns decode). Fixed in finalize, validator
  (x2), c14n (x3).
- elem_block memset: ~1.2 ns/elem (E1).
- dp_wire_child: ~1 us total (E2).
- Fixed per-parse cost: ~0 (tiny-doc parse < 0.05 us).

## Next levers (ranked)

1. **Attr init store volume** — 48 B of stores per attr (2 views
   16 B each + ns_cache 8 B + tail 8 B) vs pugixml's 36 B. Our
   views carry length (needed for zero-copy compare); pugixml
   stores bare pointers. Candidate: single 8 B store for
   ns_cache, single 8 B for the tail (they're adjacent?) — check
   field order; fold into 2 stores.
2. **Scanner loop structure** at mid-K — the K=20-50 bump is not
   store-volume (flat curve expected); suspect branch texture in
   dp_parse_attrs per-attr dispatch. Compare against pugixml
   strconv_attribute shape.
3. **compact_string endgame (TODO 182)** remains the structural
   answer for memory, but the 64 B cache-line law (element.h)
   blocks attr shrink unless the block layout changes.

## Note

The K=100 dip (10.7 ns) suggests large attr blocks (6.4 MB) hit a
better allocation path (fresh mmap, streaming faults) than mid-K
blocks — machine texture, not portable. Judge changes on the
K=20-50 mid-range.
