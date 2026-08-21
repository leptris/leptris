# 09 — Perf campaign: remaining items, all measured floors (context)

The beat-pugixml campaign is at its measured end state. For the
record, what is left and why it is not being chased:

- attr-heavy parse ~1.5x: per-attr WORK floor (sub-scans + feature
  stores), every layout point measured. See 08 for the one
  structural idea.
- pretty-ws parse ~1.1-1.4x default-mode: SEMANTICS (we keep
  whitespace-only text nodes, libxml2-faithful; pugixml discards
  them). LEPTRIS_PARSE_DROP_WS_TEXT is the apples-to-apples mode —
  DOCUMENT this in the README parse table (small task folded into
  the next README pass).
- append ~1.2x: last ~4 ns is create internals (memset + hash) +
  per-call invalidations; feature-priced.
- 1k-append anomaly (round-15 ledger): 1000-append loop measures
  48 ns/append vs 7 ns at 10k scale, reproducible at low load,
  unexplained. Only matters if a user builds small docs in a tight
  loop; investigate with the isolate driver + malloc-zone
  instrumentation before touching element_create.
- README writes-table numbers predate the attr index; refresh
  alongside the DROP_WS note.
