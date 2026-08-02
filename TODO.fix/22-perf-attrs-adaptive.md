# TODO 22: Adaptive attribute storage (fix attrs.xml perf regression)

**Priority**: P1 (performance)
**Status**: Planned
**Effort**: M

## Problem

`benchmarks/data/attrs.xml` (200 KB, ~25 attributes per element across
~300 elements) parses **2.7–3.4× slower** than libxml2:

```
taurus  :  13.7 MB/s
libxml2 :  45.8 MB/s
ratio   :  3.36x slower
```

Every other benchmark file is at parity or faster than libxml2.
Attribute-heavy documents are the outlier.

## Root cause (hypothesis)

The element struct stores attributes in a hash table from creation,
even for elements with 1–2 attributes where a linear scan would be
faster. The per-element costs:

1. Allocate hash-table bucket array (typically 8+ buckets × 8 bytes =
   64 B per element).
2. Zero-initialize the bucket array.
3. Hash each attribute name on insert.
4. Resolve hash collisions via linked-list walk.

For elements with ≤ 8 attributes (the vast majority of real-world
XML), steps 1–3 are pure overhead. A linear array of `(name, value)`
pairs scanned with `strcmp` would be faster (no allocation, no hash,
better cache locality).

pugixml — the design reference for this codebase — uses exactly this
adaptive pattern: linear storage until the attribute count exceeds a
threshold, then promotion to a hash table.

## Fix

### Strategy: linear-then-hash

```c
// dom/element.h
typedef struct taurus_element {
    // ... existing fields
    union {
        struct {
            TaurusAttribute* attrs_inline;   // Small-array fast path
            uint8_t inline_count;
        } linear;
        StringHashTable* table;              // Hash fallback
    } attr_store;
    uint8_t using_hash_table;                // 0 = linear, 1 = hash
} *TaurusElement;
```

Threshold: **8 attributes**. Below, use linear; at 8, promote to hash.

### Operations

- `taurus_element_get_attribute(elem, name)`:
  - If `using_hash_table`: hash lookup.
  - Else: linear scan of `attrs_inline[0..inline_count]`.

- `taurus_element_add_attribute(elem, name, value)`:
  - If not using hash and `inline_count < 8`: append to linear array.
  - If not using hash and `inline_count == 8`: promote — allocate
    hash table, insert all 9 attributes, set `using_hash_table = 1`.
  - If using hash: hash insert.

- `taurus_element_remove_attribute(elem, name)`:
  - If using hash and post-remove count would be < threshold,
    demote back to linear (optional; can skip for simplicity).

### Performance characteristics

For attrs.xml (25 attrs/elem):
- First 8 inserts: linear, no allocation.
- 9th insert: promote (one hash-table allocation), then 9-25 are hash
  inserts. Hash wins for >8.

For typical XML (1-3 attrs/elem):
- All inserts stay linear. No hash allocation. Faster.

The threshold is data-driven — 8 is the pugixml default; we should
re-benchmark once the pattern lands and tune if needed.

## Tests

`test/dom/test_dom.cpp`:

```cpp
TEST(ElementAttributes, SmallCountUsesLinearScan) {
    TaurusMemoryPool* pool = taurus_pool_create();
    TaurusElement e = taurus_element_create("e", 1, pool);

    for (int i = 0; i < 5; i++) {
        char name[8]; snprintf(name, sizeof(name), "a%d", i);
        taurus_element_add_attribute(e, name, "v", pool);
    }

    EXPECT_EQ(taurus_element_using_hash_table(e), 0);

    // Lookup still works.
    EXPECT_STREQ(taurus_element_attribute(e, "a3"), "v");
    EXPECT_EQ(taurus_element_attribute(e, "missing"), nullptr);

    taurus_pool_destroy(pool);
}

TEST(ElementAttributes, LargeCountPromotesToHash) {
    TaurusMemoryPool* pool = taurus_pool_create();
    TaurusElement e = taurus_element_create("e", 1, pool);

    for (int i = 0; i < 20; i++) {
        char name[8]; snprintf(name, sizeof(name), "a%d", i);
        taurus_element_add_attribute(e, name, "v", pool);
    }

    EXPECT_EQ(taurus_element_using_hash_table(e), 1);

    for (int i = 0; i < 20; i++) {
        char name[8]; snprintf(name, sizeof(name), "a%d", i);
        EXPECT_NE(taurus_element_attribute(e, name), nullptr);
    }

    taurus_pool_destroy(pool);
}
```

Plus a perf-microbenchmark under `benchmarks/dom/` that runs 1000
parses of attrs.xml and asserts the time stays under a budget.

## Architecture notes

The adaptive pattern is **data-driven polymorphism**: the element's
`using_hash_table` flag selects the algorithm at runtime. This is
OCP-friendly — adding a third representation (sorted array for binary
search, Bloom filter for fast miss, etc.) is a new flag value, not a
rewrite of callers.

**Do not over-engineer.** Start with linear + hash. The threshold is
empirical — benchmark, don't guess.

## Verification

```bash
./bench benchmarks/data/attrs.xml 200
# Expected: ratio ≤ 1.2x (taurus vs libxml2), down from 3.4x.

./bench benchmarks/data/small.xml   5000
./bench benchmarks/data/large.xml   100
# Expected: no regression on other files.
```
