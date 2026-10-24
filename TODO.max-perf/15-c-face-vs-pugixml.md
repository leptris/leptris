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
- [x] **A2** fused sizing+encoding pre-scan — PR #1848 MERGED
      (three commits: the fused kernel, the declared-encoding EXEMPT
      sense fix — dp_declares_non_utf8==1 means skip validation, not
      reject; iconv builds hide it, the no-iconv CI lanes are the
      validators — and the dispatch_init full-init TSan fix
      (x86 leaves the slot NULL by design; the lazy re-dispatch
      raced). RED-first unit tests; 2109/2109; 26/26.
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
- [x] **C2** offset fast path — PR #1847: unmutated documents
      (mutation_version == 0) rank by `(base.line << 2 | bias)`;
      mutated/unstamped take the table path unchanged. The hidden
      consumer from the first attempt was the TABLE PATH ITSELF:
      its post-sort adjacent-pointer compaction is load-bearing
      (multi-context steps shed cross-context duplicates through
      it), and the sort's merge must write into tmp — an in-place
      merge clobbers unread idx[a] sources (the k==a case). doc
      node ranks to the outside-the-tree tail exactly like the
      table's INT64_MAX miss. Suite 2105/2105; doc_rank_table_put
      gone from the exact-ops profile. The NEXT non-parse cost in
      the row is the TODO-190 element-index build (axis_query_count
      >= 2 builds it even for 3-query-fresh-doc workloads where it
      never amortizes — a tuning lever, not a bug).
      unmutated parse-created documents (mutation_version == 0)
      rank by parse-time byte offset `(base.line << 2 | kind_bias)`
      — byte offsets ARE XPath document order (attrs sit inside the
      start tag; ns/attr/xpath-text biases 1/2/3 on the owner's
      offset). No walk, no hash. A first implementation reproduced
      EXACTLY (doc node must rank to the outside-the-tree tail like
      the table path) but 8 XPathResults/SubtreeIndex/Ledger tests
      still failed: the eval ITSELF produced different node sets
      when the sort skipped the table build — some step-dedup or
      ordering consumer depends on the table's side effect
      (doc_order_index population), not just the ranks. The
      reproduce-in-one-file probe: parse kSubtree (test_xpath.cpp),
      eval //item then //section//item — fast mode duplicates the
      nested item 5x. NEXT SESSION: find the consumer (candidates:
      the vm's descendant chain dedup assumptions, or a hidden
      doc_order_index reader behind a macro), or gate the fast path
      to the FINAL result sort only.
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
