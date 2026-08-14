// test/memory/test_pool_arena.cpp — arena-backed pool mode specs
// (TODO 183 Phase 2).
//
// The pool API must behave identically whether backed by pages or by
// one contiguous arena — except exhaustion is a hard NULL (never a
// scattered fallback malloc) and node+content is always contiguous.

#include <gtest/gtest.h>

extern "C" {
#include "pool.h"
#include "arena.h"
}

#include <cstring>
#include <string>
#include <vector>

namespace {

TEST(PoolArenaMode, CreateIsArenaBacked) {
    TaurusArena* arena = taurus_arena_create(4096);
    ASSERT_NE(arena, nullptr);
    TaurusMemoryPool* pool = taurus_pool_create_arena_backed(arena, 1);
    ASSERT_NE(pool, nullptr);
    EXPECT_EQ(taurus_pool_is_arena_backed(pool), 1);
    /* Base for compact-pointer decoding is the arena span. */
    EXPECT_EQ(taurus_pool_get_base(pool), taurus_arena_base(arena));
    taurus_pool_destroy(pool);  /* owns_arena=1 → arena freed too */
}

TEST(PoolArenaMode, PageModePoolIsNotArenaBacked) {
    TaurusMemoryPool* pool = taurus_pool_create();
    ASSERT_NE(pool, nullptr);
    EXPECT_EQ(taurus_pool_is_arena_backed(pool), 0);
    taurus_pool_destroy(pool);
}

TEST(PoolArenaMode, AllocsLieWithinArenaSpan) {
    TaurusArena* arena = taurus_arena_create(8192);
    ASSERT_NE(arena, nullptr);
    TaurusMemoryPool* pool = taurus_pool_create_arena_backed(arena, 1);
    ASSERT_NE(pool, nullptr);
    char* base = (char*)taurus_arena_base(arena);
    for (int i = 0; i < 200; i++) {
        void* p = taurus_pool_alloc(pool, 1 + (size_t)(i % 31));
        ASSERT_NE(p, nullptr) << "i=" << i;
        EXPECT_GE((char*)p, base);
        EXPECT_LT((char*)p, base + 8192);
        EXPECT_EQ((uintptr_t)p % 8, 0u);
    }
    taurus_pool_destroy(pool);
}

TEST(PoolArenaMode, ExhaustionExtendsBeyondSpan) {
    /* Post-Phase-3 contract: the POOL never hard-fails (mutation APIs
     * like element_create can't start returning NULL). Overflow beyond
     * the sized span is satisfied by a tracked extension block — which
     * must lie OUTSIDE [base, base+size): the span stays contiguous
     * and exclusively holds parse-time allocations. */
    TaurusArena* arena = taurus_arena_create(128);
    ASSERT_NE(arena, nullptr);
    TaurusMemoryPool* pool = taurus_pool_create_arena_backed(arena, 1);
    ASSERT_NE(pool, nullptr);
    char* base = (char*)taurus_arena_base(arena);
    /* In-span allocations stay in-span. */
    char* in_span = (char*)taurus_pool_alloc(pool, 120);
    ASSERT_NE(in_span, nullptr);
    EXPECT_GE(in_span, base);
    EXPECT_LT(in_span, base + 128);
    /* Overflow succeeds but comes from an extension block. */
    char* ext = (char*)taurus_pool_alloc(pool, 64);
    ASSERT_NE(ext, nullptr);
    EXPECT_TRUE(ext < base || ext >= base + 128)
        << "overflow allocation must not land inside the arena span";
    /* Span accounting unchanged by the extension. */
    EXPECT_LE(taurus_pool_used_size(pool), 128u);
    taurus_pool_destroy(pool);  /* extension freed with the pool */
}

TEST(PoolArenaMode, MutationGrowthNeverFails) {
    /* The motivating case: parse a tiny doc, then build a large tree
     * through the mutation API — the page pool always allowed this,
     * so the arena-backed pool must too. */
    TaurusArena* arena = taurus_arena_create(64);
    ASSERT_NE(arena, nullptr);
    TaurusMemoryPool* pool = taurus_pool_create_arena_backed(arena, 1);
    ASSERT_NE(pool, nullptr);
    for (int i = 0; i < 1000; i++) {
        void* p = taurus_pool_alloc(pool, 72);
        ASSERT_NE(p, nullptr) << "i=" << i;
    }
    taurus_pool_destroy(pool);
}

TEST(PoolArenaMode, StrdupRoutesThroughArena) {
    TaurusArena* arena = taurus_arena_create(1024);
    ASSERT_NE(arena, nullptr);
    TaurusMemoryPool* pool = taurus_pool_create_arena_backed(arena, 1);
    ASSERT_NE(pool, nullptr);
    char* base = (char*)taurus_arena_base(arena);
    char* s = taurus_pool_strdup(pool, "hello world");
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "hello world");
    EXPECT_GE(s, base);
    EXPECT_LT(s, base + 1024);
    taurus_pool_destroy(pool);
}

