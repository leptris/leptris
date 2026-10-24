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
- [ ] **A3** lazy pristine copy. `doc->xml_buffer` is load-bearing
      for lazy LINE resolution; the pristine copy exists because
      `dp_nul` may overwrite a `\n` token terminator. Audit which
      terminators can be `\n`; either protect those or build the
      line table from the scratch. Saves ~2.5%.
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

- [ ] **C1** profile the three serialbench queries on the parsed DOM
      (compile once + eval per iteration vs the harness's exact
      call shape; pins whether compile or eval dominates).
- [ ] **C2** levers from C1: expected candidates — axis step
      dispatch (indirect calls), node-set assembly allocs per step,
      the doc-order rank table, predicate evaluation allocs.
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
