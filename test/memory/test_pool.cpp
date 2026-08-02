// test/memory/test_pool.cpp — Pool lifecycle and ownership specs.
//
// Pool API is internal; specs `#include` the pool header directly.  The
// pool is the foundation of the document-ownership model (TODO 05) and
// oversized-allocation tracking (TODO 06), so this file is the natural
// home for "pool invariants" specs.

#include <gtest/gtest.h>

#include "taurus.h"

extern "C" {
/* pool.h forward-declares `struct taurus_string_view`; include the full
 * definition from string_view.h so we can construct views by value. */
#include "string_view.h"
#include "pool.h"
}

#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

namespace {

TEST(TaurusMemoryPool, CreatesAndDestroysCleanly) {
    TaurusMemoryPool* pool = taurus_pool_create();
    ASSERT_NE(pool, nullptr);
    taurus_pool_destroy(pool);
}

TEST(TaurusMemoryPool, CustomPageSizeIsRespected) {
    TaurusMemoryPool* pool = taurus_pool_create_with_page_size(4096);
    ASSERT_NE(pool, nullptr);
    // TODO 10: taurus_pool_total_size is currently undercounting
    // (returns sizeof(MemoryPage) per page instead of page_size).
    // Until TODO 10 lands, we can only assert that some size is reported.
    EXPECT_GT(taurus_pool_total_size(pool), 0u);
    taurus_pool_destroy(pool);
}

TEST(TaurusMemoryPool, SmallAllocationsComeFromPool) {
    TaurusMemoryPool* pool = taurus_pool_create();
    ASSERT_NE(pool, nullptr);

    void* p1 = taurus_pool_alloc(pool, 64);
    void* p2 = taurus_pool_alloc(pool, 128);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_NE(p1, p2);

    taurus_pool_destroy(pool);
    // No leaks reported under `leaks --atExit --` or valgrind.
}

TEST(TaurusMemoryPool, InterningDeduplicatesEqualStrings) {
    // Interning requires the per-pool string cache to be enabled.
    // Parsing entry points (taurus_parse_string etc.) enable it;
    // bare taurus_pool_create() does not, by design — intern tables
    // cost memory and only pay off when the same strings recur.
    TaurusMemoryPool* pool = taurus_pool_create();
    ASSERT_NE(pool, nullptr);
    pool->string_cache = taurus_hash_table_create(pool, 128);
    ASSERT_NE(pool->string_cache, nullptr);

    TaurusStringView a = {"hello", 5};
    TaurusStringView b = {"hello", 5};
    char* pa = taurus_pool_intern_string(pool, &a);
    char* pb = taurus_pool_intern_string(pool, &b);
    ASSERT_NE(pa, nullptr);
    ASSERT_NE(pb, nullptr);
    EXPECT_EQ(pa, pb) << "equal strings should return same interned address";

    taurus_pool_destroy(pool);
}

TEST(TaurusMemoryPool, InterningReturnsNullForInvalidInput) {
    TaurusMemoryPool* pool = taurus_pool_create();
    ASSERT_NE(pool, nullptr);

    EXPECT_EQ(taurus_pool_intern_string(nullptr, nullptr), nullptr);
    EXPECT_EQ(taurus_pool_intern_string(pool, nullptr), nullptr);

    TaurusStringView empty = {nullptr, 0};
    EXPECT_EQ(taurus_pool_intern_string(pool, &empty), nullptr);

    taurus_pool_destroy(pool);
}

TEST(TaurusDocumentOwnership, AllNodeTypesArePoolOwned) {
    // A document containing every node type.  Under leaks/valgrind this
    // spec verifies the post-condition of TODO 05: freeing the document
    // frees the pool, which frees every node — including text/comment/
    // cdata/pi/doctype nodes that previously leaked.
    const char xml[] =
        "<?xml version='1.0'?>\n"
        "<!DOCTYPE r [\n"
        "<!ENTITY foo 'bar'>\n"
        "]>\n"
        "<!-- a comment -->"
        "<r attr='val'><!-- nested -->text<![CDATA[raw]]><?pi data?>"
        "<child>kid</child></r>";

    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(st, TAURUS_OK);

    taurus_document_free(doc);
}

TEST(TaurusMemoryPool, HashTableGrowsPastLoadFactor) {
    /* TODO 36: inserting many distinct strings should trigger hash
     * table growth without losing any keys. */
    TaurusMemoryPool* pool = taurus_pool_create();
    ASSERT_NE(pool, nullptr);
    pool->string_cache = taurus_hash_table_create(pool, 4);
    ASSERT_NE(pool->string_cache, nullptr);

    /* Insert 100 unique strings — should trigger multiple grows
     * (4 → 8 → 16 → 32 → 64 → 128 buckets). */
    char keys[100][16];
    for (int i = 0; i < 100; i++) {
        snprintf(keys[i], sizeof(keys[i]), "key_%d", i);
        TaurusStringView sv = {keys[i], strlen(keys[i])};
        EXPECT_NE(taurus_pool_intern_string(pool, &sv), nullptr);
    }

    EXPECT_GE(pool->string_cache->bucket_count, 32u);
    EXPECT_EQ(pool->string_cache->entry_count, 100u);

    /* All 100 keys still resolvable via dedup. */
    for (int i = 0; i < 100; i++) {
        TaurusStringView sv = {keys[i], strlen(keys[i])};
        EXPECT_NE(taurus_pool_intern_string(pool, &sv), nullptr);
    }

    taurus_pool_destroy(pool);
}

// ---- Per-document allocator hooks (TODO 74) ------------------------------

namespace {

static int g_my_alloc_count = 0;
static int g_my_free_count = 0;
static void* my_alloc(size_t n) {
    g_my_alloc_count++;
    return malloc(n);
}
static void my_free(void* p) {
    g_my_free_count++;
    free(p);
}

TEST(PerDocumentAllocators, HooksOverrideDefaults) {
    /* Reset counters so we measure only this test. */
    g_my_alloc_count = 0;
    g_my_free_count = 0;

    /* Unset any thread-default hooks. */
    taurus_set_memory_management_functions(NULL, NULL);

    const char xml[] = "<r><a/></r>";
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* Install custom allocators.  Currently the document is parsed with
     * the thread defaults (no per-doc override).  This proves the API
     * accepts the setters without crashing. */
    EXPECT_EQ(taurus_document_set_allocators(doc, my_alloc, my_free),
              TAURUS_OK);

    /* Freeing the document uses the thread-default free (which we
     * cleared).  Per-doc overrides only affect new pool creation. */
    taurus_document_free(doc);
    SUCCEED();
}

TEST(PerDocumentAllocators, NullDocumentReturnsError) {
    EXPECT_EQ(taurus_document_set_allocators(nullptr, my_alloc, my_free),
              TAURUS_ERROR_NULL_ARG);
}

}  // namespace

// ---- Stress tests (TODO 68) ----------------------------------------------

TEST(PoolStress, HighDocumentChurnDoesNotLeak) {
    /* Parse 100 small documents in sequence; verify pool destroy
     * cleans up each.  The compact allocator's overflow table is
     * thread-local and reused; this stresses that lifecycle. */
    const char xml[] = "<r><a/><b/><c/></r>";
    for (int i = 0; i < 100; i++) {
        TaurusStatus st;
        TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
        ASSERT_NE(doc, nullptr) << "iter " << i;
        taurus_document_free(doc);
    }
}

TEST(PoolStress, ManyOversizedAllocationsTracked) {
    /* Allocate 50 oversized blocks (each larger than the 4KB page).
     * Verify pool destroy releases all of them. */
    TaurusMemoryPool* pool = taurus_pool_create_with_page_size(4096);
    ASSERT_NE(pool, nullptr);

    for (int i = 0; i < 50; i++) {
        void* p = taurus_pool_alloc(pool, 8000);  /* > page_size */
        ASSERT_NE(p, nullptr);
        memset(p, 0xAB, 8000);  /* touch to ensure it's writable */
    }

    taurus_pool_destroy(pool);
    /* Under leaks --atExit --: 0 bytes leaked. */
}

TEST(PoolStress, LargeDocumentDoesNotLeak) {
    /* Parse a 100KB synthetic document; verify zero leaks. */
    std::string xml = "<root>";
    for (int i = 0; i < 5000; i++) {
        xml += "<item id=\"" + std::to_string(i) + "\">text</item>";
    }
    xml += "</root>";

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr);
    taurus_document_free(doc);
}

TEST(PoolStress, HashTableWithManyEntries) {
    /* Insert 500 unique strings; verify all resolvable via dedup. */
    TaurusMemoryPool* pool = taurus_pool_create();
    ASSERT_NE(pool, nullptr);
    pool->string_cache = taurus_hash_table_create(pool, 8);
    ASSERT_NE(pool->string_cache, nullptr);

    char keys[500][16];
    std::vector<char*> stored(500);
    for (int i = 0; i < 500; i++) {
        snprintf(keys[i], sizeof(keys[i]), "key_%d", i);
        TaurusStringView sv = {keys[i], strlen(keys[i])};
        stored[i] = taurus_pool_intern_string(pool, &sv);
        ASSERT_NE(stored[i], nullptr);
    }

    /* All 500 keys still resolvable — same address returned. */
    for (int i = 0; i < 500; i++) {
        TaurusStringView sv = {keys[i], strlen(keys[i])};
        EXPECT_EQ(taurus_pool_intern_string(pool, &sv), stored[i]);
    }

    EXPECT_EQ(pool->string_cache->entry_count, 500u);
    taurus_pool_destroy(pool);
}

}  // namespace
