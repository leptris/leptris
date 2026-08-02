# TODO 10: Fix `taurus_pool_total_size` reporting

**Priority**: P2 (correctness)
**Status**: Planned
**Effort**: S

## Problem

`taurus_pool_total_size` (`src/taurus/memory/pool.c:601-612`):

```c
size_t taurus_pool_total_size(TaurusMemoryPool* pool) {
    if (!pool) return 0;
    size_t total = 0;
    MemoryPage* page = pool->first_page;
    while (page) {
        total += sizeof(MemoryPage);   // ← counts only the header
        page = page->next;
    }
    return total;
}
```

Walks the page list but adds only `sizeof(MemoryPage)` (the 24-byte
header struct) — never adds `page->page_size` (the actual data capacity).
The returned number is wildly undercounted. A pool with ten 32 KB pages
reports 240 bytes instead of ~327 KB.

It also omits oversized allocations entirely (pre-TODO 06 there was no
way to know about them; post-TODO 06 they're tracked in
`first_big_alloc`).

## Root cause

Looks like an editing artifact — the author probably meant
`sizeof(MemoryPage) + page->page_size` (the actual allocation size per
page), and dropped the second term.

## Fix

```c
size_t taurus_pool_total_size(TaurusMemoryPool* pool) {
    if (!pool) return 0;

    size_t total = 0;

    // Pages: header + data capacity, summed.
    for (MemoryPage* page = pool->first_page; page; page = page->next) {
        total += sizeof(MemoryPage) - 1 + page->page_size;
        // The -1 accounts for the char data[1] flexible-array sentinel
        // already included in sizeof(MemoryPage).
    }

    // Oversized allocations tracked on the side list (post-TODO 06).
    for (TaurusBigAlloc* big = pool->first_big_alloc; big; big = big->next) {
        total += big->size;
    }

    return total;
}

size_t taurus_pool_used_size(TaurusMemoryPool* pool) {
    // New: how much of the pool is actually in use right now.
    if (!pool) return 0;
    size_t used = 0;
    for (MemoryPage* page = pool->first_page; page; page = page->next) {
        used += page->busy_size;
    }
    for (TaurusBigAlloc* big = pool->first_big_alloc; big; big = big->next) {
        used += big->size;
    }
    return used;
}
```

Also fix `taurus_pool_page_count` to actually return what its name says
(it currently does, but make sure `pool != NULL` is checked — it is).

## Tests

`test/memory/test_pool.cpp`:

```cpp
TEST(PoolStats, TotalSizeIncludesAllPages) {
    TaurusMemoryPool* pool = taurus_pool_create_with_page_size(4096);
    ASSERT_NE(pool, nullptr);

    // Force at least 3 pages worth of small allocations.
    for (int i = 0; i < 3; i++) {
        ASSERT_NE(taurus_pool_alloc(pool, 4000), nullptr);
    }

    size_t total = taurus_pool_total_size(pool);
    EXPECT_GE(total, 3 * 4096u);    // at least 3 pages of capacity

    taurus_pool_destroy(pool);
}

TEST(PoolStats, TotalSizeIncludesOversizedAllocs) {
    TaurusMemoryPool* pool = taurus_pool_create_with_page_size(4096);
    ASSERT_NE(pool, nullptr);

    ASSERT_NE(taurus_pool_alloc(pool, 100'000), nullptr);

    EXPECT_GE(taurus_pool_total_size(pool), 100'000u);

    taurus_pool_destroy(pool);
}

TEST(PoolStats, UsedSizeReflectsActualUsage) {
    TaurusMemoryPool* pool = taurus_pool_create_with_page_size(4096);
    ASSERT_NE(pool, nullptr);

    taurus_pool_alloc(pool, 100);
    taurus_pool_alloc(pool, 200);
    // Account for alignment padding (8 bytes per alloc in current impl).
    size_t used = taurus_pool_used_size(pool);
    EXPECT_GE(used, 300u);
    EXPECT_LE(used, 300u + 2 * TAURUS_POOL_ALIGNMENT);

    taurus_pool_destroy(pool);
}
```

## Architecture notes

`taurus_pool_total_size` answers "how much memory does this pool
occupy?" — a question with exactly one right answer (pages + big allocs
+ headers). The current implementation gives a wrong answer.

The new `taurus_pool_used_size` answers a different question ("how much
of that is live data?"), which is also useful — e.g., the CLI could
report `used / total` after a parse to show waste. Two functions, two
questions, MECE.

## Verification

1. New specs pass.
2. After a parse of `benchmarks/data/small.xml`, the CLI could print
   pool stats that roughly match the file size (within 2x — pools always
   over-allocate).
