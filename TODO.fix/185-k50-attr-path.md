# TODO 185 — K<=50 element/attr path vs pugixml

**Priority**: P0 (the remaining gap)
**Status**: rounds 4-10 done. Phase-level wins (TODO 187 freeze
walk, TODO 188 fused copy) shipped; every remaining lever measured
dead, parity, or sub-noise. Three consecutive sub-noise results
(rounds 7-9) plus a gate-fail regression (round 10) — the floor is
real and the codegen wall now covers setup-region edits too.

## Round 10 (2026-08-16): pugixml build/config audit + lazy
## string-cache = K=5 +2.4%, reverted

Audited pugixml v1.16 (local checkout ~/src/external/pugixml,
source + CMake) flag-for-flag and trick-for-trick:

- **Build flags: nothing to copy.** Their CMake ships zero
  optimization flags (no -O3/LTO/arch); Homebrew builds plain
  Release. We already ship -O3 + thin-LTO + hidden visibility.
  `LEPTRIS_TARGET_ARCH=native` measured MIXED (K=20 −8.9%,
  K=5 +6.8%, K=100 +1.4%) — not a lever, not apples-to-apples.
- **Correction to TODO 182 notes: pugixml attrs are 40 B**
  (5 raw pointers, doubly-linked via prev_attribute_c), nodes
  64 B. Our 48 B attrs / 64 B elements are DENSER — the residual
  mid-K gap is not struct density.
- **SCANWHILE_UNROLL** (4-byte unroll, unlikely-marked exits) is
  a loop-body shape — the class measured dead 6×.
- **Setup asymmetry (actionable)**: pugixml embeds the first
  memory page in the xml_document object (zero setup allocs);
  we eagerly build a 128-bucket string-cache table per parse
  (len >= 256) that the parse path NEVER uses (attrs are views;
  only ns-XPath functions and DOM mutation intern).

Implemented lazy table creation (create-on-first-intern in
pool.c, intern routing in sv_to_cstr_pooled, eager creation
deleted from direct_parse setup) + 64 B-aligned elem/attr block
(phase-determinism for the 48 B stride). 541 tests + ASAN +
zero leaks. Measured (2× 8-run interleaved Release A/B, fresh
dirs, min): K=5 +2.4% CONSISTENT (52.6-52.8 vs 51.3-52.1 in
16/16 runs — alignment variant identical, so not cache phase),
K=20/50/100 parity. Deleting three setup lines perturbs the
mega-function's codegen exactly like the round 2-3 loop edits:
the ~100 ns table saving is invisible next to a 1.2 µs layout
shift at K=5. Reverted; recovery confirmed (51.8-52.3 post-
revert). **The codegen wall covers setup-region edits inside
direct_parse_internal. Only changes fully OUTSIDE the function
(TODO 187/188 shapes) can win.**

## Round 9 (2026-08-16): bulk text-node block = PARITY, reverted

dp_add_text_inline with a text_block in the parser (mirroring the
attr bulk block): eliminated the per-text-node pool_alloc call.
535 tests + ASAN + leaks clean; measured PARITY at every gate
(parse K=5/20/50/100 and the XPath cycle) — leptris_pool_alloc is
a bump allocator costing ~3 ns amortized; removing the call is
below the measurement floor. Reverted. (Post-190 cycle profile:
parse 83.5%, eval 13.8%, free 1.4% — the cycle is parse-bound and
the parse loop is closed.)

## Round 8 (2026-08-16): UTF-8-declaration fast path = sub-noise, reverted

The encoding detour for `<?xml ... encoding="UTF-8"?>` documents
(full-document memcpy + two strdups in auto_convert before
direct_parse copies again) was bypassed by a bounded declaration
scan in the leptris_parse_string fast path. Correct for all gate
cases (UTF-8/lower/no-enc/latin1/UTF-16 probes + 535 tests + 19
encoding tests), but measured: cycle CPU PARITY (the detour is
~0.15 µs on the 10 KB cycle doc — below the noise floor) and the
parse gate showed an unproven +2-3.6% at K=50/100 on docs that
don't touch the changed path. Real lever, payoff below measurement
resolution at realistic sizes; reverted per the gate rule.

## Round 7 (2026-08-16): elem-block bzero removal = PARITY, reverted

The last setup-phase candidate (2.2% at K=20 in the profile):
replaced the bulk elem_block memset with explicit zero-init of
every field at element creation (the dp_add_attr_inline pattern).
535 tests + ASAN clean, but measured 8-run A/B: K=5/20 parity,
K=50 +0.8%, K=100 -1.5% — the ~7 init stores cost exactly what
the streaming bzero saved. Reverted; the bzero is genuinely the
cheaper zeroing strategy at this struct size. The phase-level
winning streak (rounds TODO 187/188) ends here — the setup phase
is now as lean as the loop.

## Round 6 (2026-08-16): TODO 182 upper-bound probe — parse thesis dead

