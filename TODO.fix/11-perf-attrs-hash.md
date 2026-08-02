# TODO 11: Investigate and fix attrs.xml performance regression

**Priority**: P1 (performance)
**Status**: Planned
**Effort**: M

## Problem

Standalone parse benchmark (clang -O3, Apple Silicon, single-threaded)
on `benchmarks/data/attrs.xml` (200 KB, ~25 attributes per element
across ~300 elements):

```
taurus  :   25.8 MB/s
libxml2 :   70.0 MB/s
ratio   :   2.71x slower
```

Every other file in the suite shows taurus at parity or 10-150% faster.
Attribute-heavy documents are the outlier.

## Root cause

Unknown — needs profiling. Hypotheses, in order of likelihood:

1. **Per-attribute hash-table allocation.** Each attribute may be
   allocating a new `StringHashTable` bucket, hashing the key, and
   inserting — even though most elements have <8 attributes where a
   linear scan would be faster.
2. **String interning on every attribute name/value.** The interning
   hash table may be thrashing cache for one-off attribute values that
   are never reused.
3. **Hash table bucket array over-allocation.** Initial bucket count may
   be too large for typical elements, causing zero-initialization cost
   per element.
4. **Per-attribute strdup.** Names and values may be copied twice (once
   into the pool, once into the hash entry's `key_data`).

## Fix (phased)

### Phase 1: Profile

```bash
# Instruments (macOS) — sample on allocating functions
instruments -t "Time Profiler" build/cli/taurus parse benchmarks/data/attrs.xml

# perf (Linux) — top functions by cycles
perf record -g ./build/cli/taurus parse benchmarks/data/attrs.xml
perf report --sort=overhead,symbol
```

Identify the top 3 functions by self-time during attrs.xml parse.

### Phase 2: Targeted fix

Based on profile data, the most likely fix is one of:

**(a) Adaptive attribute storage** — for elements with ≤ N attributes
(threshold ~8), skip the hash table entirely; use a contiguous array +
linear scan. Allocate the hash table lazily when an element exceeds the
threshold. Pugixml does exactly this.

```c
// In TaurusElement:
typedef struct taurus_element {
    // ...
    union {
        TaurusAttribute* inline_attrs[8];   // small-element fast path
        struct {
            StringHashTable* attr_table;    // fallback for big elements
        };
    };
    uint8_t attr_count;
    uint8_t using_hash_table;
} *TaurusElement;
```

**(b) Skip interning for attribute values** — intern only names (which
repeat across elements); values are usually unique and interning them
wastes time.

**(c) Cache-friendly bucket count** — start with 4 buckets, grow
dynamically at 75% load factor.

### Phase 3: Verify

Re-run the parse benchmark across all benchmark files. The fix must:
- Bring attrs.xml to within 1.2× of libxml2 (or faster).
- Not regress large.xml, large_workflow.xml, medium.xml (which are
  currently faster than libxml2).

## Tests

`test/dom/test_dom.cpp`:

```cpp
TEST(ElementAttributes, SmallAttributeCountUsesLinearScan) {
    // After the fix, this should be fast — no hash table allocation.
    TaurusMemoryPool* pool = taurus_pool_create();
    TaurusElement elem = taurus_element_create(pool);

    for (int i = 0; i < 5; i++) {
        char name[8]; snprintf(name, sizeof(name), "a%d", i);
        taurus_element_set_attribute(elem, name, "v");
    }

    // Functional correctness: lookup still works.
    EXPECT_NE(taurus_element_get_attribute(elem, "a3"), nullptr);
    EXPECT_EQ(taurus_element_get_attribute(elem, "a99"), nullptr);

    taurus_pool_destroy(pool);
}

TEST(ElementAttributes, LargeAttributeCountFallsBackToHash) {
    TaurusMemoryPool* pool = taurus_pool_create();
    TaurusElement elem = taurus_element_create(pool);

    for (int i = 0; i < 100; i++) {
        char name[16]; snprintf(name, sizeof(name), "attr%d", i);
        taurus_element_set_attribute(elem, name, "v");
    }

    for (int i = 0; i < 100; i++) {
        char name[16]; snprintf(name, sizeof(name), "attr%d", i);
        EXPECT_NE(taurus_element_get_attribute(elem, name), nullptr);
    }

    taurus_pool_destroy(pool);
}
```

Add a parse-perf microbenchmark in `benchmarks/dom/` that runs 1000
parses of attrs.xml and asserts the parse time stays under a budget
(e.g., < 5 ms/parse). Use `std::chrono::steady_clock`. Run as a separate
ctest label so it can be excluded from quick CI runs.

## Architecture notes

The adaptive-array-then-hash pattern is the **standard** solution for
"small collection that might grow." MECE: a single attribute store per
element, with one policy for switching representations. OCP: new
representations (e.g., sorted array for binary search) plug in via a
function pointer swap, not by rewriting callers.

Don't over-engineer: the threshold is data-driven, picked by
benchmarking, and configurable at compile time.

## Verification

1. `./bench benchmarks/data/attrs.xml 200` shows ratio ≤ 1.2× (taurus
   vs libxml2).
2. All other benchmark files retain their current speedup or improve.
3. New perf-microbenchmark ctest passes within its declared budget.
