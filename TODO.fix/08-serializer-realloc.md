# TODO 08: Fix serializer realloc-fail handling and size_t overflow

**Priority**: P1 (correctness)
**Status**: Planned
**Effort**: S

## Problem

`buffer_ensure_capacity` (`src/taurus/serialize/serialize.c:50-62`):

```c
void buffer_ensure_capacity(SerializeBuffer* buf, size_t needed) {
    if (buf->size + needed < buf->capacity) return;

    /* Double capacity until it's enough */
    while (buf->size + needed >= buf->capacity) {
        buf->capacity *= 2;
    }

    char* new_data = TAURUS_REALLOC_N(buf->data, char, buf->capacity);
    if (new_data) {
        buf->data = new_data;
    }
    // BUG 1: if realloc failed, buf->capacity is already inflated.
    //        Subsequent writes go past the actual allocation.
    // BUG 2: no overflow check on (buf->size + needed).
}
```

Two distinct defects:

1. **Silent overflow on realloc failure.** `buf->capacity` is updated
   before the realloc is attempted. If realloc returns NULL, `buf->data`
   is unchanged (still pointing at the old, smaller allocation) but
   `buf->capacity` claims the buffer is larger. Every subsequent write
   reads/writes out of bounds. On a memory-pressure scenario this is a
   real heap-corruption vector.
2. **size_t wrap on huge inputs.** `buf->size + needed` can wrap on
   32-bit `size_t` (or even 64-bit if the document is impossibly large).
   The guard `< buf->capacity` then fires wrongly and we write past the
   end.

## Fix

```c
void buffer_ensure_capacity(SerializeBuffer* buf, size_t needed) {
    if (needed <= buf->capacity - buf->size) {
        // capacity - size is the remaining space; can't wrap because
        // we maintain the invariant size <= capacity.
        return;
    }

    // Overflow-safe doubling.
    size_t new_cap = buf->capacity > 0 ? buf->capacity : INITIAL_BUFFER_CAPACITY;
    while (new_cap - buf->size < needed) {
        if (new_cap > (SIZE_MAX / 2)) {
            // Refuse to double past SIZE_MAX.
            new_cap = SIZE_MAX;
            break;
        }
        new_cap *= 2;
    }

    if (new_cap - buf->size < needed) {
        // Even SIZE_MAX wasn't enough — caller must handle the failure.
        // We do NOT touch buf->capacity (preserves the valid state).
        return;
    }

    char* new_data = TAURUS_REALLOC_N(buf->data, char, new_cap);
    if (!new_data) {
        // Realloc failed — buf->data and buf->capacity are unchanged.
        // Caller's append will silently truncate; document this and
        // provide buffer_has_error(buf) for callers that need to check.
        return;
    }

    buf->data = new_data;
    buf->capacity = new_cap;
}
```

Also add an `int alloc_failed` field to `SerializeBuffer` so callers can
detect the failure (currently there's no way to know). Set it on realloc
failure; `buffer_to_string` returns NULL if it's set.

## Tests

`test/serializer/test_serialize.cpp`:

```cpp
TEST(SerializeBuffer, GrowsToAccommodateLargeAppends) {
    SerializeBuffer* buf = buffer_create(0);
    ASSERT_NE(buf, nullptr);
    buffer_append_len(buf, std::string(10'000'000, 'x').c_str(), 10'000'000);
    EXPECT_GE(buf->capacity, 10'000'000u);
    EXPECT_EQ(buf->size, 10'000'000u);
    buffer_free(buf);
}

TEST(SerializeBuffer, MarksErrorOnReallocFailure) {
    // Use a custom taurus_alloc_hook that always fails for sizes > N.
    // (Requires the alloc hook override added in TODO 05.)
    SerializeBuffer* buf = buffer_create(0);
    // ... trigger a realloc beyond the failing threshold
    EXPECT_TRUE(buffer_has_error(buf));
    buffer_free(buf);
}

TEST(SerializeRoundTrip, HugeTextRoundTripsExactly) {
    std::string xml = "<r>" + std::string(5'000'000, 'A') + "</r>";
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr);

    char* out = taurus_element_serialize(taurus_document_root(doc), NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, xml.c_str());
    free(out);
    taurus_document_free(doc);
}
```

## Architecture notes

The post-condition of `buffer_ensure_capacity` is **either the buffer
can hold `needed` more bytes, or `alloc_failed` is set**. No third state.
Callers assert the post-condition with `buffer_has_error` before using
the buffer.

The overflow-safe arithmetic is the standard pattern: use subtraction
(`new_cap - buf->size`) instead of addition (`buf->size + needed`), so
wrap is impossible given the invariant `size <= capacity`.

## Verification

1. New specs pass.
2. Running the existing `taurus format` on a 10 MB text node succeeds.
3. ASAN build (when the project gets one) reports no heap-buffer-overflow
   in the serializer.
