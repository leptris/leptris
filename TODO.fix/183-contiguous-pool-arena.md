# TODO 183 — Contiguous per-document pool arena

**Priority**: P0 (blocks [[180-compact-pointer-phase-c-element]] and
[[181-compact-pointer-phase-d-attributes]])
**Status**: scoped

## Goal

Redesign `TaurusMemoryPool` to allocate one contiguous arena per
document, eliminating the multi-page layout that breaks compact
pointer encodings wider than the page size.

## Why

Discovered 2026-08-14 while attempting TODO 180 Phase C (element
tree migration to cp16). Three tests failed:

- `SerializeRoundTrip.GrowsBufferForHugeTextContent` — silent data loss
- `SerializeRoundTrip.HugeTextContentStaysAttachedToParent` — silent data loss
- `HighDocCountStress.ParseVerifyFree5000Docs` — segfault

**Root cause**: `taurus_pool_alloc` allocates 32 KB pages via `malloc`.
Distinct malloc'd pages for one document can land megabytes apart on
macOS ASLR / Linux glibc. Element tree edges span the whole document,
easily exceeding cp16's ±256 KB range when the document has more than
~500 elements (one pool page worth). Silent truncation corrupts the
tree.

`direct_parse.c` is overflow-table-free by design (line 672, issue
#261) to avoid cross-document contamination under high doc counts.
Re-introducing the overflow table from the parser hot path would
re-open #261.

## pugixml's approach

pugixml's `xml_allocator` allocates pages of 32 KB by default but
**guarantees pages are chained contiguously** via virtual memory
reservation. The whole document lives in one virtual address range;
tree-edge pointers (even 1-byte compact mode) stay within the range.

For our purposes, the simplest analog is **one big malloc per
document**, sized upfront from the document's byte length. Drawback:
must estimate size; if estimate is wrong, must realloc (which may
relocate and invalidate all internal pointers — a hard problem).

## Phases — one PR each

### Phase 1 — Arena allocator core

New file `src/taurus/memory/arena.{h,c}`:

```c
typedef struct taurus_arena {
    char* base;            /* Single contiguous malloc */
    size_t size;           /* Total bytes allocated */
    size_t used;           /* Bump pointer */
    /* Optional growth: list of "extension" arenas for overflow.
     * Each extension is its own malloc; pointers within an extension
     * are valid only within that extension. */
} TaurusArena;
```

Initial size estimate: 2× document byte length + 64 KB (overhead for
node structs etc.). Tune via benchmarks.

### Phase 2 — Pool API compatibility

`TaurusMemoryPool` becomes a thin wrapper around `TaurusArena`,
preserving the existing `taurus_pool_alloc / taurus_pool_destroy`
API. Internal callers see no change.

### Phase 3 — Migrate direct_parse.c

`direct_parse.c` allocates the arena at parse start with the size
estimate. All node/attr/string allocations come from the arena. No
more multi-page allocations within a single document.

### Phase 4 — Oversized-content handling

Text nodes with > arena-remaining content (rare) need special
handling: either fail, or use a side allocation that lives outside
the arena but is tracked for cleanup. pugixml fails parsing on
oversized requests; we likely want the same.

### Phase 5 — Compact-pointer re-enablement

With contiguous arenas, cp16 (±256 KB) covers all elements in any
document up to 256 KB element-pool size. For larger docs, fall back
to int32 within the same accessor (hybrid mode). Phase C and Phase D
compact pointer migrations then work as designed.

## Estimated impact

Unblocks Phase C (1.5–2× on tree walks) and Phase D (2–3× on K=100).
Indirectly enables Phase E (compact_string).

Direct perf impact: minimal — pool allocation cost is already dominated
by the actual allocation, not the bookkeeping.

## Risk

Medium-high. Changing the pool allocator touches every allocation
site. Mitigations:

1. Phase 2 preserves the `TaurusMemoryPool` API — internal callers
   see no change.
2. ASAN + UBSAN + leak check must pass on all 474 tests.
3. Adversarial inputs (huge text content, 100K-element docs) must work.
4. Fuzz for 10M iterations before merge.

## References

- Unblocks: [[180-compact-pointer-phase-c-element]], [[181-compact-pointer-phase-d-attributes]]
- Background: [[149-pugixml-architecture-study]], [[154-single-arena-per-parse-allocation]]
- Related issue: #261 (cross-document contamination)
