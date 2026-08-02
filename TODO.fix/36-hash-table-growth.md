# TODO 36: Hash table dynamic growth

**Priority**: P2 (performance — affects large-attribute workloads)
**Status**: Planned
**Effort**: M

## Problem

`StringHashTable` is created with a fixed bucket count (128 for
interning, smaller for DTD tables) and never grows.  When the
entry-to-bucket ratio exceeds ~1.0, hash chains get long and lookup
degrades from O(1) to O(n).

For typical XML this is fine.  For pathological inputs (e.g., a
document with 10 000 distinct element names), it's a real slowdown.

## Root cause

`taurus_hash_table_create(pool, bucket_count)` allocates a fixed
bucket array.  `taurus_hash_table_set` and `taurus_pool_intern_string`
append to chains without considering the load factor.

## Fix

Add a load-factor check to `taurus_hash_table_set` /
`taurus_pool_intern_string`:

```c
/* Triggered when entry_count > 3/4 * bucket_count. */
static int hash_table_maybe_grow(StringHashTable** table_ptr,
                                  TaurusMemoryPool* pool) {
    StringHashTable* table = *table_ptr;
    if (table->entry_count < (table->bucket_count * 3) / 4) {
        return 0;  /* no growth needed */
    }

    size_t new_count = table->bucket_count * 2;
    /* Allocate new bucket array from pool. */
    StringHashEntry** new_buckets =
        pool_calloc(pool, sizeof(StringHashEntry*) * new_count);
    if (!new_buckets) return -1;

    /* Rehash: walk each existing chain, redistribute into new buckets. */
    for (size_t i = 0; i < table->bucket_count; i++) {
        StringHashEntry* e = table->buckets[i];
        while (e) {
            StringHashEntry* next = e->next;
            size_t b = hash_bytes(e->key_data, e->key_length) % new_count;
            e->next = new_buckets[b];
            new_buckets[b] = e;
            e = next;
        }
    }

    /* Old bucket array is pool-owned; will be reclaimed on pool
     * destroy.  No explicit free needed. */
    table->buckets = new_buckets;
    table->bucket_count = new_count;
    return 0;
}
```

Call from each insert path before incrementing `entry_count`.

## Tests

`test/memory/test_pool.cpp`:

```cpp
TEST(HashTableGrowth, GrowsPastLoadFactor) {
    TaurusMemoryPool* pool = taurus_pool_create();
    pool->string_cache = taurus_hash_table_create(pool, 4);
    ASSERT_NE(pool->string_cache, nullptr);

    /* Insert 100 unique strings — should trigger multiple grows. */
    for (int i = 0; i < 100; i++) {
        char s[16]; snprintf(s, sizeof(s), "key_%d", i);
        TaurusStringView sv = {s, strlen(s)};
        EXPECT_NE(taurus_pool_intern_string(pool, &sv), nullptr);
    }

    EXPECT_GE(pool->string_cache->bucket_count, 32u);
    EXPECT_EQ(pool->string_cache->entry_count, 100u);

    /* All 100 keys still resolvable. */
    for (int i = 0; i < 100; i++) {
        char s[16]; snprintf(s, sizeof(s), "key_%d", i);
        TaurusStringView sv = {s, strlen(s)};
        EXPECT_NE(taurus_pool_intern_string(pool, &sv), nullptr);
    }

    taurus_pool_destroy(pool);
}
```

## Architecture notes

Growth is **amortized O(1)** — each insert is O(1) on the fast path;
the occasional resize is O(n) but happens n/log(n) times, so amortized
constant.  Standard hash-table behavior.

The pool-based model makes growth easy: the old bucket array stays in
the pool and is reclaimed at pool destroy.  No manual free.

## Verification

```bash
ctest --test-dir build --output-on-failure -R HashTableGrowth
# Pass.

# Performance check on pathological input:
build/cli/taurus parse path/to/many_unique_names.xml
# Time should be linear in input size, not quadratic.
```
