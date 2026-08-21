# TODO 108 — remaining-work survey (post-29-PR session)

**Date**: 2026-08-05
**State**: main is clean, 221 ctest pass, Release+LTO default

## What was accomplished this session (29 PRs)

### Memory safety (P0) — 4 bugs fixed
- leptris_element_text ownership leak (6+ call sites)
- Public API enum leak (LEPTRIS_ERROR_MEMORY_ALLOCATION → LEPTRIS_ERROR_MEMORY)
- leptris_element_create_pooled name-copy (heap-use-after-free)
- Benchmark LEN overread (855-2298 bytes past buffer)

### Architecture — 7 cleanups
- Public type dedup (leptris.h → types.h delegation)
- strict_mode cached in Parser struct
- Freeze contract clarified (advisory, not enforced)
- Child-cache invalidation encapsulated (leptris_element_invalidate_child_cache)
- Element field reads routed through accessors (TODO 90 Phase 1)
- Duplicate leptris_element_create_pooled declaration removed
- Serializer emits document-level PIs

### Performance — 10 PRs
- SAX scratch arena + vectorized scans (2.6-3.5x cumulative)
- Chartype lookup table (pugixml trick #1)
- Name-scan 4-way unroll (pugixml trick #2)
- LTO option + default-on for Release (1.3-3.5x on top of -O3)
- O(1) indexed child access (children_array cache)
- Write-path: tail pointer + no-intern + strlen hoist

### Specs — +47 new
- DOM mutation (9), text accessors (5), SAX edge cases (11)
- Serialize (9), encoding (9), C14N (7)
- Perf regression (3)

### Benchmark infrastructure
- Harness: CPU + RSS + throughput (wall + CPU time, peak RSS)
- CI runs every bench binary, uploads artifacts
- Write benchmark vs pugixml + libxml2
- XPath benchmark vs pugixml
- Benchmark LEN bug fixed (real numbers now)

### Features
- XInclude parse="xml" with cross-document deep copy
- DTD validator Phases 1-7 + #FIXED (shipped in earlier session)

## Competitive position (Release + LTO)

### vs libxml2 — leptris dominates

| Benchmark | Leptris | libxml2 | Advantage |
|---|---|---|---|
| SAX small | 2.83 µs | 7.07 µs | 2.5x faster |
| SAX medium | 7.54 µs | 26.87 µs | 3.6x faster |
| DOM parse | 33 µs | 47 µs | 1.42x faster |
| DOM attr access | 1.57 µs | 2.98 µs | 1.90x faster |
| Append 1000 children | 14.96 µs | 56.20 µs | 3.75x faster |
| Parse medium + writes | 30.73 µs | 41.64 µs | 1.36x faster |

### vs pugixml — gap narrowed

| Benchmark | Leptris | pugixml | Gap |
|---|---|---|---|
| Append 1000 children | 14.96 µs | 11.94 µs | 1.25x |
| Set text | 0.90 µs | 0.68 µs | 1.3x |
| Set 100 attrs | 43.29 µs | 10.35 µs | 4.2x |

## What's left (all priorities)

### P0 — compact architecture migration (TODO 90)

The single highest-leverage remaining work. Infrastructure exists
at `dom/compact.h` (340 lines, fully designed). 5-phase plan:

- **Phase 1** (partially done): accessor audit
  - Cache invalidation encapsulated (PR #78)
  - Serializer/C14N/node_public reads migrated (PR #79)
  - Remaining: element_modify.c field writes (blocked on type
    mismatch — setters take LeptrisElement but children list is
    mixed-type LeptrisNode*)

- **Phase 2** (3-5 days): dual representation (compact + regular)
- **Phase 3** (3-5 days): parser builds compact
- **Phase 4** (1 day): default compact
- **Phase 5** (1 day): remove old representation

Target: 104-byte elements → 23-byte (4.5x smaller).

### P1 — set-attrs gap (TODO 106)

4.2x slower than pugixml on set_attribute. Root cause: O(N)
existence check per call. Options:
- Add `leptris_element_append_attribute` (no existence check)
- Per-element attribute hash table
- Both

### P2 — DTD validator Phase 8 (TODO 91)

ENTITY/ENTITIES attribute resolution, parameter entities,
choice-model backtracking. Infrastructure exists (entity parsing
shipped). Estimated 1-2 weeks.

### P2 — XInclude Phase 4 (TODO 92)

xpointer fragment selection. parse="text" and parse="xml" both
work. xpointer requires XPath engine integration. Estimated 1 week.

### P2 — XPath conformance suite (TODO 69)

W3C XPath test suite harness. leptris passes 438/438 against the
existing suite; the W3C suite would provide broader coverage.
Estimated 2-3 days.

### P3 — FFI bindings (TODOs 79-85)

Ruby, Python, Rust bindings. Each needs its own repo.
docs/FFI.md documents the contract. Language-specific work.

### P3 — Incremental SAX (TODO 89)

leptris_sax_parser_feed is one-shot. True incremental parsing
requires resumable state machine. Estimated 1 week.

### P3 — More spec coverage (TODO 104)

vtable (8 specs), abi (6 specs) — thin but functional.
Priority: every public API has at least one spec.

### P3 — Multi-file benchmark workload

Not yet benchmarked. Most benches parse one file. A "parse N
small files in sequence" bench would catch churn/workflow issues.

## How leptris improves on pugixml (once compact lands)

1. **children_array cache** — O(1) indexed child access (pugixml is O(N))
2. **last_attribute tail pointer** — O(1) attribute append
3. **Pool string interning** — deduplicates recurring attr names
4. **Lazy StringView reads** — already 1.9x faster on attr access
5. **Full DTD validator** — pugixml doesn't have one
6. **XInclude support** — pugixml doesn't have one
7. **SAX parser** — pugixml doesn't have one
