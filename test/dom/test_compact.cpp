// test/dom/test_compact.cpp — compact allocator / overflow table specs (TODO 39).
//
// The compact allocator is the 4-byte-pointer optimization used by the
// DOM.  When a pointer can't be encoded into 4 bytes (e.g., the
// address is far from the pool's base), an overflow entry is added to
// a thread-local table.  These specs exercise the table's lifecycle
// and verify cleanup happens per-document.

#include <gtest/gtest.h>

#include "taurus.h"

extern "C" {
#include "compact.h"
}

#include <cstring>
#include <vector>
#include <cstdlib>

namespace {

TEST(CompactAllocator, ParsesMultipleDocumentsWithoutLeak) {
    /* The overflow table is reused across documents within a thread.
     * Each document free must clean up its own entries. */
    const char xml[] = "<r><a/><b/><c/></r>";

    for (int i = 0; i < 5; i++) {
        TaurusStatus st;
        TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
        ASSERT_NE(doc, nullptr) << "iter " << i;
        taurus_document_free(doc);
    }
    /* Under leaks --atExit --, zero bytes leaked is the contract. */
}

TEST(CompactAllocator, ExplicitCleanupDoesNotCrash) {
    /* taurus_explicit_cleanup tears down the thread-local overflow
     * table.  Calling it when no documents are active must be safe. */
    taurus_explicit_cleanup();
    taurus_explicit_cleanup();  /* idempotent */
    SUCCEED();
}

TEST(CompactAllocator, LargeDocumentDoesNotLeakOverflowEntries) {
    /* A document large enough to trigger many overflow entries must
     * release them all on free. */
    std::string xml = "<r>";
    for (int i = 0; i < 1000; i++) {
        xml += "<a>text</a>";
    }
    xml += "</r>";

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr);

    taurus_document_free(doc);
    /* Zero leaks under valgrind/leaks. */
}

/* ---- TODO 178: 1-byte and 2-byte compact pointer round-trip specs ---- */

TEST(CompactPtr8, NullRoundTrips) {
    /* base + non-null field address. Encode NULL → 0. Decode 0 → NULL. */
    int dummy_base;
    int8_t field = 42;
    int8_t encoded = taurus_compact_ptr8_encode(&dummy_base, nullptr,
                                                 3 /* align_log2 */,
                                                 &field);
    EXPECT_EQ(encoded, 0);
    EXPECT_EQ(taurus_compact_ptr8_decode(&dummy_base, 0, 3, &field), nullptr);
}

TEST(CompactPtr8, EncodesPositiveOffsetWithinRange) {
    /* Allocate two 8-byte-aligned buffers far enough apart that the
     * offset fits in 1 byte (scaled by 8). */
    char buf[2048];
    char* base = buf;
    char* target = buf + 256;  /* 256 / 8 = 32, fits in int8_t */
    int8_t field = 0;
    int8_t encoded = taurus_compact_ptr8_encode(base, target, 3, &field);
    ASSERT_NE(encoded, 0);
    ASSERT_NE(encoded, TAURUS_COMPACT_PTR8_OVERFLOW);
    EXPECT_EQ(taurus_compact_ptr8_decode(base, encoded, 3, &field), target);
}

TEST(CompactPtr8, EncodesNegativeOffsetWithinRange) {
    char buf[2048];
    char* base = buf + 1024;  /* middle of buffer */
    char* target = buf + 512; /* 512 bytes before base; scaled -64 fits in int8_t */
    int8_t field = 0;
    int8_t encoded = taurus_compact_ptr8_encode(base, target, 3, &field);
    ASSERT_NE(encoded, 0);
    ASSERT_NE(encoded, TAURUS_COMPACT_PTR8_OVERFLOW);
    EXPECT_EQ(taurus_compact_ptr8_decode(base, encoded, 3, &field), target);
}

TEST(CompactPtr8, OverflowsBeyond1ByteRange) {
    /* Offset too large for 1 byte even with alignment scaling. */
    size_t sz = 64 * 1024;
    void* big = std::malloc(sz);
    ASSERT_NE(big, nullptr);
    char* base = (char*)big;
    char* target = (char*)big + sz - 8;  /* far beyond 1KB reach */
    /* Round target down to 8-byte alignment. */
    target = (char*)((uintptr_t)target & ~(uintptr_t)7);

    int8_t field = 0;
    int8_t encoded = taurus_compact_ptr8_encode(base, target, 3, &field);
    EXPECT_EQ(encoded, TAURUS_COMPACT_PTR8_OVERFLOW);
    EXPECT_EQ(taurus_compact_ptr8_decode(base, encoded, 3, &field), target);

    /* Cleanup so we don't leak the overflow entry. */
    taurus_explicit_cleanup();
    std::free(big);
}

