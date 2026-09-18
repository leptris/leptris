# TODO.max-perf — remaining-work ledger, lowest-hanging first

Rank = impact ÷ effort. Work top-down; one lever per PR with a
RED bench first where perf claims are made.

| # | Lever | Type | Effort | Status |
|---|-------|------|--------|--------|
| 3 | iterparse fixed-cost (6.9µs) + error channel (#563) | perf | small — closed scope, bench exists | NEXT |
| 1 | SIMD/memchr HTML tokenizer scans | perf | small-medium — dispatch hooks exist | NEXT |
| 7 | leptris validate CLI + bindings | feature | small | next |
| 8 | native XML diff consumer surface | feature | small | next |
| 4 | compact-node micro-opts (non-layout) | perf | medium | next |
| 2 | compiled-dispatch tokenizer | perf | medium | queued |
| 5 | XSLT 2x per-bench profile rounds (#682) | perf | medium, ongoing | in progress |
| 6 | language tails (QT3/dates/fn-items/lane 15) | correctness | medium-large | queued |
| 9 | prebuilt-gem 12-platform matrix | release | small, BLOCKED on owner go | blocked |
| 10 | moxml Plan follow-ups (upstream) | upstream | small | asks pending |
