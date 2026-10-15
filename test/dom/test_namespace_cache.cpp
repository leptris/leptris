#include <gtest/gtest.h>
#include <cstring>

#include "leptris.h"
extern "C" {
#include "element.h"
#include "pool.h"
#include "arena.h"
}

namespace {

class NamespaceCacheInitialization : public ::testing::Test {
protected:
    LeptrisMemoryPool* pool = nullptr;
    LeptrisElement element = nullptr;

    void SetUp() override {
        LeptrisArena* arena = leptris_arena_create(4096);
        ASSERT_NE(arena, nullptr);
        pool = leptris_pool_create_arena_backed(arena, 1);
        if (!pool) leptris_arena_destroy(arena);
        ASSERT_NE(pool, nullptr);
        // Pool allocations are deliberately not zeroed. Poison the remaining
        // arena so a new cache cannot pass by inheriting fresh OS zero pages.
        std::memset(arena->base + arena->used, 0xa5, arena->size - arena->used);
        element = static_cast<LeptrisElement>(
            leptris_pool_alloc(pool, sizeof(*element)));
        ASSERT_NE(element, nullptr);
        std::memset(element, 0, sizeof(*element));
    }

    void TearDown() override {
        leptris_pool_destroy(pool);
    }

    void ExpectEmptyAuxiliaryState(const leptris_ns_cache* cache) {
        ASSERT_NE(cache, nullptr);
        EXPECT_EQ(cache->declarations, nullptr);
        EXPECT_EQ(cache->raw_attrs, nullptr);
        EXPECT_EQ(cache->doc_next, nullptr);
        EXPECT_EQ(cache->prefix_heap, 0);
        EXPECT_EQ(cache->uri_heap, 0);
        EXPECT_EQ(cache->child_gen, 0u);
        EXPECT_EQ(cache->child_cached_gen, 0u);
        EXPECT_EQ(cache->child_idx, 0u);
        EXPECT_EQ(cache->child_node, nullptr);
        // The public raw-attribute read must see no phantom journal entries.
        // Avoid following the poisoned pointer if the assertion above failed.
        if (!cache->raw_attrs) {
            EXPECT_EQ(leptris_element_attributes_raw(element, nullptr, nullptr, 0), 0u);
        }
    }
};

TEST_F(NamespaceCacheInitialization, PrefixSetterStartsWithEmptyAuxiliaryState) {
    char prefix[] = "p";
    leptris_elem_set_prefix(element, prefix, pool);
    const auto* cache = elem_get_ns_cache(element);
    ASSERT_NE(cache, nullptr);
    EXPECT_EQ(cache->prefix, prefix);
    EXPECT_EQ(cache->namespace_uri, nullptr);
    ExpectEmptyAuxiliaryState(cache);
}

TEST_F(NamespaceCacheInitialization, NamespaceSetterStartsWithEmptyAuxiliaryState) {
    char uri[] = "urn:example";
    leptris_elem_set_ns_uri(element, uri, pool);
    const auto* cache = elem_get_ns_cache(element);
    ASSERT_NE(cache, nullptr);
    EXPECT_EQ(cache->prefix, nullptr);
    EXPECT_EQ(cache->namespace_uri, uri);
    ExpectEmptyAuxiliaryState(cache);
}

TEST_F(NamespaceCacheInitialization, EnsureStartsEmptyAndPreservesExistingCache) {
    auto* cache = leptris_elem_cache_ensure(element, pool);
    ASSERT_NE(cache, nullptr);
    EXPECT_EQ(cache->prefix, nullptr);
    EXPECT_EQ(cache->namespace_uri, nullptr);
    ExpectEmptyAuxiliaryState(cache);

    // Reusing a cache must preserve the state its consumers already stored.
    cache->child_gen = 7;
    EXPECT_EQ(leptris_elem_cache_ensure(element, pool), cache);
    EXPECT_EQ(cache->child_gen, 7u);
}

} // namespace
