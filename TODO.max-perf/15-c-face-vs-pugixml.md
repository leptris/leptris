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
