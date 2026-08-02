# TODO 48: Per-document allocator hooks (TODO 38 phase 3)

**Priority**: P2 (architecture — full thread-safety story)
**Status**: Planned
**Effort**: M

## Problem

TODO 38 (phase 2) added per-document strict mode.  Allocator hooks
remain thread-default (`__thread`).  Two documents in the same thread
can't have different allocators.

For most users this is fine.  But for plugin systems where one
document might use a tracking allocator and another a slab allocator,
the lack of per-document hooks is a real constraint.

## Fix

### Step 1: add fields to TaurusDocument

```c
struct taurus_document {
    // ... existing fields
    taurus_allocation_function  alloc_hook;
    taurus_deallocation_function dealloc_hook;
};
```

### Step 2: pool carries the hooks

The pool needs to know which hooks to use.  Add fields to
`struct taurus_memory_pool`:

```c
taurus_allocation_function  alloc_hook;
taurus_deallocation_function dealloc_hook;
```

`taurus_pool_alloc` calls `pool->alloc_hook` instead of the global.
Same for free.

### Step 3: parser creates pool with document's hooks

`taurus_parse` does:

```c
TaurusMemoryPool* pool = taurus_pool_create_with_hooks(
    page_size, doc->alloc_hook, doc->dealloc_hook);
```

Defaults: if hooks are NULL, use the thread-default globals.

### Step 4: public API

```c
TAURUS_API TaurusStatus taurus_document_set_allocators(
    TaurusDocument doc,
    taurus_allocation_function alloc,
    taurus_deallocation_function dealloc);
```

Note: must be called BEFORE parsing.  Changing after parsing has no
effect on already-allocated memory.

## Tests

```cpp
TEST(DocumentAllocators, PerDocumentOverride) {
    /* Set up a counting allocator. */
    static int alloc_count = 0;
    static int free_count = 0;
    auto my_alloc = [](size_t n) -> void* { alloc_count++; return malloc(n); };
    auto my_free = [](void* p) { free_count++; free(p); };

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string("<r/>", 4, &st);
    ASSERT_NE(doc, nullptr);

    EXPECT_EQ(taurus_document_set_allocators(doc, my_alloc, my_free),
              TAURUS_OK);
    /* Subsequent parses or string ops on this document use my_alloc. */

    taurus_document_free(doc);
    EXPECT_GT(alloc_count, 0);
}
```

## Architecture notes

The allocator hooks move from process-global → thread-local → document-
scoped.  Each step narrows the scope:

- Process: shared across all documents in all threads.
- Thread: shared across all documents in one thread.
- Document: per-document, fully isolated.

This is the C analog of "instance variable" vs "class variable".

## Verification

```bash
build/test/parser/test_parser --gtest_filter='DocumentAllocators.*'
# All pass.  No cross-contamination between documents.
```
