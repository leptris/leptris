# TODO 06: Fix pool oversized-allocation leak

**Priority**: P1 (correctness)
**Status**: Planned
**Effort**: S

## Problem

`taurus_pool_alloc` (`src/taurus/memory/pool.c:175-192`) has a fallback
path for oversized allocations:

```c
if (size > page_size) {
    /* Allocation too large for standard page */
    return taurus_alloc_hook(size);            // ← untracked!
}
```

The returned pointer is never recorded anywhere, so
`taurus_pool_destroy()` — which only walks the `first_page` linked list
— cannot free it.

Confirmed by validation:

```
$ leaks --atExit -- taurus parse bigattr.xml   # 10 KB attribute
Process 81130: 2 leaks for 16912 total leaked bytes.
```

`taurus_pool_alloc_batch` (pool.c:231-234) has the same bug.

## Root cause

The pool was modeled as "bump allocator over fixed pages." When an
allocation doesn't fit, the design said "fall back to malloc." But the
ownership contract — "the pool owns everything allocated from it" — was
silently broken by that fallback.

## Fix

Track oversized allocations in a side list. `taurus_pool_destroy` walks
both lists.

### `src/taurus/memory/pool.h`

```c
typedef struct taurus_big_alloc {
    struct taurus_big_alloc* next;
    void* ptr;                 // The oversized allocation
    size_t size;
} TaurusBigAlloc;

struct taurus_memory_pool {
    MemoryPage*       first_page;
    MemoryPage*       current_page;
    TaurusBigAlloc*   first_big_alloc;     // NEW
    TaurusBigAlloc**  last_big_alloc_link; // NEW — O(1) append
    // ... existing fields
};
```

### `src/taurus/memory/pool.c`

```c
static void* pool_alloc_oversized(TaurusMemoryPool* pool, size_t size) {
    void* ptr = taurus_alloc_hook(size);
    if (!ptr) return NULL;

    // The tracking node itself is small; allocate it from the pool
    // (it fits in a page) so we don't recurse.
    TaurusBigAlloc* node =
        (TaurusBigAlloc*)taurus_pool_alloc(pool, sizeof(TaurusBigAlloc));
    if (!node) {
        taurus_free_hook(ptr);
        return NULL;
    }
    node->ptr  = ptr;
    node->size = size;
    node->next = NULL;
    *pool->last_big_alloc_link = node;
    pool->last_big_alloc_link  = &node->next;
    return ptr;
}
```

In `taurus_pool_alloc` and `taurus_pool_alloc_batch`, replace
`return taurus_alloc_hook(size);` with `return pool_alloc_oversized(pool, size);`.

In `taurus_pool_destroy`:

```c
TaurusBigAlloc* big = pool->first_big_alloc;
while (big) {
    TaurusBigAlloc* next = big->next;
    taurus_free_hook(big->ptr);
    // big itself is pool-allocated; freed with the pages below.
    big = next;
}

// Then walk pages as before.
```

## Tests

`test/memory/test_pool.cpp`:

```cpp
TEST(TaurusMemoryPool, OversizedAllocIsTrackedAndFreed) {
    TaurusMemoryPool* pool = taurus_pool_create_with_page_size(4096);
    ASSERT_NE(pool, nullptr);

    // Allocation larger than the page size (10 KB).
    void* big = taurus_pool_alloc(pool, 10000);
    ASSERT_NE(big, nullptr);

    // Allocated from the side list, not from a page (would have failed).
    EXPECT_EQ(taurus_pool_total_size(pool), ...);   // see TODO 10

    taurus_pool_destroy(pool);
    // Under leaks/valgrind, this test must report zero leaked bytes.
}

TEST(TaurusMemoryPool, ManyOversizedAllocsAllTracked) {
    TaurusMemoryPool* pool = taurus_pool_create_with_page_size(4096);
    for (int i = 0; i < 100; i++) {
        EXPECT_NE(taurus_pool_alloc(pool, 8000), nullptr);
    }
    taurus_pool_destroy(pool);
    // Zero leaks.
}
```

Plus an integration spec that parses a document with a 10 KB attribute
and asserts zero leaks under `leaks`.

## Architecture notes

The pool's ownership contract is now restored: **every byte allocated
from the pool is freed by `taurus_pool_destroy`**, regardless of size.

The tracking node itself is pool-allocated (it fits in a page), which
means we don't introduce a meta-leak of the tracker. The chained
`last_big_alloc_link` gives O(1) append instead of an O(n) tail walk.

This is the **minimal, complete** fix. An alternative — refusing
oversized allocations and forcing callers to chunk — would break the
public API and is the wrong tradeoff for an XML parser (a single 10 KB
attribute is legitimate).

## Verification

1. `leaks --atExit -- build/cli/taurus parse /tmp/bigattr.xml` — zero
   leaks (where `bigattr.xml` is the 10 KB-attribute fixture).
2. New spec passes under valgrind on Linux CI.
3. `taurus_pool_total_size()` (post-TODO 10) reflects the big-alloc
   bytes, so users can see the true pool footprint.
