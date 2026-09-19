# TODO.max-perf — remaining-work ledger, lowest-hanging first

Rank = impact ÷ effort. Work top-down; one lever per PR with a
RED bench first where perf claims are made.

| # | Lever | Type | Effort | Status |
|---|-------|------|--------|--------|
| 3 | iterparse fixed-cost (6.9µs) + error channel (#563) | perf | bench #1195; zero-copy emission landed #1236 (295->52us full-doc); remaining = iterparse per-event DOM construction (0.45->0.15us/event stretch) | mostly done |
| 1 | SIMD/memchr HTML tokenizer scans | perf | memchr ×2 (#1176/#1190) + LUT (#1199, +30%/+51%) | DONE v1.9.199 |
| 7 | leptris validate CLI + bindings | feature | engine half merged #1198 (--dtd + DTD engine fixes); bindings remain | half done |
| 8 | native XML diff consumer surface | feature | CLI diff text/JSON/--summary + engine API shipped | DONE (#1184 closed) |
| 4 | compact-node micro-opts (non-layout) | perf | audited: TODOs 172/195/Lane18-P3 already in; residual = node-creation cost (node-layout fork, owner call) | parked on fork |
| 2 | compiled-dispatch tokenizer | perf | slices 1+3 landed #1236; slice 2's class LUT + dense switch pre-existed; residual = iterparse consumer (row 3) | mostly done |
| 5 | XSLT 2x per-bench profile rounds (#682) | perf | medium, ongoing | in progress |
| 6 | language tails (QT3/dates/fn-items/lane 15) | correctness | medium-large | queued |
| 9 | prebuilt-gem 12-platform matrix | release | small, BLOCKED on owner go | blocked |
| 10 | moxml Plan follow-ups (upstream) | upstream | small | asks pending |

Portability wave 2026-09-18 (v1.9.199): ILP32 layout port #1193 —
per-wordsize pins, SSE2 gate, xsl:number 64-bit chain, ILP32 +
native-arm64 + Intel-macOS CI legs, concurrency groups everywhere.
Next portability gate: s390x big-endian (#1194, PR #1202).
