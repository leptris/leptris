# 15 — C-face vs pugixml: close the fair-flags gap (#1842)

Serialbench's fair-flags wave (pugixml `parse_ws_pcdata`, run
40066278528, posted to #1842): the gap is real — **parse 2.81x**,
**generation 2.23x**, **xpath 3.48x** median across 13 CI legs —
and mingw is outright parity (772 vs 770 ips), so the compiler/arch
dimension matters as much as the library. macos-26-intel is our
weakest runner every wave and exaggerates every ratio.

Leads on XSLT (341 vs libxslt 287 ips) — not part of this campaign.

## Track A — parse (2.81x)

Profiled (8s sample, no-LTO build, text-shape fixture; see the
#1842 comment): scanner loop 68% | memchr 8% | dp_wire_child 6.4% |
whole-buffer copies ~5% | count3 sizing pass 4.4% | SWAR utf8
validation 2.8% | elem_pos journal ~2.5%.

- [x] **A1** journal pre-size from the count3 estimate — PR #1844.
- [ ] **A2** fold the UTF-8 validation into the count3 pass
      (`count3_high`: ASCII => valid; high bytes => the existing
      DFA). Saves one full memory pass (~2.8%).
- [x] **A3** FALSIFIED (2026-11-06, reverted). The audit passed
      (only `dp_nul` writes the input — 8 sites; CR/entity paths
      write the pool) and the implementation (drop the pristine
      carve, xml_buffer = scratch for const inputs) passed the full
      suite, but the A/B measured 0.985-1.001 (neutral to slightly
      negative against the predicted +2-3%): the pristine memcpy was
      arena-bump and overlapped the caller-side copy on the cache
      pipeline — lane-18 round 8 had already banked the win. The
      write inventory stands; xml_buffer's only readers are the
      lazy line table (newline offsets — NUL placements never move
      a newline) and the free path.
- [ ] **A4** (sprint 2, the actual gap) scanner-loop surgery in
      `direct_parse_internal`: per-tag branch layout, edge-encode
      elimination on the hot path, NUL-write discipline audit.
      pugixml-class work; the loop is 68% of text-shape parse.

## Track B — generation (2.23x)

- [ ] **B1** profile `leptris_document_serialize` on the serialbench
      fixture (same method: no-LTO sample, MB/s A/B vs pugixml's
      `xml_document::save` to buffer).
- [ ] **B2** levers from B1: expected candidates — escape-table
      scans (the common no-escape fast path), buffer growth
      strategy, per-node virtual dispatch on the writer.

## Track C — xpath (3.48x — the widest)

**C0 (measured, 2026-10-24):** the serialbench xpath ROW re-parses
inside the timed loop (bench.c: `leptris_parse_string` + 3 evals per
iteration) — the row inherits the parse gap. On the exact ops
(306KB canonical medium.xml), the dominant NON-parse cost is the
document-order rank-table build per fresh document:
`doc_rank_table_put` 337 + `index_walk` 78 + zeroing 29 of ~900
non-parse samples — every query on every fresh doc re-walks the
whole tree to assign ranks (#485 table).

- [x] **C1** exact-ops profile (scratch_sb_repro vs serialbench-c
      bench.c BENCH bodies; fixture = serialbench/fixtures
      medium.xml).
- [x] **C2** offset fast path LANDED (evaluator_path.c:
      doc_order_offset_key + the mutation_version==0 gate with the
      LEPTRIS_SORT_SLOW escape) — the historical failures predated
      the adjacent-dedup the fast path now carries ("Same contract
      here"). VERIFIED 2026-11-06: the historical repro (kSubtree,
      //item then //section//item) is clean on the current tree; the
      full xpath suite 104/104; suite 2241/2241. MEASURED: perf
      NEUTRAL on both the 80 KB driver fixture and the canonical
      serialbench medium.xml (306 KB) — fast 188 vs slow 187 median
      ops/s. C0's sample attribution over-weighted the rank-table
      build; the build is not a bottleneck the offsets can remove.
      The xpath 3.48x gap therefore lives in the eval/axis layer,
      not the sort.
      **Track C lever 1 SHIPPED** (perf/1842-node-test-hash): the
      line-level profile of the eval layer named node-test strcmp
      (~65 samples) — matches_node_test now pre-filters candidates
      on elem->name_hash (the AST node caches the test's local-name
      hash lazily; the strcmp remains as the collision guard).
      ABBA x6 median 1.011x on parse+3-query mix, all pairs
      positive, suite 2241/2241.
      **RE-SCOPED 2026-11-07**: the sample inventory above was
      measured on a synthetic `[@stock > '50']` shape that
      classify_predicate does NOT cover — the SCOREBOARD set
      (serialbench-c bench.c:104: `//user | //record`,
      `//user[@id='101']`, `//preferences/theme`) is fully VM-fused
      already; the interpreter is absent from the row. Measured
      shares (RelWithDebInfo+LTO, fresh-doc driver): parse 56.9%,
      union 20.1%, theme 12.4%, attreq 10.7%. The apply_axis/
      step-dispatch levers are de-prioritized (they only pay on
      non-fused shapes).
      **Track C lever 2 SHIPPED** (perf/1842-union-fusion, PR
      #1987): `//a | //b` fuses to ONE doc-ordered pass —
      XPATH_BC_ABSOLUTE_UNION_NAMES. Compiler gate = both branches
      bare plain-name descendant shapes (second step arrives
      RELATIVE_PATH-wrapped — same duality compile_absolute_path
      unwraps). VM: index up = O(n+m) two-pointer merge on the
      parse-offset keys (xpath_doc_order_offset_key exported);
      index not yet built (first query per doc) = one combined
      pre-order walk matching either name; same-name unions =
      one bucket copy. ABBA x6, serialbench medium.xml: union-only
      1255 vs 1156 ops/s (+8.5%), full scoreboard row 945 vs 888
      (+6.4%), all pairs positive, suite 2248/2248 (7 new
      XPathUnionFusion pins incl. the interleaved doc-order trap).
      Next: A4 (parse, 56.9% of the row) remains the other live
      arc; the attr-num-cmp predicate family ([@a > '50']) is a
      real-world fallback shape but NOT on the scoreboard row.
- [ ] **C3** mingw/Windows parity investigation: we are AT parity
      there — diff the codegen assumptions (branch-heavy loops?
      LTO behavior on gcc-win vs clang-mac?) and port whatever
      wins back to the other legs.

      **A4 INVENTORY COMPLETE (2026-11-07, non-LTO function-level
      profile, parse-only scoreboard shape)**: tokenizer own body
      ~39% (the arc itself — char dispatch + element-open/close),
      NEON text scan ~25% (already SIMD; residual = loop overhead
      + scalar tails), dp_wire_child ~15% (branchless, memory-bound
      — near-optimal), dp_text_create 6.3% (carved, lean), pos
      journal 2.8%. Named sub-levers in the element-open region:
      dp_parse_attrs call site (~1300), the source-position gate +
      record (~600), the close-tag scan (dp_scan_name NOT fused —
      a bounded fusion lever ~300). Evidence: #1842 comment
      (function-level table).

## Laws

- CI benchmark legs (quiet runners) gate every number; local A/B
  under a loaded machine is noise.
- Profile with a separate `-DLEPTRIS_ENABLE_LTO=OFF` build dir; LTO
  flattens samples into one frame.
- #1218: arena estimates only ever grow.

Refs #1842, serialbench-c#8.

## Track D — attr-dense parse+read row (post-#1842 closure, 2026-11-11)

The row's CI gate was missing: `bench_append_attr_pugixml` was NOT in
the benchmark.yml binary list, so the attrparse row had no
quiet-runner arbiter (the 2026-11-09 numbers were local). NOW WIRED
(this commit): the binary joins the CI benchmark legs (ubuntu + macos,
Release + LTO, quiet GH runners). The row's verdict comes from those
artifacts.

Fresh profile (2026-11-11, no-inline attribution, attrparse shape 4x16
attrs): dp_parse_attrs 37% | dp_add_attr_inline 20% | dup table 10% |
strlen faces 10% | count3-neon 4.3% | attributes_raw 3.7% | map
mutex+TLS 2.7% | dp_nul 2%.

Cut ledger:
- [x] dup-table growth x8 -> x2 (16-attr elements no longer pay a
      512B rehash per doc).
- [x] dup-probe fast path INLINED in dp_add_attr_inline (~30-45
      ns/doc; the per-attr out-of-line call was the cost).
- [x] REVERTED: inline 16B SIMD value scan (NEON/SSE2) — a 20%
      REGRESSION for the ~11-byte values the shape has: one chunk of
      5x cmpeq + vpadd-ladder movemask loses to the branch-fused
      scalar table loop at short lengths. The 48-byte inline probe
      threshold was already tuned; do not re-attempt below ~64B.
- [ ] count3 small-doc inline: DEAD END — without the scan the
      arena sizing needs len-derived upper bounds that over-reserve
      ~3x and push small-doc arenas into bigger allocator bins
      (the madvise teardown cost the slack floor already fights).
- [ ] A3d pristine-copy skip for newline-free docs (count3 gains a
      4th counter): PARKED — the declaration's version/encoding
      views BORROW doc->xml_buffer; switching them to the scratch
      needs the borrowed-pointer topology audited first.
- The interleaved lane (LEPTRIS_IL_MIN=0) is NOT faster on this
  shape (measured; the small-doc threshold stands).

Local measurement discipline note: under parallel agent load,
leptris degrades ~20% while pugixml stays flat (leptris's scratch +
pristine + arena working set is far more cache-sensitive). Loaded
ratios are meaningless; the CI artifacts gate this row from here on.

## Track D addendum — the first CI-gated numbers (2026-11-11, PR #2001)

bench_append_attr_pugixml's first quiet-runner rows SPLIT BY PLATFORM:
- macos-latest: leptris 1728.6 vs pugixml 2182.9 — leptris 1.26x WIN.
- ubuntu-latest: leptris 2798.6 vs pugixml 1885.2 — leptris 1.48x LOSS.

The ubuntu anomaly is NOT attr-specific: benchmark_parse (tiny/small/
medium, cold-start) shows pugixml ahead on BOTH legs at 0.52-0.65x
(macos) but 0.31-0.40x (ubuntu) — the ubuntu factor roughly DOUBLES
leptris's deficit on every small-parse shape. pugixml is FASTER on
ubuntu than macos (1885 vs 2183), so runner speed does not explain it.

Compiler dimension isolated locally: gcc-16/arm64 builds leptris
FASTER than clang (1428 vs 1500, same machine, same clang-built
pugixml dylib) — the arm64/gcc dimension is healthy. The suspect set
is gcc-13 x86-64 codegen on the fused scanner or a glibc interaction;
ubuntu-24.04-arm joins the benchmark matrix to split the dimension
(same gcc-13 + glibc, different arch+codegen).

Slack-floor shrink for small docs: DO NOT re-attempt — #1846
falsified it (the retained pool amortizes the reservation; measured
0.9743x = negative).

## Track D addendum 2 — the three parse-vs-pugixml gaps (CI-gated, 2026-11-11)

bench_parse_ab in the same CI run (shape-fair MB/s) splits the parse
deficit into three NAMED gaps, none attr-specific:

1. **Text-shape scanner gap**: text 1.5/5MB = leptris 1290-1322 MB/s
   vs pugixml 2086-2199 (1.6x) on macos; 573-577 vs 1896 (3.3x!) on
   ubuntu. This is Track A's scanner surgery with a platform shape:
   the text-run fused loop is where the ubuntu-x86 codegen collapses.
2. **The 13MB collapse (macos)**: attr 502->139 MB/s, text 1290->362
   as the doc grows to 13MB (5.1-5.8x behind). The arena reservation
   sums ~3x len (scratch_bytes + pristine_bytes + text_room are EACH
   len-sized, plus elem/attr/slack terms) -> ~40MB reserved + 2
   full memcpys per parse. Levers: drop the pristine copy when the
   input has no newlines (A3d, parked on the borrowed-pointer audit),
   and fold text_room into scratch (text nodes are substrings of the
   scratch; text_room's 64B/node overhead is the only true extra).
3. **The ubuntu x-factor**: leptris 2798 vs its own macos 1728 on the
   SAME source+flags (62% self-slowdown) while pugixml runs FASTER on
   ubuntu (1885 vs 2183). gcc-16/arm64 local = healthy. The
   ubuntu-24.04-arm CI lane (this PR) splits gcc-13-x86 codegen from
   glibc. Candidate micro-levers once the dimension is known:
   -fno-plt / mmap-threshold pinning (mallopt) / loop restructuring
   where gcc-13 jump-threading bails.

## Track D addendum 3 — the ubuntu dimension RESOLVED (2026-11-11, all three CI legs)

attrparse rows from the final three-leg CI run (same commit):
- macos-latest (clang arm64): leptris 1830.8 vs pugixml 2036.6 — WIN 1.11x.
- ubuntu-24.04-arm (gcc-13 + glibc, arm64): 2091.7 vs 1850.3 — loss 1.13x.
- ubuntu-latest (gcc-13 + glibc, x86-64): 2793.4 vs 1914.7 — loss 1.46x.

Decomposition: gcc-13+glibc on arm64 costs ~0.02-0.2x (mild); the
EXTRA ~30% penalty is x86-64-SPECIFIC codegen of the fused scanner
under gcc-13. The lever: gcc-13-x86 scanner codegen (jump-threading /
register allocation in direct_parse_internal), CI-leg-verified with
the x86 lane as the gate. pugixml again ran FASTER on ubuntu hardware
than macos on both linux legs — the hardware/allocator dimension
favors it; leptris's own linux penalty is the 1.13x arm number.

Also note run-to-run variance: the macos attrparse pair measured
1728/2183 then 1831/2037 across runs (~5-10%) — treat sub-10% CI
deltas as noise bands; the win/loss verdicts above sit outside them.


## Track D addendum 4 — the arena cut LANDED (PR #2003, 2026-11-11)

Content-census copy budget (count3_high amp/valws outputs): macos
13MB collapse CONFIRMED as the reservation — CI-gated bench_parse_ab
before/after: text 13MB 361.6 -> 794.2 MB/s (+120%), attr 13MB
139.4 -> 174.4 (+25%); the 13MB gaps halved (5.8x -> 2.57x text,
5.14x -> 3.84x attr). macos attr-5MB measured -32% in the same run —
single-datapoint between-run variance on that row (two runs only);
watch it on the next run before treating as real.

Remaining 13MB terms: scratch (len, required) + pristine (len, A3d
parked on the borrowed-decl audit). NEXT LEVER CANDIDATES, in order:
(a) A3d pristine skip — needs the decl-views audit (direct_parse.c
borrowed version/encoding point INTO xml_buffer);
(b) parse-path arena reuse (thread-local spare-span recycle, the
#1846 warm-span idea applied to fresh parse arenas — amortizes the
per-doc mmap/fault churn that dominates large-doc create/free
loops);
(c) ubuntu-x86 scanner codegen (gcc-13; the 1.35-3.7x rows there are
now the widest platform gap).

## Track D addendum 5 — A3d (PR #2005) + AVX2 pre-scan (PR #2006) LANDED

A3d (pristine skip for the newline-free class), macos CI verdicts:
attr 13MB 174.4 -> 503.7 MB/s (+189%), text 13MB 794.2 -> 1366.9
(+72%); 13MB gaps 5.14x -> 2.18x attr, 5.80x -> 1.87x text across
the two cuts (#2003 census + #2005 pristine). The attr-5MB -32% from
addendum 4 was between-run variance (+78% this run). ubuntu ratios
held (that leg itself ran slower; pugixml moved with it).

count3_high's AVX2 kernel (#2006) TRANSFORMED the ubuntu-x86 lane —
the SCALAR sizing pre-scan was the dominant tax there, NOT scanner
codegen: attr 1.5MB 286.5 -> 582.7 MB/s (NOW AHEAD: 0.75x), attr
5MB 1.36x -> 0.93x, attr 13MB 1.36x -> 1.03x (parity), text
1.5/5/13MB 3.41x/3.37x/3.50x -> 1.45x/1.38x/1.69x. Runner variance
caveat noted (pugixml's rows moved too); the deltas are far outside
those bands.

Soundness note for A3d: parse_string frees the caller's buffer by
contract — the skip repoints xml_buffer at the doc-owned SCRATCH
(newline-free: no dp_nul clobber can lose a newline), never the
caller's const buffer. Teeth: NewlineTerminatedNameNeedsPristine
parses `a\n='1'` (whitespace before '=' is legal; an attr NAME can
be newline-terminated) — RED under unconditional skip, GREEN gated.
Two pin drafts discarded for wrong premises (element names are not
NUL'd; `<e a b='1'>` is malformed XML). Also learned: the eager
line-breaks table in direct_parse_internal is OOM-path-only; the
live resolver is node_public.c's lazy table off xml_buffer.

REMAINING (the honest tail): ubuntu-x86 text 1.38-1.69x, attr
0.75-1.03x (parity-to-ahead!), arm text ~1.9-2.1x, macos text 13MB
1.87x — the fused scanner loop is the one lever left, and the
platform picture is now win/parity-adjacent everywhere instead of
2-5x behind.

## Track D addendum 6 — the text-shape profile + the IL-default rejection (PR #2009)

Fresh no-inline text-shape attribution (300k elements, 19MB, classic
lane): direct_parse_internal own body 34% | text_scan_run_neon 17% |
dp_wire_child 9% | memmove (scratch copy) 8.2% | count3_neon 7.9% |
dp_text_create 7.4% | dp_parse_attrs 5% | pos journal 3.3% | edge
encode 3%.

LANDED: the no-attribute fast-out (dp_parse_attrs' 5% on a zero-attr
doc = per-element call + prologue + dup memset).

REJECTED BY MEASUREMENT: flipping LEPTRIS_INTERLEAVED's default on —
the IL lane measured 1.72x SLOWER user-CPU on the element-dense-
with-text 19MB fixture (the record pass + replay outweigh the fused
copy on this shape). The opt-in gate + 64KB threshold stand; the
lane's large-doc wins are shape-specific. (Second finding: the lane
is env-gated OFF in CI rows — every published bench_parse_ab number
is the classic lane.)

THE REMAINING ARC (unchanged): direct_parse_internal's own body —
the per-tag dispatch, name scan, close-tag match, and per-node
bookkeeping spread across wire/pos/edge-create. Instruction-level,
multi-session. The per-node bookkeeping share (wire 9% + text_create
7.4% + pos 3.3% + edge 3% = ~23%) is the largest coherent block;
a layout-level pass (fewer, wider stores per node) is the shape of
the next slice if the campaign continues.

## Track D addendum 6b — the fast-out's CI verdict + the macos-noise proof (PR #2009)

The no-attribute fast-out delivered on the stable legs: ubuntu-x86
attr 5/13MB +31-32% (13MB now PARITY at 0.99x), attr 1.5MB 0.72x
(win deepening), text 1.5/5/13MB +52/53/54% (1.27x/1.16x/1.46x);
ubuntu-arm text rows all up (937->978, 939->984, 872->906).

THE MACOS 13MB ROWS ARE RUNNER NOISE: attr-13MB measured
139.4 -> 174.4 -> 503.7 -> 104.3 MB/s across runs of
near-identical code (the cuts only ADD work-free fast paths) —
a 4.8x swing on one leg. Single-run macos attr/text-13MB datapoints
MUST NOT drive verdicts; the macos lane needs repeated-run medians
before any row there is called a regression or a win.

## Track D addendum 7 — the scanner arc OPENED (2026-11-11 evening)

Post-all-cuts text-shape samples (300k-element fixture, no-inline
build): direct_parse_internal's own body dominates the parse at
34-73% across samples — the per-tag dispatch loop (open scan, close
scan+match, text-run dispatch, elem carve/zero) is THE remaining
arc, exactly as the ledger predicted. The region split needs
inFunction attribution: the sample tool cannot see inside one frame,
so the next session's opening move is the noinline region-wrap probe
(close-tag region first — biggest single suspect; needs the
goto/label surgery threaded through DParser).

INSTRUMENT LAWS learned tonight: (1) `git checkout main -- <file>`
on a worktree branch copies from the STALE LOCAL main — always
`git checkout HEAD --` or origin/<ref>; it silently reverted the
fast-out mid-profile and produced one void sample. (2) The Bash tool
shell layers make `$!`/pgrep -f unreliable for backgrounding a
sample target — pgrep -x with the binary name + the process printing
its own getpid() is the reliable pattern. (3) Sample files written
into the wt/ worktree dirs vanished between commands (sandbox
sweep class) — write samples into the main repo tree and extract in
the same command.

ALSO this session: the interleaved-lane default flip REJECTED
(1.72x slower on element-dense text; addendum 6), and the
no-attribute fast-out SHIPPED (PR #2009, ubuntu-x86 attr-13MB to
parity). Release trains v1.9.416 + v1.9.417 sealed, all registries.

## Track D addendum 8 — the region split MEASURED (probe branch probe/close-region)

The close-tag region extracted NOINLINE (dp_close_tag_probe — the
instrument lives on probe/close-region, DO NOT MERGE) and sampled on
the post-fast-out text shape (~1250 in-frame samples):

- loop residual (open-tag scan + dispatch + elem carve/zero): 21%
- text-run family (scan_run_neon 17% + scalar wrapper 2.2% +
  dp_text_create 8.5% + set_content 1.5%): ~29%
- wiring (dp_wire_child 9% + int32 encode 5.8% + edge_idx 0.7%): ~15%
- CLOSE-TAG REGION (probe 7.2% + pos_close_at 4.4%): 11.6%
- positions journal (pos_record 6.6% + the close_at share above): ~11%
- count3 pre-scan: 5.8%

Next-cut ranking by (size x boundedness):
1. text_scan_run's NEON residual (17% in-kernel — the chunk loop +
   flag handling; an AVX2 mirror also lands for x86).
2. The loop residual's elem carve/zeroing (memset per 64-element
   chunk vs explicit-init stores).
3. The positions journal (12% combined; SKIP_SOURCE_POSITIONS
   already opts out — the default-on cost is the conformance/positions
   faces' price).
The close-tag region (11.6%) has no obvious algorithmic cut — memcmp
fast paths already cover it.

## Track D addendum 8b — the scan-kernel analysis (2026-11-11, ranked cut #1 CLOSED as structural)

The text_scan_run NEON kernel read + analysis: the 17% in-kernel
share for the fixture-class run lengths (30-64B) is dominated by
PER-RUN FIXED COST — the call, the 3 needle vdup setups, the
accumulator zeroing, and one vmaxvq reduction per 16B chunk — not
per-byte throughput. The batched-reduction redesign (8 chunks
reduction-free, then chunk-rescan on hit) only wins runs >128B;
the bench-class runs break even or lose. The cut that actually
moves this block = merging the run scan INTO the caller's loop
(killing the per-run call+setup for short runs entirely), which is
the loop-body surgery slice — it inherits the region-wrap probe
(the text branch wraps the same way the close-tag region did).

RANKED CUT #1 therefore moves to the loop-surgery slice with the
scan-merge as its first step. Nothing ships from the kernel alone.

## Track D addendum 9 — the inline text-scan cut REJECTED by CI (PR #2018, closed)

The ranked-#1 cut (inline 32-byte scalar prefix scan for short text
runs, kernel for the remainder) went through the full gate: round 1
caught the restart-at-run-start flaw (the kernel rescanned the
scalar prefix — every leg regressed), round 2 fixed that (kernel
from the prefix end) and STILL regressed on all three legs vs the
A3d baselines (macos -39-46%, ubuntu-arm -26-27%, ubuntu-x86 -37-45%
vs its AVX2 run). Cause: the bench-class run lengths (33-64B) sit
exactly where the cut loses — 32 branchy scalar bytes plus a kernel
pass over a 1-32B remainder exceeds the kernel alone, and runs
<=32B are too few in these shapes to carry it.

THE MISSING DATUM: the run-length distribution of the real fixtures
and the scoreboard corpus. The next session's opening instrument =
a run-length histogram (one counting pass per fixture, reusable),
which decides whether the scan-merge has a paying window at all —
and at what threshold. Until then the kernel stands unchanged; the
PR closed with the full data (three legs, two rounds, CI artifacts).

## Track D addendum 10 — the scan-merge slice CLOSED (PR #2020, closed unmerged)

The 64-byte window (histogram-driven) went through the full gate and
regressed WORSE than the 32-byte attempt: macos -45/-39/-46%, arm
-32/-32/-29%, x86 -53/-53/-50% (vs the AVX2 run) — even for runs
fully inside the window. THE VERDICT: the branchy scalar loop
(~3-5 ops/byte) loses to the SIMD kernel (~1 op/byte) at every run
length on every platform; the kernel's per-run fixed cost is small
next to its per-byte advantage. The scan-merge slice is structurally
dead — two windows, two full CI runs, one consistent verdict.

THE TEXT-ROW LEVER REDIRECTS to the kernel's own per-byte
throughput: the text_scan_run NEON kernel processes ~1 byte/op
while pugixml's scanner sustains 2.1-2.7 GB/s — the Track A scanner
class. The next concrete candidate: process 32B chunks (NEON
registers ×2) per iteration instead of 16B, halving the reduction
frequency; and the parser's text-branch call overhead already
measures separably (the scalar wrapper at 2.2%).

The histogram instrument (probe/run-histogram) remains in the repo
history on probe/run-histogram; both rejected cut branches carry
their full data.

## Track D addendum 11 — the 32B kernel window measured-NEGATIVE (PR #2022, closed unmerged)

The addendum-10 candidate (NEON text_scan_run at 32B windows — two
uint8x16 registers, term masks OR-combined before ONE vmaxvq, halving
the horizontal-reduction frequency) passed the full CI suite but the
CI split was poisoned by fleet variance (x86 +31% on code the diff
does not touch). The decisive instrument was a same-machine
interleaved A/B on Apple silicon (Release+LTO, 3 rounds ABBA, pugixml
side flat ~2370/1800 MB/s across rounds — machine verified stable):

| shape | 16B (main) | 32B (PR) | delta |
|-------|-----------|----------|-------|
| 1.5MB | ~1341 MB/s | ~1144 MB/s | −15% |
| 5MB   | ~1332 MB/s | ~1135 MB/s | −15% |
| 13MB  | ~1029 MB/s | ~913 MB/s  | −11% |

THE INVARIANT: horizontal-reduction frequency is NOT the binding term
in text_scan_run on M-series. The accumulator chain is — two extra
cmpeq+orr per register per window lengthen the dependency chain and
raise live-register pressure (9+ Q registers) without removing any
real per-byte work. Widening the window cannot pay; the kernel's
per-byte op count is already minimal for its contract (first-term
scan + f1/f2 seen-flags).

TEXT-ROW STATUS after three measured closures (scan-merge 32B,
scan-merge 64B, kernel-width 32B): the kernel is at its floor. The
remaining text-row gap to pugixml is the parser's text-branch call
overhead (the scalar wrapper measures separably at 2.2% — the only
untried lever in this class), not the kernel.

## Track D addendum 12 — the text-branch call-overhead class CLOSED (measured tie + the bound-vs-run datum)

The named remainder from addendum 11 ("the parser's text-branch call
overhead, the scalar wrapper at 2.2%") went through the full
instrumentation pass and closes the class:

1. **The microbench decomposition**: the dispatch entry costs 2-3x the
   kernel's own work per call (7.1 vs 3.0 ns at 5B, 17.9 vs 6.9 ns at
   31B) — the fn-pointer defeats thin-LTO and forces the full call ABI
   plus per-call key re-materialization. A devirtualized cut (arch-
   direct calls + a <16B scalar fast path) was implemented, pinned,
   and mutation-falsified — and measured a DOC-LEVEL TIE in interleaved
   A/B (−0.7/−2.3/−1.4%, inside the noise band). The microbench's
   variant-B "win" was LTO INLINING the local kernel into the bench
   loop — a zero-call comparison the real direct-call cut cannot
   replicate across TUs.

2. **The bound-vs-run instrument datum** (the decisive one): the
   parser's scan calls pass the REMAINING BUFFER as `len`
   (`p.end - p.pos`), not the run length — so a length-gated fast path
   never engages in the parser context (bucket census on the bench
   text fixture: <16B = 0 of 2.1M calls, 64+ = 99.999%). The
   run-length histogram that motivated the fast path describes the
   RUN distribution, which the kernel only knows after scanning.

Disposition after four closures in the call-structure class
(scan-merge 32B/64B, kernel-width 32B, devirtualization): **the
text-branch call structure is at its floor.** The remaining text-row
gap to pugixml is the kernel's per-byte throughput itself — and the
accumulator chain that binds it was measured at addendum 11. Next
text-row work, if any, is a kernel redesign (Track A scanner class),
not call-structure rearrangement.
