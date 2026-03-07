/* test_cow_frozen.c - Phase 2.1: Frozen Node System Tests
 * Copyright (c) 2025, Ribose Inc.
 *
 * Tests for Copy-on-Write frozen node system:
 * - Nodes are frozen after parsing
 * - Frozen nodes cannot be modified (in Phase 2.4)
 * - Thawing returns mutable copy (in Phase 2.4)
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

/* ============================================================================
 * Test 1: Nodes are frozen after parsing
 * ============================================================================ */

static int test_frozen_after_parse(void) {
    const char* xml = "<root><child1/><child2/><child3/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Check that root is frozen */
    int is_frozen = taurus_node_is_frozen((TaurusNode*)root);
    TEST_ASSERT(is_frozen == 1, "Root should be frozen after parsing");

    taurus_document_free(doc);
    TEST_PASS("test_frozen_after_parse");
    return 1;
}

/* ============================================================================
 * Test 2: All nodes in tree are frozen
 * ============================================================================ */

static int test_frozen_tree(void) {
    const char* xml = "<root><a><b><c/></b></a><d/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Check that root is frozen */
    TEST_ASSERT(taurus_node_is_frozen((TaurusNode*)root) == 1, "Root should be frozen");

    /* Check that children are frozen */
    TaurusElement a = taurus_element_child(root, 0);
    TEST_ASSERT(a != NULL, "Element 'a' should exist");
    TEST_ASSERT(taurus_node_is_frozen(a) == 1, "Element 'a' should be frozen");

    TaurusElement b = taurus_element_child(a, 0);
    TEST_ASSERT(b != NULL, "Element 'b' should exist");
    TEST_ASSERT(taurus_node_is_frozen(b) == 1, "Element 'b' should be frozen");

    TaurusElement c = taurus_element_child(b, 0);
    TEST_ASSERT(c != NULL, "Element 'c' should exist");
    TEST_ASSERT(taurus_node_is_frozen(c) == 1, "Element 'c' should be frozen");

    TaurusElement d = taurus_element_child(root, 1);
    TEST_ASSERT(d != NULL, "Element 'd' should exist");
    TEST_ASSERT(taurus_node_is_frozen(d) == 1, "Element 'd' should be frozen");

    taurus_document_free(doc);
    TEST_PASS("test_frozen_tree");
    return 1;
}

/* ============================================================================
 * Test 3: Newly created nodes are not frozen
 * ============================================================================ */

static int test_mutable_on_create(void) {
    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Create new element */
    TaurusElement new_elem = taurus_element_create(doc, "new");
    TEST_ASSERT(new_elem != NULL, "New element creation failed");

    /* Check that new element is NOT frozen */
    int is_frozen = taurus_node_is_frozen((TaurusNode*)new_elem);
    TEST_ASSERT(is_frozen == 0, "Newly created element should NOT be frozen");

    taurus_document_free(doc);
    TEST_PASS("test_mutable_on_create");
    return 1;
}

/* ============================================================================
 * Test 4: Thaw creates mutable copy (Phase 2.4 COW implemented)
 * ============================================================================ */

static int test_thaw_creates_mutable_copy(void) {
    const char* xml = "<root><child/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Root should be frozen after parsing */
    int is_frozen = taurus_node_is_frozen((TaurusNode*)root);
    TEST_ASSERT(is_frozen == 1, "Root should be frozen after parsing");

    /* Thaw frozen node - should return mutable copy in Phase 2.4 */
    TaurusElement thawed = (TaurusElement)taurus_node_thaw((TaurusNode*)root);

    /* Phase 2.4: COW implemented, thaw returns mutable copy */
    TEST_ASSERT(thawed != NULL, "Thaw should return mutable copy");
    TEST_ASSERT(thawed != root, "Thaw should create a new node");

    /* Thawed copy should be mutable */
    int thawed_is_frozen = taurus_node_is_frozen(thawed);
    TEST_ASSERT(thawed_is_frozen == 0, "Thawed copy should be mutable");

    /* Original should still be frozen */
    int root_is_frozen = taurus_node_is_frozen((TaurusNode*)root);
    TEST_ASSERT(root_is_frozen == 1, "Original should still be frozen");

    taurus_document_free(doc);
    TEST_PASS("test_thaw_creates_mutable_copy");
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
    {"test_frozen_after_parse", test_frozen_after_parse},
    {"test_frozen_tree", test_frozen_tree},
    {"test_mutable_on_create", test_mutable_on_create},
    {"test_thaw_creates_mutable_copy", test_thaw_creates_mutable_copy},
    {NULL, NULL}
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║   COW Phases 2.1 & 2.4: Frozen Node & Copy-on-Write Tests  ║\n");
    printf("║   Testing frozen nodes and path copying                    ║\n");
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
