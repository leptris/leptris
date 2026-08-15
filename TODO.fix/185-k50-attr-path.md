# TODO 185 — K<=50 element/attr path vs pugixml

**Priority**: P0 (the remaining gap)
**Status**: round 2 done (scanner-shape levers dead; code reverted to main)

## Measured state (2026-08-15, Release, best-of interleaved)

Gap by K (median): K=5 1.60x, K=20 2.05x, K=50 2.24x, K=100 1.55x.
Per-attr cost curve (taurus, best-of): K=1 21ns, K=5 11.6,
K=20 15.0, K=50 14.3, K=100 10.7 — non-monotonic dip at K=100.
pugixml: flat ~7 ns/attr at every K.

## Round 2 (2026-08-15): profile + four shape experiments — ALL DEAD

macOS `sample` profile of the K=50 doc (601 KB, 1000 elems x 50 attrs):
- **86%** direct_parse_internal main loop
- 6% memmove (buffer copy — pugixml pays this too)
- 4% count3 pre-sizing scan
- 1% __bzero (elem block), 1% strlen (close-tag check)
- <2% everything else

The loop IS the battleground; no hidden sink outside it. Four
experiments on the loop's micro-shape, each reverted after
interleaved A/B (fresh Release dirs, min of 4-8 runs):

1. **skip_ws/scan_name local-pointer form** — disasm confirmed
   the naive form stores p->pos per byte (`str x8,[sp,#224]` in
   the loop body); the local form compiles to 3-instr loops
   (`ldrb [x8,#1]!` fused load+inc, zero stores). MEASURED
   **+4% to +13.7%** regression at all K.
2. **skip_ws early-out writeback variant** (zero-byte skips pay
   nothing): **+1% to +4.8%**.
3. skip_ws early-out only (scan_name reverted): **+1% to +2.5%**.
4. **strlen kill** — close-tag check re-walked the open name
   (`strlen(open->name)`, ~1% in profile); replaced with an
   `open_len_stack[DP_MAX_DEPTH]` parallel to open_stack
   (pugixml's cursor-cache shape): parity at K>=20, **+3.4% at
   K=5** (consistent across 12 runs).

**Conclusion: direct_parse_internal is at a compiler local
optimum.** ANY perturbation — even removing work — costs 1-3%
somewhere via register allocation / stack layout in the
mega-function. The per-byte stack store retires at 1/cycle and
was never the bottleneck; the real per-byte cost is the two
loads (char + chartype table), which pugixml also pays.

**Rule for future rounds**: no micro-shape edits to
direct_parse_internal without (a) a profile-backed hypothesis
and (b) an interleaved 8-run min A/B gate passing on ALL four
K sections. Three of today's four failures looked like certain
wins on disasm.

## Eliminated this round

- finalize O(K^2) indexed walks (~5M at K=100) — real quadratic,
  immaterial cost (sub-ns decode). Fixed in finalize, validator
  (x2), c14n (x3).
- elem_block memset: ~1.2 ns/elem (E1).
- dp_wire_child: ~1 us total (E2).
- Fixed per-parse cost: ~0 (tiny-doc parse < 0.05 us).

## Next levers (ranked)

1. ~~**Attr init store volume**~~ — DEAD (2026-08-15 disasm): clang
   -O3 already emits the floor — `stp`x2 (views) + one 8 B zero
   (ns_cache+next_cp+has_entities) + `str wzr` (name_hash) = 4
   stores/attr. Nothing to fold. CHECK DISASM BEFORE OPTIMIZING.
2. ~~**Scanner loop structure** at mid-K~~ — DEAD (2026-08-15
   round 2, four measured experiments above): compiler local
   optimum; perturbation of any kind costs 1-3%.
3. **compact_string endgame (TODO 182)** remains the structural
   answer for memory, but the 64 B cache-line law (element.h)
   blocks attr shrink unless the block layout changes.
4. The residual ~5-8 ns/attr is feature cost (line tracking,
   count3 pre-scan, DTD-capable routing) + the shared two-load
   chartype scan. Only FEATURE REMOVAL or a different data
   layout moves it — not loop shape.

## Feature costs we pay that pugixml does not (context for the gap)

- Line tracking (issue #223): '\n' compare per ws-skip + per text
  span fold. No newlines in the benchmark docs, so ~1 compare/ws.
- Arena pre-sizing scan (count3): one SIMD pass over the doc —
  ~2-4% at K=5, ~1% at K=100.
- DTD-entity-capable value routing.

The remaining ~5-8 ns/attr is diffuse micro-cost plus these
feature deltas — no single lever left. Judge any future change on
the K=20-50 mid-range (the honest gap: ~2.0-2.2x).

## Note

The K=100 dip (10.7 ns) suggests large attr blocks (6.4 MB) hit a
better allocation path (fresh mmap, streaming faults) than mid-K
blocks — machine texture, not portable. Judge changes on the
K=20-50 mid-range.
