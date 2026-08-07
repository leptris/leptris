# TODO 74: Per-document allocator hooks (full implementation)

**Priority**: P2 (architecture — completes thread safety story)
**Status**: Planned — execution
**Effort**: M

## Problem

TODOs 27/38 made allocator hooks `__thread`.  Two documents in the
same thread can't have different allocators.  TODO 48 designed the
solution; this TODO executes it.

## Fix

### Step 1: fields on TaurusMemoryPool

```c
struct taurus_memory_pool {
    // ... existing fields
    taurus_allocation_function  alloc_hook;
    taurus_deallocation_function dealloc_hook;
};
```

`taurus_pool_alloc` calls `pool->alloc_hook ? pool->alloc_hook :
taurus_alloc_hook` instead of always the global.

### Step 2: constructor with hooks

```c
TaurusMemoryPool* taurus_pool_create_with_hooks(
    size_t page_size,
    taurus_allocation_function alloc,
    taurus_deallocation_function dealloc);
```

Defaults: NULL → use thread-default globals.

### Step 3: fields on TaurusDocument + parser uses them

```c
struct taurus_document {
    // ...
    taurus_allocation_function alloc_hook;
    taurus_deallocation_function dealloc_hook;
};
```

`taurus_parse` passes `doc->alloc_hook` to the pool constructor.

### Step 4: public API

```c
TAURUS_API TaurusStatus taurus_document_set_allocators(
    TaurusDocument doc,
    taurus_allocation_function alloc,
    taurus_deallocation_function dealloc);
```

Note: must be called BEFORE parsing; changes after parse have no
effect on already-allocated memory.

## Tests

```cpp
TEST(DocumentAllocators, PerDocumentOverride) {
    static int alloc_count = 0;
    auto my_alloc = [](size_t n) -> void* { alloc_count++; return malloc(n); };
    auto my_free = [](void* p) { free(p); };

    /* Set before parsing — taurus_parse_string_with_options or
     * similar path that lets us pre-create a doc... actually the API
     * doesn't support pre-parsing allocation config.  See TODO 73. */
}
```

Realistically the API needs `taurus_parse_with_options` to honor
`opts->alloc_hook`.  Add that.

## Verification

```bash
ctest --test-dir build --output-on-failure -R Allocators
```
