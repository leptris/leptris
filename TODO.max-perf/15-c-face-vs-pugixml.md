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
      not the sort — fold into A4's scanner arc or profile the axis
      walks separately before touching this again.
- [ ] **C3** mingw/Windows parity investigation: we are AT parity
      there — diff the codegen assumptions (branch-heavy loops?
      LTO behavior on gcc-win vs clang-mac?) and port whatever
      wins back to the other legs.

## Laws

- CI benchmark legs (quiet runners) gate every number; local A/B
  under a loaded machine is noise.
- Profile with a separate `-DLEPTRIS_ENABLE_LTO=OFF` build dir; LTO
  flattens samples into one frame.
- #1218: arena estimates only ever grow.

Refs #1842, serialbench-c#8.
