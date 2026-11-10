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