32 B views-only stride (no ctrl, no wiring, no finalize — the
theoretical maximum of any split-stream design) vs shipped 48 B:
K=20 +14% WORSE (273.5 vs 239.4), K=100 only −6.5% (1239.6 vs
1326.3; the 64 B point is 1013). Full table in TODO 182. The
attr-design space is now exhaustively measured:
{32-split-upper-bound < 48 = SHIPPED > 56 dead, 64 old law}.

## Round 4 (2026-08-16): the round-4-of-184 law was K-local — 48 B wins

Re-measured the "must stay 64" attr law at every K (the original
measurement was K=100 only). 12-run interleaved Release A/B,
fresh dirs, min per section:

| K | 64 B | 48 B | pugixml | gap 64B | gap 48B |
|---|------|------|---------|---------|---------|
| 5 | 54.4 | 54.4 | 33.5 | 1.62x | 1.62x |
| 20 | 270.5 | **218.7** | 141.0 | 1.91x | **1.55x** |
| 50 | 637.6 | **619.9** | 315.3 | 2.02x | **1.96x** |
| 100 | **1013.4** | 1222.7 | 617.5 | **1.64x** | 1.97x |

Shipped 48 B (TODO 186): mid-K is the yardstick per this TODO,
real-world attr density is < 20/element, and the K=20 win is 19%.
The K=100 loss (~20%) is the straddle penalty in a 4.8 MB
DRAM-streaming block — unavoidable at any 48 B stride. 535 tests,
ASAN, leaks-clean at 48 B.

## Round 5 (2026-08-16): 56 B is strictly worse — 48 B confirmed optimal

Tried 56 B (8 B pad; every 8th slot straddles vs every 3rd at
48 B) expecting to keep half the mid-K win while halving the
K=100 penalty. Measured (4-run interleaved): K=20 268.9 vs
220.8 — **loses the ENTIRE mid-K win** — K=5/50/100 also slightly
worse or equal. The 48 B win is not a smooth density function of
stride; it is specific to the natural packed size (clang folds the
control tail into fewer stores + 25% smaller block). Operating
points measured: 48 = shipped optimum, 56 = dead, 64 = old law.
Remaining: TODO 182 split-stream for K=100 recovery.

## Round 3 (2026-08-15): lazy elem hash dead; PGO measured

5. **Lazy element name_hash** (mirror of the TODO 172 attr pattern;
   parse path stopped walking every name with FNV — pure work
   REMOVED): **+3 to +5% at K=5-50** (56.9→58.6, 279→293, 671→704,
   parity at K=100). Fifth consecutive experiment where even
   deleting work regresses — the codegen/layout shift dominates any
   µarch saving. Reverted.

   **Verdict: closed.** Five experiments, two of them work-removal,
   all regressing 1-13%. direct_parse_internal cannot be improved
   by source-level edits on this compiler. Do not reopen without an
   architectural change.

6. **PGO (the systematic alternative to all five)** — trained on
   benchmark_many_attrs, llvm-profdata, -fprofile-instr-use:
   **-1.2 to -2.2% on every K section** (55.5→54.4, 276→270,
   647→637, 1027→1015 µs, interleaved). Real and free of source
   risk, but far below the ~10-15% it yields on the XPath VM
   dispatch loop. Worth enabling for packagers; not a gap-closer.
   Workflow: `-DLEPTRIS_ENABLE_PGO=GENERATE` → train → `llvm-profdata
   merge` → `-DLEPTRIS_ENABLE_PGO=USE -DLEPTRIS_PGO_DIR=<gen dir>`.
   NOTE: USE defaults to its own binary dir — point LEPTRIS_PGO_DIR
   at the GENERATE dir explicitly.

## Remaining paths (the only two left)

1. **TODO 182 endgame: 32 B attrs** (2/cache line — now ALSO the
   path to recovering the K=100 loss at 48 B; the split-stream
   ctrl array gives perfect 2/line alignment that no single-array
   stride can).
2. PGO for packagers (1-2%).

## Measured state (2026-08-15, Release, best-of interleaved)

Gap by K (median): K=5 1.60x, K=20 2.05x, K=50 2.24x, K=100 1.55x.
Per-attr cost curve (leptris, best-of): K=1 21ns, K=5 11.6,
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

## Round 11 (2026-08-17): out-of-line attr split = mid-K regression, reverted

The parse-endgame architectural test: `dp_parse_attrs` forced
LEPTRIS_NOINLINE (it was being inlined back into the mega-function;
89% of K=50 parse time sits in that loop region per the fresh
767-sample profile; only 8% in the fused copy, <3% everything
else). Hypothesis: an out-of-line attr parser gives clang an
independent optimization surface on both halves — the documented
escape from the codegen wall. 553 tests pass; 8-run interleaved
fresh-dir gate: K=5 +0.4%, K=20 +2.7% WORSE, K=50 +1.5% WORSE,
K=100 -0.8%. GATE FAIL — reverted, recovery confirmed. Eleventh
measured failure; the wall holds even across a real function
boundary: register warmth/state sharing across the per-element
call costs more than independent codegen gains. Conclusion: the
attr loop's local optimum is genuinely global on this compiler;
only a structurally different parser (two-pass SIMD scan index,
TODO 193) can move mid-K.
