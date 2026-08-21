# TODO 140 — Beat pugixml on parse + first access

## Current state

Per `benchmark_parse` (Apple M1):

| Doc size          | Leptris  | pugixml | Ratio   |
|-------------------|---------|---------|---------|
| Small (~1 KB)     | 18.1 µs | 0.6 µs  | 30× slower |
| Medium (~10 KB)   | 233.7 µs| 40.4 µs | 5.8× slower |

Per the flat fast path benchmark (`bench_flat_parse`):

| Operation                  | Time    |
|----------------------------|---------|
| Parse only (flat)          | 12 µs   |
| Parse + lazy promote       | 25 µs   |
| `count(//name)` flat fast  | 12 µs   |

## Root cause analysis

pugixml's document representation IS a flat buffer with 32-byte node
records. Traversal, XPath, and mutation all operate directly on the
flat buffer — no "promote" step exists.

Leptris has two representations:
- FlatDoc (28 B + 12 B) — fast to build, used for parse-only paths
- Compact-pointer tree (96 B elements) — fast for XPath, requires promote

The promote pass costs the same as legacy parse (~1.5 µs per element).
For workloads that access the tree, this doubles the effective parse
cost vs pugixml.

## Strategy: attack the promote cost

Three concrete optimizations, layered:

### Phase A — Bulk pool allocation (TODO 141)

Replace per-element `leptris_pool_alloc` in flat_promote with a single
bulk allocation. Allocate a contiguous block of N elements, slice it
into individual element pointers. Saves ~50% of promote time.

Expected: promote cost drops from 1.5 µs/elem to 0.7 µs/elem.

### Phase B — Skip lazy document pointer propagation (TODO 142)

Currently promote sets `elem->document = doc` on every element. Walk
the tree ONCE after promote to set the document pointer, instead of
per-element during creation.

Expected: minor — saves ~50 ns per element.

### Phase C — Direct flat-mode traversal for read-only paths (TODO 143)

Make the XPath VM's wildcard descendant and named-element queries
use the FlatDoc directly when the document hasn't been mutated.
Mutation triggers promote; reads stay on the flat path.

This is the big win — makes the common read-only XPath workload
match `count(//name)` flat fast path (12 µs) instead of promote
(78 µs at 5 KB).

Expected: read-only XPath queries on unpromoted docs hit ~12 µs
regardless of size, matching pugixml parity.

### Phase D — SIMD-accelerated flat parser (TODO 144)

Use SIMD for the element-name scan loop. The current ASCII tight
loop processes 1 byte/cycle; SIMD can do 16-32 bytes/cycle.

Expected: parse cost drops 4-8×.

## Combined target

| Operation                  | Current | Target | pugixml |
|----------------------------|---------|--------|---------|
| Parse only                 | 12 µs   | 3 µs   | 0.6 µs  |
| Parse + promote (Phase A+B)| 25 µs   | 10 µs  | 0.6 µs  |
| Parse + read-only XPath    | 100 µs  | 5 µs   | TBD     |
| Parse + mutation           | 25 µs   | 10 µs  | TBD     |

We won't beat pugixml on raw parse (their parser is hand-tuned C++),
but we'll be within 5-10× and much faster on every other axis.

## Implementation order

1. Phase A — bulk alloc (1 PR, low risk)
2. Phase B — document pointer (1 PR, low risk)
3. Phase C — flat-mode XPath (1 PR, larger surface)
4. Phase D — SIMD parser (1 PR, performance only)

Phases A+B can ship together. Phase C unlocks the biggest perf win
but requires careful XPath VM changes. Phase D is incremental.
