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
struct leptris_document {
    // ...
    leptris_allocation_function alloc_hook;
    leptris_deallocation_function dealloc_hook;
};

struct leptris_memory_pool {
    // ...
    leptris_allocation_function alloc_hook;
    leptris_deallocation_function dealloc_hook;
};
```

### Step 2: pool uses its hooks

`leptris_pool_alloc` calls `pool->alloc_hook` instead of the global.
Same for free.  Defaults: NULL → use the global thread-default.

### Step 3: parser creates pool with document's hooks

`leptris_parse` reads `doc->alloc_hook` and passes to
`leptris_pool_create_with_hooks`.

### Step 4: public API

```c
LEPTRIS_API LeptrisStatus leptris_document_set_allocators(
    LeptrisDocument doc,
    leptris_allocation_function alloc,
    leptris_deallocation_function dealloc);
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
