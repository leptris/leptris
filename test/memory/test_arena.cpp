// test/memory/test_arena.cpp — contiguous arena specs (TODO 183 Phase 1).
//
// The load-bearing property under test: every pointer handed out lies
// within [base, base + size) of one contiguous allocation, and
// exhaustion is a hard NULL — never a silent fallback malloc. These
// are the invariants compact-pointer tree edges (TODO 180/181) rely on.

#include <gtest/gtest.h>

extern "C" {
#include "arena.h"
#include "leptris.h"
}

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

TEST(Arena, CreateDestroyRoundTrip) {
    LeptrisArena* a = leptris_arena_create(4096);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(leptris_arena_remaining(a), 4096);
    leptris_arena_destroy(a);
    leptris_arena_destroy(nullptr);  /* NULL-safe */
}

TEST(Arena, RejectsNonsenseSize) {
    EXPECT_EQ(leptris_arena_create(0), nullptr);
}

TEST(Arena, AllocIsAlignedAndWithinBase) {
    /* 100 iterations × up to align_up(33)=40 bytes → 4000 B worst case. */
    LeptrisArena* a = leptris_arena_create(8192);
    ASSERT_NE(a, nullptr);
    void* base = leptris_arena_base(a);
    ASSERT_NE(base, nullptr);

    for (int i = 0; i < 100; i++) {
        size_t sz = 1 + (size_t)(i * 7 % 33);  /* odd sizes stress alignment */
        void* p = leptris_arena_alloc(a, sz);
        ASSERT_NE(p, nullptr) << "i=" << i;
        EXPECT_EQ((uintptr_t)p % 8, 0u) << "i=" << i;
        /* Within the single contiguous allocation. */
        EXPECT_GE((char*)p, (char*)base);
        EXPECT_LT((char*)p, (char*)base + 8192);
    }
    leptris_arena_destroy(a);
}

