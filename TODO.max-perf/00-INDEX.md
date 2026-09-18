# TODO.max-perf — remaining-work ledger, lowest-hanging first

Rank = impact ÷ effort. Work top-down; one lever per PR with a
RED bench first where perf claims are made.

| # | Lever | Type | Effort | Status |
|---|-------|------|--------|--------|
| 3 | iterparse fixed-cost (6.9µs) + error channel (#563) | perf | bench landed #1195; remaining = zero-copy one-shot emission (design with lever 2) | half done |
| 1 | SIMD/memchr HTML tokenizer scans | perf | memchr ×2 (#1176/#1190) + LUT (#1199, +30%/+51%) | DONE v1.9.199 |
| 7 | leptris validate CLI + bindings | feature | engine half merged #1198 (--dtd + DTD engine fixes); bindings remain | half done |
| 8 | native XML diff consumer surface | feature | small | next |
| 4 | compact-node micro-opts (non-layout) | perf | audited: TODOs 172/195/Lane18-P3 already in; residual = node-creation cost (node-layout fork, owner call) | parked on fork |
| 2 | compiled-dispatch tokenizer | perf | medium; design together with lever 3's zero-copy emission | queued |
| 5 | XSLT 2x per-bench profile rounds (#682) | perf | medium, ongoing | in progress |
| 6 | language tails (QT3/dates/fn-items/lane 15) | correctness | medium-large | queued |
| 9 | prebuilt-gem 12-platform matrix | release | small, BLOCKED on owner go | blocked |
| 10 | moxml Plan follow-ups (upstream) | upstream | small | asks pending |

Portability wave 2026-09-18 (v1.9.199): ILP32 layout port #1193 —
per-wordsize pins, SSE2 gate, xsl:number 64-bit chain, ILP32 +
native-arm64 + Intel-macOS CI legs, concurrency groups everywhere.
Next portability gate: s390x big-endian (#1194, PR #1202).
