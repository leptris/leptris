// test/memory/test_arena.cpp — contiguous arena specs (TODO 183 Phase 1).
//
// The load-bearing property under test: every pointer handed out lies
// within [base, base + size) of one contiguous allocation, and
// exhaustion is a hard NULL — never a silent fallback malloc. These
// are the invariants compact-pointer tree edges (TODO 180/181) rely on.

#include <gtest/gtest.h>

extern "C" {
#include "arena.h"
}

#include <cstdint>
#include <vector>

namespace {

TEST(Arena, CreateDestroyRoundTrip) {
    TaurusArena* a = taurus_arena_create(4096);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(taurus_arena_remaining(a), 4096);
    taurus_arena_destroy(a);
    taurus_arena_destroy(nullptr);  /* NULL-safe */
}

TEST(Arena, RejectsNonsenseSize) {
    EXPECT_EQ(taurus_arena_create(0), nullptr);
}

TEST(Arena, AllocIsAlignedAndWithinBase) {
    /* 100 iterations × up to align_up(33)=40 bytes → 4000 B worst case. */
    TaurusArena* a = taurus_arena_create(8192);
    ASSERT_NE(a, nullptr);
    void* base = taurus_arena_base(a);
    ASSERT_NE(base, nullptr);

    for (int i = 0; i < 100; i++) {
        size_t sz = 1 + (size_t)(i * 7 % 33);  /* odd sizes stress alignment */
        void* p = taurus_arena_alloc(a, sz);
        ASSERT_NE(p, nullptr) << "i=" << i;
        EXPECT_EQ((uintptr_t)p % 8, 0u) << "i=" << i;
        /* Within the single contiguous allocation. */
        EXPECT_GE((char*)p, (char*)base);
        EXPECT_LT((char*)p, (char*)base + 8192);
    }
    taurus_arena_destroy(a);
}

TEST(Arena, AllocsDoNotOverlap) {
    TaurusArena* a = taurus_arena_create(512);
    ASSERT_NE(a, nullptr);
    std::vector<void*> ptrs;
    for (int i = 0; i < 64; i++) {
        void* p = taurus_arena_alloc(a, 8);
        ASSERT_NE(p, nullptr);
        for (void* q : ptrs) {
            /* Distinct 8-byte slots must differ. */
            EXPECT_NE(p, q);
        }
        /* Write to prove the slot is exclusively owned. */
        *(char*)p = (char)i;
        ptrs.push_back(p);
    }
    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(*(char*)ptrs[i], (char)i);
    }
    taurus_arena_destroy(a);
}

TEST(Arena, ExhaustionReturnsNullThenSmallerFits) {
    /* THE contract: no silent fallback. A refused request returns
     * NULL; a smaller request that still fits must succeed. */
    TaurusArena* a = taurus_arena_create(64);
    ASSERT_NE(a, nullptr);
    void* p = taurus_arena_alloc(a, 56);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(taurus_arena_remaining(a), 8u);
    EXPECT_EQ(taurus_arena_alloc(a, 16), nullptr);
    void* q = taurus_arena_alloc(a, 8);
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(taurus_arena_remaining(a), 0u);
    taurus_arena_destroy(a);
}

TEST(Arena, ExactFitSucceedsAndNextFails) {
    TaurusArena* a = taurus_arena_create(64);
    ASSERT_NE(a, nullptr);
    void* p = taurus_arena_alloc(a, 64);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(taurus_arena_remaining(a), 0u);
    EXPECT_EQ(taurus_arena_alloc(a, 1), nullptr);
    EXPECT_EQ(taurus_arena_alloc(a, 8), nullptr);
    taurus_arena_destroy(a);
}

TEST(Arena, OversizedRequestFails) {
    TaurusArena* a = taurus_arena_create(128);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(taurus_arena_alloc(a, 129), nullptr);
    /* Small requests still work after a refusal. */
    void* p = taurus_arena_alloc(a, 16);
    ASSERT_NE(p, nullptr);
    taurus_arena_destroy(a);
}

