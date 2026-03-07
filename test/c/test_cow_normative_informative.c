/* test_cow_normative_informative.c - Phase 2.3: Normative/Informative Split Tests
 * Copyright (c) 2025, Ribose Inc.
 *
 * Tests for Copy-on-Write normative/informative architecture:
 * - Linked list = NORMATIVE (source of truth)
 * - Array cache = INFORMATIVE (optional optimization)
 * - Modifications invalidate cache (O(1)) instead of rebuilding (O(n))
 * - Cache rebuilt lazily on indexed access
 */

#include <taurus.h>
#include <stdio.h>
#include <string.h>

/* Include internal headers for testing */
#include "dom/element.h"

/* ============================================================================
 * Test Framework
 * ============================================================================ */

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s\n", message); \
            return 0; \
        } \
    } while(0)

#define TEST_PASS(name) \
    do { \
        printf("✓ PASS: %s\n", name); \
    } while(0)

/* Access internal children_cache_valid field for testing */
static unsigned char test_get_children_cache_valid(TaurusElement elem) {
    if (!elem) return 0;
    TaurusElementNode* node = (TaurusElementNode*)elem;
    return node->children_cache_valid;
}

/* ============================================================================
 * Test 1: Modifications invalidate cache
 * ============================================================================ */

static int test_invalidate_on_append(void) {
    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Create new children */
    TaurusElement child1 = taurus_element_create(doc, "child1");
    TaurusElement child2 = taurus_element_create(doc, "child2");

    /* Append first child - cache should be invalid */
    taurus_element_append_child(root, child1);
    TEST_ASSERT(test_get_children_cache_valid(root) == 0, "Cache should be invalid after append");

    /* Append second child - cache should still be invalid */
    taurus_element_append_child(root, child2);
    TEST_ASSERT(test_get_children_cache_valid(root) == 0, "Cache should still be invalid after second append");

    /* Access by index - this should trigger lazy rebuild */
    TaurusElement retrieved = taurus_element_child(root, 0);
    TEST_ASSERT(retrieved != NULL, "Should retrieve first child");
    TEST_ASSERT(test_get_children_cache_valid(root) == 1, "Cache should be valid after indexed access");

    taurus_document_free(doc);
    TEST_PASS("test_invalidate_on_append");
    return 1;
}

/* ============================================================================
 * Test 2: Multiple modifications don't rebuild cache
 * ============================================================================ */

static int test_no_rebuild_on_multiple_modifies(void) {
    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Create and append many children */
    for (int i = 0; i < 10; i++) {
        char name[50];
        snprintf(name, sizeof(name), "child%d", i);
        TaurusElement child = taurus_element_create(doc, name);
        taurus_element_append_child(root, child);
        /* Cache should be invalid after each append */
        TEST_ASSERT(test_get_children_cache_valid(root) == 0,
                   "Cache should be invalid after each append");
    }

    /* Now access by index - should rebuild ONCE */
    TaurusElement child5 = taurus_element_child(root, 5);
    TEST_ASSERT(child5 != NULL, "Should retrieve 5th child");
    TEST_ASSERT(test_get_children_cache_valid(root) == 1, "Cache should be valid after first access");

    /* Access again - cache should still be valid */
    TaurusElement child3 = taurus_element_child(root, 3);
    TEST_ASSERT(child3 != NULL, "Should retrieve 3rd child");
    TEST_ASSERT(test_get_children_cache_valid(root) == 1, "Cache should still be valid");

    taurus_document_free(doc);
    TEST_PASS("test_no_rebuild_on_multiple_modifies");
    return 1;
}

/* ============================================================================
 * Test 3: Insert operations invalidate cache
 * ============================================================================ */

static int test_invalidate_on_insert(void) {
    const char* xml = "<root><a/><c/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Access by index to build cache */
    TaurusElement a = taurus_element_child(root, 0);
    TEST_ASSERT(a != NULL, "Element 'a' should exist");
    TEST_ASSERT(test_get_children_cache_valid(root) == 1, "Cache should be valid after indexed access");

    /* Create new element */
    TaurusElement b = taurus_element_create(doc, "b");
    TaurusElement a_elem = (TaurusElement)a;

    /* Insert after 'a' - should invalidate cache */
    taurus_element_insert_after(a, b);
    TEST_ASSERT(test_get_children_cache_valid(root) == 0, "Cache should be invalid after insert");

    /* Verify all children are accessible */
    size_t count = taurus_element_child_count(root);
    TEST_ASSERT(count == 3, "Should have 3 children");

    taurus_document_free(doc);
    TEST_PASS("test_invalidate_on_insert");
    return 1;
}

