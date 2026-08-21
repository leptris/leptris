# TODO 74: Per-document allocator hooks (full implementation)

**Priority**: P2 (architecture — completes thread safety story)
**Status**: Planned — execution
**Effort**: M

## Problem

TODOs 27/38 made allocator hooks `__thread`.  Two documents in the
same thread can't have different allocators.  TODO 48 designed the
solution; this TODO executes it.

## Fix

### Step 1: fields on LeptrisMemoryPool

```c
struct leptris_memory_pool {
    // ... existing fields
    leptris_allocation_function  alloc_hook;
    leptris_deallocation_function dealloc_hook;
};
```

`leptris_pool_alloc` calls `pool->alloc_hook ? pool->alloc_hook :
leptris_alloc_hook` instead of always the global.

### Step 2: constructor with hooks

```c
LeptrisMemoryPool* leptris_pool_create_with_hooks(
    size_t page_size,
    leptris_allocation_function alloc,
    leptris_deallocation_function dealloc);
```

Defaults: NULL → use thread-default globals.

### Step 3: fields on LeptrisDocument + parser uses them

```c
struct leptris_document {
    // ...
    leptris_allocation_function alloc_hook;
    leptris_deallocation_function dealloc_hook;
};
```

`leptris_parse` passes `doc->alloc_hook` to the pool constructor.

### Step 4: public API

```c
LEPTRIS_API LeptrisStatus leptris_document_set_allocators(
    LeptrisDocument doc,
    leptris_allocation_function alloc,
    leptris_deallocation_function dealloc);
```

Note: must be called BEFORE parsing; changes after parse have no
effect on already-allocated memory.

## Tests

```cpp
TEST(DocumentAllocators, PerDocumentOverride) {
    static int alloc_count = 0;
    auto my_alloc = [](size_t n) -> void* { alloc_count++; return malloc(n); };
    auto my_free = [](void* p) { free(p); };

    /* Set before parsing — leptris_parse_string_with_options or
     * similar path that lets us pre-create a doc... actually the API
     * doesn't support pre-parsing allocation config.  See TODO 73. */
}
```

Realistically the API needs `leptris_parse_with_options` to honor
`opts->alloc_hook`.  Add that.

## Verification

```bash
ctest --test-dir build --output-on-failure -R Allocators
```