TEST(Arena, ZeroedAllocClears) {
    TaurusArena* a = taurus_arena_create(256);
    ASSERT_NE(a, nullptr);
    /* Dirty the memory first via a plain alloc. */
    char* dirty = (char*)taurus_arena_alloc(a, 32);
    ASSERT_NE(dirty, nullptr);
    memset(dirty, 0xAB, 32);
    char* z = (char*)taurus_arena_alloc_zeroed(a, 32);
    ASSERT_NE(z, nullptr);
    for (int i = 0; i < 32; i++) EXPECT_EQ(z[i], 0);
    taurus_arena_destroy(a);
}

TEST(Arena, NodeWithContentIsContiguous) {
    TaurusArena* a = taurus_arena_create(512);
    ASSERT_NE(a, nullptr);
    char* content = nullptr;
    void* node = taurus_arena_alloc_node_with_content(a, 24, 100, &content);
    ASSERT_NE(node, nullptr);
    ASSERT_NE(content, nullptr);
    /* Content follows the struct (aligned up), within the arena. */
    char* base = (char*)taurus_arena_base(a);
    EXPECT_GE(content, (char*)node + 24);
    EXPECT_LT(content, base + 512);
    /* Writing 100 bytes + NUL must be in-bounds. */
    memset(content, 'x', 100);
    content[100] = '\0';
    taurus_arena_destroy(a);
}

TEST(Arena, NodeWithContentKeepsNextAllocAligned) {
    /* Regression: content_size+1 must round up to the 8-byte grid,
     * else every allocation after an odd-sized node+content bump
     * lands misaligned (caught live when direct_parse moved onto the
     * arena — comments/PIs mangled downstream). */
    TaurusArena* a = taurus_arena_create(8192);
    ASSERT_NE(a, nullptr);
    for (int i = 0; i < 50; i++) {
        char* content = nullptr;
        void* node = taurus_arena_alloc_node_with_content(
            a, 24, (size_t)(i % 2 ? 13 : 40), &content);  /* odd sizes */
        ASSERT_NE(node, nullptr) << "i=" << i;
        EXPECT_EQ((uintptr_t)node % 8, 0u) << "i=" << i;
        void* next = taurus_arena_alloc(a, 8);
        ASSERT_NE(next, nullptr) << "i=" << i;
        EXPECT_EQ((uintptr_t)next % 8, 0u) << "i=" << i;
    }
    taurus_arena_destroy(a);
}

TEST(Arena, NodeWithContentFailsWhenTooLarge) {
    TaurusArena* a = taurus_arena_create(128);
    ASSERT_NE(a, nullptr);
    char* content = nullptr;
    /* 24 (aligned) + 200 + 1 = 225 > 128 → refuse, no fallback. */
    EXPECT_EQ(taurus_arena_alloc_node_with_content(a, 24, 200, &content), nullptr);
    taurus_arena_destroy(a);
}

TEST(Arena, AllPointersWithinSpanForCompactEncoding) {
    /* Simulates the compact-pointer use case: allocate many "nodes"
     * and verify every pairwise distance is bounded by the arena size
     * (the property page-based pools cannot guarantee). */
    const size_t SIZE = 4096;
    TaurusArena* a = taurus_arena_create(SIZE);
    ASSERT_NE(a, nullptr);
    std::vector<void*> nodes;
    while (true) {
        void* p = taurus_arena_alloc(a, 24);
        if (!p) break;
        nodes.push_back(p);
    }
    ASSERT_GT(nodes.size(), 100u);
    char* base = (char*)taurus_arena_base(a);
    for (void* p : nodes) {
        ptrdiff_t off = (char*)p - base;
        EXPECT_GE(off, 0);
        EXPECT_LT(off, (ptrdiff_t)SIZE);
        /* Every pairwise distance < SIZE ⇒ cp16 with the right
         * capacity model can encode any edge. */
    }
    taurus_arena_destroy(a);
}

}  // namespace
