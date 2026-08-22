# 09 — Perf campaign: remaining items, all measured floors (context)

CLOSED (2026-08-22). Every item resolved or investigated to ground:

- attr-heavy parse ~1.5x: per-attr WORK floor (sub-scans + feature
  stores), every layout point measured. See 08 for the closed
  split-stream question.
- pretty-ws parse ~1.1-1.4x default-mode: SEMANTICS (we keep
  whitespace-only text nodes, libxml2-faithful; pugixml discards
  them). LEPTRIS_PARSE_DROP_WS_TEXT is the apples-to-apples mode —
  documented in the README performance section (parse-modes note).
- append ~1.2x: last ~4 ns is create internals (memset + hash) +
  per-call invalidations; feature-priced.
- 1k-append anomaly (round-15 ledger): INVESTIGATED 2026-08-22 with
  the isolate driver (9 scales x 3 rounds in-process, plus repeated
  fresh-process runs). VERDICT: measurement artifact, no bug. The
  "48 ns at 1k vs 7 ns at 10k" compared a COLD first-document run
  (fresh arena, cold caches: 64-110 ns at EVERY scale) against a
  WARM later-document run (retained arena reuse: 20-30 ns flat).
  Within any single round, ns/append is flat from 100 to 16000
  (occasional +-30% single-scale spikes match the known preemption
  noise class). element_create is not scale-sensitive; the retained
  arena (TODO 183) is simply ~3x faster on reuse than on first use,
  by design.
- README writes-table numbers: refreshed 2026-08-22 from a fresh
  benchmark-matrix run (PR #480) — append-10k now honestly recorded
  as 1.3x slower than pugixml (the doc-level attr index is the
  price of O(1) duplicate-rejecting set_attribute at scale);
  set_attr ~25x faster than find-then-set; serialize splits
  attr-heavy (1.6x faster) / text-heavy (parity).