/* ============================================================================
 * Test 4: Remove operations invalidate cache
 * ============================================================================ */

static int test_invalidate_on_remove(void) {
    const char* xml = "<root><a/><b/><c/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Access by index to build cache */
    TaurusElement b = taurus_element_child(root, 1);
    TEST_ASSERT(b != NULL, "Element 'b' should exist");
    TEST_ASSERT(test_get_children_cache_valid(root) == 1, "Cache should be valid after indexed access");

    /* Remove middle element */
    taurus_element_remove_child(root, b);
    TEST_ASSERT(test_get_children_cache_valid(root) == 0, "Cache should be invalid after remove");

    /* Verify cache is rebuilt on next access */
    TaurusElement a = taurus_element_child(root, 0);
    TEST_ASSERT(a != NULL, "Element 'a' should still exist");
    TEST_ASSERT(test_get_children_cache_valid(root) == 1, "Cache should be valid after access");

    taurus_document_free(doc);
    TEST_PASS("test_invalidate_on_remove");
    return 1;
}

/* ============================================================================
 * Test 5: Set text invalidates cache
 * ============================================================================ */

static int test_invalidate_on_set_text(void) {
    const char* xml = "<root><a/><b/><c/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Access by index to build cache */
    TaurusElement a = taurus_element_child(root, 0);
    TEST_ASSERT(a != NULL, "Element 'a' should exist");
    TEST_ASSERT(test_get_children_cache_valid(root) == 1, "Cache should be valid after indexed access");

    /* Set text - replaces all children with a single text node */
    taurus_element_set_text(root, "new text");
    TEST_ASSERT(test_get_children_cache_valid(root) == 0, "Cache should be invalid after set_text");

    taurus_document_free(doc);
    TEST_PASS("test_invalidate_on_set_text");
    return 1;
}

/* ============================================================================
 * Test 6: Performance - O(1) invalidation vs O(n) rebuild
 * ============================================================================ */

static int test_performance_invalidation(void) {
    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Add many children - each is O(1) cache invalidation */
    for (int i = 0; i < 100; i++) {
        char name[50];
        snprintf(name, sizeof(name), "child%d", i);
        TaurusElement child = taurus_element_create(doc, name);
        taurus_element_append_child(root, child);
    }

    /* Verify all children were added */
    size_t count = taurus_element_child_count(root);
    TEST_ASSERT(count == 100, "Expected 100 children");

    /* With old architecture: 100 array rebuilds = O(100 * n) = SLOW
     * With COW 2.3: 100 cache invalidations = O(100) = FAST */

    /* Access by index to rebuild cache once */
    TaurusElement child50 = taurus_element_child(root, 50);
    TEST_ASSERT(child50 != NULL, "Should retrieve 50th child");

    taurus_document_free(doc);
    TEST_PASS("test_performance_invalidation");
    return 1;
}

/* ============================================================================
 * Main
 * ============================================================================ */

typedef int (*test_func_t)(void);

struct test_case {
    const char* name;
    test_func_t func;
};

static struct test_case tests[] = {
    {"test_invalidate_on_append", test_invalidate_on_append},
    {"test_no_rebuild_on_multiple_modifies", test_no_rebuild_on_multiple_modifies},
    {"test_invalidate_on_insert", test_invalidate_on_insert},
    {"test_invalidate_on_remove", test_invalidate_on_remove},
    {"test_invalidate_on_set_text", test_invalidate_on_set_text},
    {"test_performance_invalidation", test_performance_invalidation},
    {NULL, NULL}
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║   COW Phase 2.3: Normative/Informative Split Test Suite        ║\n");
    printf("║   Testing cache invalidation and lazy rebuild                 ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    int passed = 0;
    int failed = 0;

    for (int i = 0; tests[i].name != NULL; i++) {
        printf("Running %s...\n", tests[i].name);
        if (tests[i].func()) {
            passed++;
        } else {
            failed++;
        }
    }

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                      Test Results                         ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Passed:  %d / %d\n", passed, passed + failed);
    printf("  Failed:  %d / %d\n", failed, passed + failed);

    if (failed == 0) {
        printf("\n  ✓ All tests passed!\n");
        return 0;
    } else {
        printf("\n  ✗ Some tests failed\n");
        return 1;
    }
}
