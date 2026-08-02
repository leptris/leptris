# TODO 49: Pool-route serializer buffer

**Priority**: P2 (correctness + performance)
**Status**: Planned
**Effort**: S

## Problem

`src/taurus/serialize/serialize.c::buffer_create()` allocates the
`SerializeBuffer` struct and its initial 1 KB data buffer via
`TAURUS_ALLOC` (calloc).  These allocations are freed in `buffer_free`.

For a single serialize call this is fine.  But:

1. **Each serialize allocates ~1 KB even if the output is tiny.**  Most
   documents serialize to <1 KB; calloc + free is overhead.
2. **The calloc/free pair bypasses ASAN leak tracking** — false
   positives if the buffer is freed via a different code path.
3. **Mixed ownership**: the document is pool-owned, the buffer is
   calloc'd.  Confusing for readers.

## Fix

When the caller has a document (via `taurus_document_serialize`), pass
the pool through to `buffer_create`.  Allocate from the pool.

```c
SerializeBuffer* buffer_create_pooled(TaurusMemoryPool* pool, int indent_spaces);
```

The buffer is then freed when the pool is destroyed.  The public API
still returns a `char*` that the caller frees via `taurus_free_string`
— that part doesn't change.

For the element-only serialize path (`taurus_element_serialize`), the
caller may not have a document reference.  Fall back to calloc.

## Tests

No behavioral change.  Existing serializer specs cover correctness.

## Architecture notes

After this fix, the entire serialize path is pool-owned when called
via `taurus_document_serialize`.  That's MECE: one allocation source.

## Verification

```bash
leaks --atExit -- build/cli/taurus format basic.xml
# 0 leaks.  (Was 0 before; this TODO doesn't fix a leak, just aligns
# the ownership model.)
```