TEST(PoolArenaMode, NodeWithContentAlwaysContiguous) {
    /* Oversized content must stay inside the span in arena mode —
     * the page-mode pool splits it into a separate oversized malloc,
     * which is exactly what broke cross-pointer tree edges. */
    TaurusArena* arena = taurus_arena_create(64 * 1024);
    ASSERT_NE(arena, nullptr);
    TaurusMemoryPool* pool = taurus_pool_create_arena_backed(arena, 1);
    ASSERT_NE(pool, nullptr);
    char* base = (char*)taurus_arena_base(arena);

    char* content = nullptr;
    void* node = taurus_pool_alloc_node_with_content(
        pool, 40, 40 * 1024, &content);  /* 40 KB content >> any page */
    ASSERT_NE(node, nullptr);
    ASSERT_NE(content, nullptr);
    EXPECT_GE(content, base);
    EXPECT_LT(content, base + 64 * 1024);
    EXPECT_GE(content, (char*)node + 40);
    /* The whole content must be writable in-span. */
    memset(content, 'z', 40 * 1024);
    content[40 * 1024 - 1] = '\0';
    taurus_pool_destroy(pool);
}

TEST(PoolArenaMode, StringInterningWorksOnArena) {
    TaurusArena* arena = taurus_arena_create(64 * 1024);
    ASSERT_NE(arena, nullptr);
    TaurusMemoryPool* pool = taurus_pool_create_arena_backed(arena, 1);
    ASSERT_NE(pool, nullptr);
    StringHashTable* table = taurus_hash_table_create(pool, 16);
    ASSERT_NE(table, nullptr);
    pool->string_cache = table;  /* intern dedup keys off this field */
    TaurusStringView sv = taurus_sv_from_ptr("chapter", 7);
    char* a = taurus_pool_intern_string(pool, &sv);
    ASSERT_NE(a, nullptr);
    EXPECT_STREQ(a, "chapter");
    /* Second intern of the same view dedups to the same pointer. */
    char* b = taurus_pool_intern_string(pool, &sv);
    EXPECT_EQ(a, b);
    taurus_pool_destroy(pool);
}

TEST(PoolArenaMode, StatsReflectArena) {
    TaurusArena* arena = taurus_arena_create(2048);
    ASSERT_NE(arena, nullptr);
    TaurusMemoryPool* pool = taurus_pool_create_arena_backed(arena, 1);
    ASSERT_NE(pool, nullptr);
    EXPECT_EQ(taurus_pool_total_size(pool), 2048u);
    EXPECT_EQ(taurus_pool_used_size(pool), 0u);
    EXPECT_EQ(taurus_pool_page_count(pool), 1u);
    ASSERT_NE(taurus_pool_alloc(pool, 100), nullptr);
    EXPECT_EQ(taurus_pool_used_size(pool), 104u);  /* aligned to 8 */
    taurus_pool_destroy(pool);
}

TEST(PoolArenaMode, BorrowedArenaOutlivesPool) {
    /* owns_arena=0: pool destroy must not free the caller's arena. */
    TaurusArena* arena = taurus_arena_create(512);
    ASSERT_NE(arena, nullptr);
    {
        TaurusMemoryPool* pool = taurus_pool_create_arena_backed(arena, 0);
        ASSERT_NE(pool, nullptr);
        ASSERT_NE(taurus_pool_alloc(pool, 64), nullptr);
        taurus_pool_destroy(pool);
    }
    /* Arena still alive and usable. */
    EXPECT_NE(taurus_arena_alloc(arena, 64), nullptr);
    taurus_arena_destroy(arena);
}

}  // namespace
