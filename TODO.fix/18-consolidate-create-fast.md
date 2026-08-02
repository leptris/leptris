# TODO 18: Consolidate `_create` and `_create_fast` per node type

**Priority**: P2 (DRY / maintainability)
**Status**: Planned
**Effort**: M

## Problem

Every node type exposes two creation functions:

```c
// text.h
TaurusTextNode* taurus_text_create(const char* content, TaurusMemoryPool* pool);
TaurusTextNode* taurus_text_create_fast(const char* content,
                                        size_t content_len,
                                        TaurusMemoryPool* pool);
```

Same pattern for `comment`, `cdata`, `pi`. Historically `_create` was
calloc-backed and `_create_fast` was pool-backed. After TODO 05,
**both** route through the pool — the only remaining difference is
that `_fast` does a single bulk allocation (struct + content
contiguous) while `_create` does two pool allocations (struct, then
strdup'd content).

Two functions doing the same thing is a DRY violation. Worse, callers
have to choose, and the choice has no observable consequence — both
end up in the pool.

## Root cause

The `_fast` variants were an optimization introduced before pool
allocation was universal. After TODO 05, the regular `_create` is
also pool-backed, so the only remaining win from `_fast` is the
contiguous allocation (one pool bump instead of two). That's a real
but small perf delta.

## Fix

### Strategy

Make `_create` *be* the fast path. Take a `content_len` parameter
(use `strlen` at call sites that don't have it). Single allocation:
struct + content contiguous.

```c
// text.h
TaurusTextNode* taurus_text_create(const char* content,
                                   size_t content_len,
                                   TaurusMemoryPool* pool);
```

Delete `taurus_text_create_fast`. Update every caller to pass `len`.
The parser already has length-bounded views; passing the length is
trivial.

Same for `comment`, `cdata`, `pi`.

For `pi`, the `_fast` variant takes `target_len` and `data_len`
separately. Keep that — the consolidated signature takes both lengths.

### Migration

For callers that only have a null-terminated string (e.g., the public
API `taurus_element_set_text(elem, text)` in `element_modify.c`),
compute `strlen(text)` at the boundary. This is one strlen per call,
negligible.

## Tests

Existing specs cover both paths transitively. Add a regression spec
in `test/dom/test_dom.cpp`:

```cpp
TEST(DomNodeCreation, TextCreateUsesContiguousAllocation) {
    TaurusMemoryPool* pool = taurus_pool_create();
    auto* t = taurus_text_create("hello", 5, pool);
    ASSERT_NE(t, nullptr);
    EXPECT_STREQ(t->content, "hello");
    // Contiguous: content lives immediately after the struct.
    EXPECT_EQ((char*)t + sizeof(TaurusTextNode), (char*)t->content);
    taurus_pool_destroy(pool);
}
```

## Architecture notes

DRY: one creation path per node type. The "fast/slow" distinction was
an artifact of the calloc era; pool allocation obviates it.

The contiguous-allocation trick (`struct + content` in one bump) is a
cache-locality win for free — the content lives in the same cache line
as the struct that points to it. Worth keeping; that's why the
consolidated signature takes a length (so the bulk allocation can be
sized correctly).

## Verification

```bash
grep -rn "_create_fast" src/taurus/    # zero hits after migration
cmake --build build                    # clean
ctest --test-dir build                 # 100% pass
```