TEST(Arena, AllocsDoNotOverlap) {
    LeptrisArena* a = leptris_arena_create(512);
    ASSERT_NE(a, nullptr);
    std::vector<void*> ptrs;
    for (int i = 0; i < 64; i++) {
        void* p = leptris_arena_alloc(a, 8);
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
    leptris_arena_destroy(a);
}

TEST(Arena, ExhaustionReturnsNullThenSmallerFits) {
    /* THE contract: no silent fallback. A refused request returns
     * NULL; a smaller request that still fits must succeed. */
    LeptrisArena* a = leptris_arena_create(64);
    ASSERT_NE(a, nullptr);
    void* p = leptris_arena_alloc(a, 56);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(leptris_arena_remaining(a), 8u);
    EXPECT_EQ(leptris_arena_alloc(a, 16), nullptr);
    void* q = leptris_arena_alloc(a, 8);
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(leptris_arena_remaining(a), 0u);
    leptris_arena_destroy(a);
}

TEST(Arena, ExactFitSucceedsAndNextFails) {
    LeptrisArena* a = leptris_arena_create(64);
    ASSERT_NE(a, nullptr);
    void* p = leptris_arena_alloc(a, 64);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(leptris_arena_remaining(a), 0u);
    EXPECT_EQ(leptris_arena_alloc(a, 1), nullptr);
    EXPECT_EQ(leptris_arena_alloc(a, 8), nullptr);
    leptris_arena_destroy(a);
}

TEST(Arena, OversizedRequestFails) {
    LeptrisArena* a = leptris_arena_create(128);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(leptris_arena_alloc(a, 129), nullptr);
    /* Small requests still work after a refusal. */
    void* p = leptris_arena_alloc(a, 16);
    ASSERT_NE(p, nullptr);
    leptris_arena_destroy(a);
}

TEST(Arena, ZeroedAllocClears) {
    LeptrisArena* a = leptris_arena_create(256);
    ASSERT_NE(a, nullptr);
    /* Dirty the memory first via a plain alloc. */
    char* dirty = (char*)leptris_arena_alloc(a, 32);
    ASSERT_NE(dirty, nullptr);
    memset(dirty, 0xAB, 32);
    char* z = (char*)leptris_arena_alloc_zeroed(a, 32);
    ASSERT_NE(z, nullptr);
    for (int i = 0; i < 32; i++) EXPECT_EQ(z[i], 0);
    leptris_arena_destroy(a);
}

TEST(Arena, NodeWithContentIsContiguous) {
    LeptrisArena* a = leptris_arena_create(512);
    ASSERT_NE(a, nullptr);
    char* content = nullptr;
    void* node = leptris_arena_alloc_node_with_content(a, 24, 100, &content);
    ASSERT_NE(node, nullptr);
    ASSERT_NE(content, nullptr);
    /* Content follows the struct (aligned up), within the arena. */
    char* base = (char*)leptris_arena_base(a);
    EXPECT_GE(content, (char*)node + 24);
    EXPECT_LT(content, base + 512);
    /* Writing 100 bytes + NUL must be in-bounds. */
    memset(content, 'x', 100);
    content[100] = '\0';
    leptris_arena_destroy(a);
}

TEST(Arena, NodeWithContentKeepsNextAllocAligned) {
    /* Regression: content_size+1 must round up to the 8-byte grid,
     * else every allocation after an odd-sized node+content bump
     * lands misaligned (caught live when direct_parse moved onto the
     * arena — comments/PIs mangled downstream). */
    LeptrisArena* a = leptris_arena_create(8192);
    ASSERT_NE(a, nullptr);
    for (int i = 0; i < 50; i++) {
        char* content = nullptr;
        void* node = leptris_arena_alloc_node_with_content(
            a, 24, (size_t)(i % 2 ? 13 : 40), &content);  /* odd sizes */
        ASSERT_NE(node, nullptr) << "i=" << i;
        EXPECT_EQ((uintptr_t)node % 8, 0u) << "i=" << i;
        void* next = leptris_arena_alloc(a, 8);
        ASSERT_NE(next, nullptr) << "i=" << i;
        EXPECT_EQ((uintptr_t)next % 8, 0u) << "i=" << i;
    }
    leptris_arena_destroy(a);
}

TEST(Arena, NodeWithContentFailsWhenTooLarge) {
    LeptrisArena* a = leptris_arena_create(128);
    ASSERT_NE(a, nullptr);
    char* content = nullptr;
    /* 24 (aligned) + 200 + 1 = 225 > 128 → refuse, no fallback. */
    EXPECT_EQ(leptris_arena_alloc_node_with_content(a, 24, 200, &content), nullptr);
    leptris_arena_destroy(a);
}

TEST(Arena, AllPointersWithinSpanForCompactEncoding) {
    /* Simulates the compact-pointer use case: allocate many "nodes"
     * and verify every pairwise distance is bounded by the arena size
     * (the property page-based pools cannot guarantee). */
    const size_t SIZE = 4096;
    LeptrisArena* a = leptris_arena_create(SIZE);
    ASSERT_NE(a, nullptr);
    std::vector<void*> nodes;
    while (true) {
        void* p = leptris_arena_alloc(a, 24);
        if (!p) break;
        nodes.push_back(p);
    }
    ASSERT_GT(nodes.size(), 100u);
    char* base = (char*)leptris_arena_base(a);
    for (void* p : nodes) {
        ptrdiff_t off = (char*)p - base;
        EXPECT_GE(off, 0);
        EXPECT_LT(off, (ptrdiff_t)SIZE);
        /* Every pairwise distance < SIZE ⇒ cp16 with the right
         * capacity model can encode any edge. */
    }
    leptris_arena_destroy(a);
}


/* ---- Retained-block free list (parse fault fix) ---------------------- */

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ARENA_TESTS_ASAN 1
#endif
#elif defined(__SANITIZE_ADDRESS__)
#define ARENA_TESTS_ASAN 1
#endif

TEST(LeptrisArena, RetainsAndReusesLargeBlocks) {
    /* Blocks >= 256 KB must round-trip through the free list: the
     * next same-size create gets the SAME mapping back (no munmap,
     * no page faults). Machine-independent by construction: it
     * asserts pointer identity, not timing. */
    /* Under ASAN the retain cache frees for real (no TLS
     * destructor; parked blocks read as LSan leaks) — the reuse
     * contract is deliberately disabled there. */
#if defined(ARENA_TESTS_ASAN)
    GTEST_SKIP() << "retain cache disabled under ASAN";
#endif
    const size_t big = 512u * 1024u;
    LeptrisArena* a1 = leptris_arena_create(big);
    ASSERT_NE(a1, nullptr);
    void* b1 = leptris_arena_base(a1);
    memset(b1, 0xAB, big);          /* dirty every page */
    leptris_arena_destroy(a1);

    LeptrisArena* a2 = leptris_arena_create(big);
    ASSERT_NE(a2, nullptr);
    EXPECT_EQ(leptris_arena_base(a2), b1);   /* same mapping reused */
    leptris_arena_destroy(a2);
}

TEST(LeptrisArena, RetentionCapacityIsHonest) {
    /* A reused block may be larger than requested; remaining()
     * must report the BLOCK capacity so the fail-fast bound
     * [base, base+size) stays exact. */
    LeptrisArena* a1 = leptris_arena_create(300u * 1024u);
    ASSERT_NE(a1, nullptr);
    size_t cap1 = leptris_arena_remaining(a1);
    leptris_arena_destroy(a1);

    LeptrisArena* a2 = leptris_arena_create(260u * 1024u);  /* fits inside */
    ASSERT_NE(a2, nullptr);
    EXPECT_GE(leptris_arena_remaining(a2), 260u * 1024u);
    EXPECT_LE(leptris_arena_remaining(a2), cap1);
    leptris_arena_destroy(a2);
}

TEST(LeptrisArena, SmallBlocksStillMallocSemantics) {
    /* Below the retain threshold nothing is parked; semantics are
     * plain malloc/free and all writes stay valid. */
    LeptrisArena* a = leptris_arena_create(1024);
    ASSERT_NE(a, nullptr);
    char* p = (char*)leptris_arena_alloc(a, 1024);
    ASSERT_NE(p, nullptr);
    memset(p, 7, 1024);
    EXPECT_EQ((unsigned char)p[1023], 7);
    leptris_arena_destroy(a);
}

TEST(LeptrisArena, BufferRoundTripReusesMapping) {
/* Under ASAN the retain cache frees for real (no TLS
     * destructor; parked blocks read as LSan leaks) — the reuse
     * contract is deliberately disabled there. */
#if defined(ARENA_TESTS_ASAN)
    GTEST_SKIP() << "retain cache disabled under ASAN";
#endif

    char* b1 = leptris_arena_buffer_alloc(512u * 1024u);
    ASSERT_NE(b1, nullptr);
    memset(b1, 1, 512u * 1024u);
    leptris_arena_buffer_release(b1, 512u * 1024u);

    char* b2 = leptris_arena_buffer_alloc(512u * 1024u);
    ASSERT_NE(b2, nullptr);
    EXPECT_EQ(b2, b1);
    leptris_arena_buffer_release(b2, 512u * 1024u);
}
}  // namespace

