# TODO 64: Per-document allocator hooks

**Priority**: P2 (architecture — completes thread-safety story)
**Status**: Planned
**Effort**: M

## Problem

TODO 27/38 made allocator hooks `__thread`.  Two documents in the
same thread can't have different allocators.

## Fix

### Step 1: fields on document + pool

```c
struct taurus_document {
    // ...
    taurus_allocation_function alloc_hook;
    taurus_deallocation_function dealloc_hook;
};

struct taurus_memory_pool {
    // ...
    taurus_allocation_function alloc_hook;
    taurus_deallocation_function dealloc_hook;
};
```

### Step 2: pool uses its hooks

`taurus_pool_alloc` calls `pool->alloc_hook` instead of the global.
Same for free.  Defaults: NULL → use the global thread-default.

### Step 3: parser creates pool with document's hooks

`taurus_parse` reads `doc->alloc_hook` and passes to
`taurus_pool_create_with_hooks`.

### Step 4: public API

```c
TAURUS_API TaurusStatus taurus_document_set_allocators(
    TaurusDocument doc,
    taurus_allocation_function alloc,
    taurus_deallocation_function dealloc);
```

Must be called BEFORE parsing; changes after parse have no effect.

## Tests

```cpp
TEST(DocumentAllocators, PerDocumentOverride) {
    /* Counting allocator; verify it's used for this document only. */
}
```

## Verification

```bash
build/test/parser/test_parser --gtest_filter='DocumentAllocators.*'
```
