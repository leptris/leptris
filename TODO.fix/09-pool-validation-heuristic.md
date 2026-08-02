# TODO 09: Remove pool pointer-validation heuristic

**Priority**: P2 (code smell)
**Status**: Planned
**Effort**: S

## Problem

`hash_string_view` (`src/taurus/memory/pool.c:289-301`) and
`taurus_pool_intern_string` (pool.c:353-370) contain this block:

```c
uintptr_t addr = (uintptr_t)sv->data;
if (addr < 0x1000) return 0;                  // near-NULL — reasonable
unsigned char* bytes = (unsigned char*)&addr;
int all_printable = 1;
for (size_t i = 0; i < sizeof(addr); i++) {
    if (bytes[i] < 0x20 || bytes[i] > 0x7E) {
        all_printable = 0;
        break;
    }
}
if (all_printable) return 0;                  // "ASCII text in pointer"
```

Same pattern in `taurus_pool_intern_string` for `entry->key_data`.

This is **cargo-cult defensive programming**. It tries to detect "memory
corruption" by checking whether the bytes of the pointer look like ASCII
text. It doesn't reliably detect corruption (corrupted pointers usually
don't have all-printable bytes), and it suggests the author didn't trust
the allocator's own invariants. A reader has to stop and puzzle out what
scenario this is defending against — and the answer is "none, really."

## Root cause

The author was diagnosing a real bug (the original codebase had a use-after-free
in the hash table that stored raw pointers into the XML buffer; that was
fixed at pool.c:421-428 by copying the key). The heuristic was a
knee-jerk "let's also reject obviously-wrong pointers" patch layered on
top. The real fix (the copy) made the heuristic unnecessary.

## Fix

Replace each block with the only check that's actually meaningful:
**non-NULL**. Optionally add an alignment assertion (the pool guarantees
8-byte alignment for pool-allocated data, but `sv->data` points into the
caller's XML buffer which is aligned to `malloc`'s default — typically
16 bytes — so alignment is not a useful invariant to enforce here).

```c
static uint32_t hash_string_view(const TaurusStringView* sv) {
    if (!sv || sv->length == 0 || sv->data == NULL) return 0;

    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sv->length; i++) {
        hash ^= (uint8_t)sv->data[i];
        hash *= 16777619u;
    }
    return hash;
}
```

And in `taurus_pool_intern_string`:

```c
char* taurus_pool_intern_string(TaurusMemoryPool* pool,
                                const TaurusStringView* sv) {
    if (!pool || !sv || sv->length == 0 || sv->data == NULL) {
        return NULL;
    }
    // ... rest unchanged, minus the heuristic block.
}
```

The `entry->key_data >= 0x1000` check (pool.c:391) — remove that too.

## Tests

`test/memory/test_pool.cpp`:

```cpp
TEST(PoolInternString, NullInputReturnsNull) {
    EXPECT_EQ(taurus_pool_intern_string(NULL, NULL), nullptr);

    TaurusMemoryPool* pool = taurus_pool_create();
    TaurusStringView sv = TAURUS_SV_FROM_CSTR("");
    EXPECT_EQ(taurus_pool_intern_string(pool, &sv), nullptr);
    taurus_pool_destroy(pool);
}

TEST(PoolInternString, DeduplicatesEqualStrings) {
    TaurusMemoryPool* pool = taurus_pool_create();
    TaurusStringView a = TAURUS_SV_FROM_CSTR("hello");
    TaurusStringView b = TAURUS_SV_FROM_CSTR("hello");
    char* pa = taurus_pool_intern_string(pool, &a);
    char* pb = taurus_pool_intern_string(pool, &b);
    EXPECT_EQ(pa, pb);                       // same interned address
    taurus_pool_destroy(pool);
}
```

## Architecture notes

Trust your invariants. If you suspect memory corruption, the right tool
is **ASAN** at build time, not runtime heuristics. The heuristic also
made the hot path slower — `hash_string_view` runs once per
inter/dedup call, and was doing 8 byte comparisons + branches that
could never usefully fire.

The only legitimate runtime check here is `NULL`. Adding it preserves
the original safety for genuinely empty input without the smell.

## Verification

1. New specs pass.
2. ASAN build (when available) shows no use-after-free in the interning
   path.
3. Microbenchmark: interning 1M strings is at least as fast as before
   (likely slightly faster).
