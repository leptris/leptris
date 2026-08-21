# TODO 154 — Single-arena per-parse allocation

## Status

**Phase A+B+C DONE.** Shipped across v0.14.0 (A+B), v0.15.1 (C).

## Why

`leptris_parse_string` does **4 mallocs** per call:

1. `malloc(len + 1)` for the buf copy in `direct_parse`.
2. `leptris_alloc_hook(sizeof(LeptrisMemoryPool))` for the pool struct.
3. `leptris_alloc_hook(page_size + sizeof(MemoryPage))` for the first
   pool page (called from `leptris_pool_create_with_page_size`).
4. `calloc(1, sizeof(struct leptris_document))` for the doc struct.

pugixml does **1 malloc** for the entire document (doc header + first
page in one block). On a 37-byte input this is the single biggest
reason we're 5× slower on tiny docs.

## Plan

### Phase A — Combine pool struct + first page (saves 1 malloc)
- New internal API: `leptris_pool_create_combined(page_size)` returns
  a pool whose `LeptrisMemoryPool` struct and first `MemoryPage` live
  in the same allocation.
- The pool struct goes at offset 0; the page header follows at
  `sizeof(LeptrisMemoryPool)`; the page data follows.
- `leptris_pool_destroy` must know that the first page is not a
  standalone allocation. Add a `first_page_inline` flag to the pool
  struct. When set, the destroy walk skips freeing the first page
  (it's freed via the pool struct free at the end).

### Phase B — Allocate doc struct from the pool (saves 1 malloc)
- `direct_parse_internal` allocates the doc via `leptris_pool_alloc`
  instead of `calloc`.
- Need a `doc_pool_allocated` flag on the doc struct so
  `leptris_document_free` knows whether to `LEPTRIS_FREE(doc)` or
  leave it to be reclaimed by `leptris_pool_destroy`.
- All other doc-allocation sites (`leptris_document_copy`,
  `leptris_parse_fragment`) keep using `calloc` for now — they're not
  hot paths.

### Phase C — Optional: skip the buf copy on the writable fast path
- `leptris_parse_string` could expose an `adopt_buffer` mode where
  the caller's `char*` is used directly (no malloc+memcpy).
- Already exposed as `leptris_parse_string_inplace`. The benchmark
  shows this saves ~100 ns on tiny docs.
- Document the contract: caller must NOT free until doc is freed.

## Risk

- **ABI**: `leptris_document` size grows by 1 byte (flag). Not visible
  to callers (opaque handle).
- **Free ordering**: pool-allocated doc must survive until
  `leptris_pool_destroy` runs in `leptris_document_free`. Already true
  — pool destroy is step 9, doc free is step 10.
- **Threading**: pool-allocated docs are not safe to share across
  threads beyond the existing doc-level threading model. No change.

## Expected impact

Tiny (37 B): 0.41 µs → ~0.30 µs (saves ~3 mallocs × 30 ns each).
Medium (24 KB): ~5 µs saved (less impact, dominated by parse work).

## Status

Pending. Implementation phase A is the first PR.