// ---- #1436 tiny-doc fixed row: the small-span TLS recycle -------
// The global retain tier deliberately skips spans below 256 KB; the
// per-thread small cache parks them instead (stands down under
// custom allocators, exactly like the pool recycler). Parking is
// observable WITHOUT hooks via pointer identity: a same-size
// create/destroy/create cycle reuses the parked span's address.
TEST(ArenaSmallRecycle, SameSizeCycleReusesTheParkedSpan) {
    leptris_arena_retain_drain();

    LeptrisArena* a1 = leptris_arena_create(64u * 1024u);
    ASSERT_NE(a1, nullptr);
    void* base1 = leptris_arena_base(a1);
    leptris_arena_destroy(a1);

    LeptrisArena* a2 = leptris_arena_create(64u * 1024u);
    ASSERT_NE(a2, nullptr);
    /* Exact-size reuse: capacity stays exactly the request — the
     * fail-fast bound does not silently widen (first-fit would). */
    EXPECT_EQ(leptris_arena_remaining(a2), 64u * 1024u);
    EXPECT_EQ(leptris_arena_base(a2), base1)
        << "small span was not parked for reuse";
    leptris_arena_destroy(a2);

    leptris_arena_retain_drain();
}

// Custom-allocator accounting: every cycle is a visible
// malloc/free pair — the cache must not park hooked spans.
namespace {

long g_recycle_allocs = 0;
void* recycle_counting_alloc(size_t n) {
    g_recycle_allocs++;
    return malloc(n);
}
long g_recycle_frees = 0;
void recycle_counting_free(void* p) {
    g_recycle_frees++;
    free(p);
}

}  // namespace

TEST(ArenaSmallRecycle, StandsDownUnderCustomAllocator) {
    leptris_arena_retain_drain();
    leptris_set_memory_management_functions(
        recycle_counting_alloc, recycle_counting_free);
    g_recycle_allocs = 0;
    g_recycle_frees = 0;

    for (int i = 0; i < 32; i++) {
        LeptrisArena* a = leptris_arena_create(32u * 1024u);
        ASSERT_NE(a, nullptr);
        leptris_arena_destroy(a);
    }

    /* No parking under hooks: one hooked allocation AND one hooked
     * free per cycle, balanced to the byte. */
    EXPECT_EQ(g_recycle_allocs, 32);
    EXPECT_EQ(g_recycle_frees, 32);

    leptris_arena_retain_drain();
    leptris_set_memory_management_functions(NULL, NULL);
}

// The cache parks at most ARENA_TLS_SPANS spans; distinct sizes
// make reuse identity unambiguous — recreate after a bulk destroy
// and at least the parked bound's worth of addresses repeat.
TEST(ArenaSmallRecycle, BulkDestroyParksUpToTheBound) {
    leptris_arena_retain_drain();

    enum { N = 12 };
    LeptrisArena* keep[N];
    void* bases[N];
    for (int i = 0; i < N; i++) {
        keep[i] = leptris_arena_create(64u * 1024u + (size_t)i * 8192u);
        ASSERT_NE(keep[i], nullptr);
        bases[i] = leptris_arena_base(keep[i]);
    }
    for (int i = 0; i < N; i++) leptris_arena_destroy(keep[i]);

    int reused = 0;
    for (int i = 0; i < N; i++) {
        size_t sz = 64u * 1024u + (size_t)i * 8192u;
        LeptrisArena* a = leptris_arena_create(sz);
        ASSERT_NE(a, nullptr);
        if (leptris_arena_base(a) == bases[i]) reused++;
        leptris_arena_destroy(a);
    }
    /* 8 parked spans reuse their address; the rest went back to
     * libc (which may or may not hand the same block back — the
     * parked count is the stable lower bound). */
    EXPECT_GE(reused, 8);
    EXPECT_LE(reused, N);

    leptris_arena_retain_drain();
}