TEST(CompactPtr8, MisalignedTargetOverflows) {
    /* Offset between base and target isn't divisible by alignment. */
    char buf[256];
    char* base = buf;
    char* target = buf + 5;  /* not 8-byte aligned */
    int8_t field = 0;
    int8_t encoded = taurus_compact_ptr8_encode(base, target, 3, &field);
    EXPECT_EQ(encoded, TAURUS_COMPACT_PTR8_OVERFLOW);
    EXPECT_EQ(taurus_compact_ptr8_decode(base, encoded, 3, &field), target);
    taurus_explicit_cleanup();
}

TEST(CompactPtr8, DistinctFieldsOnSameBaseDoNotCollide) {
    /* Two fields on the same struct each get their own overflow entry. */
    size_t sz = 64 * 1024;
    void* big = std::malloc(sz);
    ASSERT_NE(big, nullptr);
    char* base = (char*)big;
    char* t1 = (char*)big + sz - 8;
    char* t2 = (char*)big + sz - 16;
    t1 = (char*)((uintptr_t)t1 & ~(uintptr_t)7);
    t2 = (char*)((uintptr_t)t2 & ~(uintptr_t)7);

    int8_t f1 = 0, f2 = 0;
    int8_t e1 = taurus_compact_ptr8_encode(base, t1, 3, &f1);
    int8_t e2 = taurus_compact_ptr8_encode(base, t2, 3, &f2);
    EXPECT_EQ(e1, TAURUS_COMPACT_PTR8_OVERFLOW);
    EXPECT_EQ(e2, TAURUS_COMPACT_PTR8_OVERFLOW);
    EXPECT_EQ(taurus_compact_ptr8_decode(base, e1, 3, &f1), t1);
    EXPECT_EQ(taurus_compact_ptr8_decode(base, e2, 3, &f2), t2);

    taurus_explicit_cleanup();
    std::free(big);
}

/* ---- 2-byte compact pointer ---- */

TEST(CompactPtr16, NullRoundTrips) {
    int dummy_base;
    int16_t field = 42;
    int16_t encoded = taurus_compact_ptr16_encode(&dummy_base, nullptr,
                                                   3, &field);
    EXPECT_EQ(encoded, 0);
    EXPECT_EQ(taurus_compact_ptr16_decode(&dummy_base, 0, 3, &field), nullptr);
}

TEST(CompactPtr16, EncodesLargeOffsetWithinRange) {
    /* 2-byte covers ±256 KB at align_log2=3 — enough for any document. */
    size_t sz = 100 * 1024;
    void* big = std::malloc(sz);
    ASSERT_NE(big, nullptr);
    char* base = (char*)big;
    char* target = (char*)big + 65536;  /* 64 KB away, fits in int16_t */
    int16_t field = 0;
    int16_t encoded = taurus_compact_ptr16_encode(base, target, 3, &field);
    ASSERT_NE(encoded, 0);
    ASSERT_NE(encoded, TAURUS_COMPACT_PTR16_OVERFLOW);
    EXPECT_EQ(taurus_compact_ptr16_decode(base, encoded, 3, &field), target);
    std::free(big);
}

TEST(CompactPtr16, OverflowsBeyond256KB) {
    /* Beyond int16 range even with alignment — needs overflow table. */
    size_t sz = 1024 * 1024;
    void* big = std::malloc(sz);
    ASSERT_NE(big, nullptr);
    char* base = (char*)big;
    char* target = (char*)big + sz - 8;
    target = (char*)((uintptr_t)target & ~(uintptr_t)7);

    int16_t field = 0;
    int16_t encoded = taurus_compact_ptr16_encode(base, target, 3, &field);
    EXPECT_EQ(encoded, TAURUS_COMPACT_PTR16_OVERFLOW);
    EXPECT_EQ(taurus_compact_ptr16_decode(base, encoded, 3, &field), target);

    taurus_explicit_cleanup();
    std::free(big);
}

TEST(CompactPtr8And16, ShareOverflowTableWithoutInterference) {
    /* Both encoders use the same table; field addresses are unique
     * keys, so 1-byte and 2-byte entries coexist safely. */
    size_t sz = 1024 * 1024;
    void* big = std::malloc(sz);
    ASSERT_NE(big, nullptr);
    char* base = (char*)big;
    char* target = (char*)big + sz - 8;
    target = (char*)((uintptr_t)target & ~(uintptr_t)7);

    int8_t f8 = 0;
    int16_t f16 = 0;
    int8_t e8 = taurus_compact_ptr8_encode(base, target, 3, &f8);
    int16_t e16 = taurus_compact_ptr16_encode(base, target, 3, &f16);
    EXPECT_EQ(e8, TAURUS_COMPACT_PTR8_OVERFLOW);
    EXPECT_EQ(e16, TAURUS_COMPACT_PTR16_OVERFLOW);
    EXPECT_EQ(taurus_compact_ptr8_decode(base, e8, 3, &f8), target);
    EXPECT_EQ(taurus_compact_ptr16_decode(base, e16, 3, &f16), target);

    taurus_explicit_cleanup();
    std::free(big);
}

}  // namespace
