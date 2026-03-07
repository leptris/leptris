/* test_cow_path_copy.c - Phase 2.4: Copy-on-Write Modifications Tests
 * Copyright (c) 2025, Ribose Inc.
 *
 * Tests for Copy-on-Write path copying:
 * - Thawing frozen nodes creates mutable copies
 * - Original tree remains frozen after modification
 * - Copied subtree is mutable
 */

#include <taurus.h>
#include <stdio.h>
#include <string.h>

/* Include internal headers for testing */
#include "dom/node.h"
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
 * Test 1: Frozen nodes remain frozen after thaw attempt
 * ============================================================================ */

static int test_original_stays_frozen(void) {
    const char* xml = "<root><child>text</child></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    TaurusElement child = taurus_element_child(root, 0);
    TEST_ASSERT(child != NULL, "Child element is NULL");

    /* Nodes should be frozen after parsing */
    TaurusElementNode* child_node = (TaurusElementNode*)child;
    TEST_ASSERT(child_node->base.frozen == 1, "Child should be frozen after parsing");

    /* Thaw should create a copy, not modify original */
    TaurusElement thawed = (TaurusElement)taurus_node_thaw((TaurusNode*)child);
    TEST_ASSERT(thawed != NULL, "Thaw should succeed");
    TEST_ASSERT(thawed != child, "Thaw should create a new node");

    /* Original should still be frozen */
    TEST_ASSERT(child_node->base.frozen == 1, "Original should still be frozen");

    /* Thawed copy should be mutable */
    TaurusElementNode* thawed_node = (TaurusElementNode*)thawed;
    TEST_ASSERT(thawed_node->base.frozen == 0, "Thawed copy should be mutable");

    taurus_document_free(doc);
    TEST_PASS("test_original_stays_frozen");
    return 1;
}

/* ============================================================================
 * Test 2: Thawed copy is mutable
 * ============================================================================ */

static int test_thawed_is_mutable(void) {
    const char* xml = "<root><child>text</child></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    TaurusElement child = taurus_element_child(root, 0);
    TEST_ASSERT(child != NULL, "Child element is NULL");

    /* Thaw the child */
    TaurusElement thawed = (TaurusElement)taurus_node_thaw((TaurusNode*)child);
    TEST_ASSERT(thawed != NULL, "Thaw should succeed");

    TaurusElementNode* thawed_node = (TaurusElementNode*)thawed;
    TEST_ASSERT(thawed_node->base.frozen == 0, "Thawed node should be mutable");

    /* Create a new element and append to thawed node */
    TaurusElement new_child = taurus_element_create(doc, "grandchild");
    TEST_ASSERT(new_child != NULL, "Grandchild creation failed");

    /* This should succeed because thawed node is mutable */
    TaurusStatus status = taurus_element_append_child(thawed, new_child);
    TEST_ASSERT(status == TAURUS_OK, "Append to thawed node should succeed");

    /* Version should be incremented */
    TEST_ASSERT(thawed_node->base.version > 0, "Thawed node version should increment");

    taurus_document_free(doc);
    TEST_PASS("test_thawed_is_mutable");
    return 1;
}

/* ============================================================================
 * Test 3: Thawing non-frozen node returns same node
 * ============================================================================ */

static int test_thaw_mutable_returns_self(void) {
    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);

    /* Create a new (mutable) element */
    TaurusElement new_elem = taurus_element_create(doc, "new");
    TEST_ASSERT(new_elem != NULL, "Element creation failed");

    /* New elements are mutable */
    TaurusElementNode* new_node = (TaurusElementNode*)new_elem;
    TEST_ASSERT(new_node->base.frozen == 0, "New element should be mutable");

    /* Thawing a mutable node should return the same node */
    TaurusElement thawed = (TaurusElement)taurus_node_thaw((TaurusNode*)new_elem);
    TEST_ASSERT(thawed == new_elem, "Thawing mutable node should return same node");

    taurus_document_free(doc);
    TEST_PASS("test_thaw_mutable_returns_self");
    return 1;
}

/* ============================================================================
 * Test 4: Thawed copy has same name as original
 * ============================================================================ */

static int test_thawed_preserves_name(void) {
    const char* xml = "<root><child>text</child></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_child(root, 0);
    TEST_ASSERT(child != NULL, "Child element is NULL");

    /* Get original name */
    const char* orig_name = taurus_element_name(child);

    /* Thaw the child */
    TaurusElement thawed = (TaurusElement)taurus_node_thaw((TaurusNode*)child);
    TEST_ASSERT(thawed != NULL, "Thaw should succeed");

    /* Thawed copy should have same name */
    const char* thawed_name = taurus_element_name(thawed);
    TEST_ASSERT(strcmp(orig_name, thawed_name) == 0, "Thawed copy should preserve name");

    taurus_document_free(doc);
    TEST_PASS("test_thawed_preserves_name");
    return 1;
}

/* ============================================================================
 * Test 5: Thawing NULL returns NULL
 * ============================================================================ */

static int test_thaw_null_returns_null(void) {
    TaurusElement thawed = (TaurusElement)taurus_node_thaw(NULL);
    TEST_ASSERT(thawed == NULL, "Thawing NULL should return NULL");

    TEST_PASS("test_thaw_null_returns_null");
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
    {"test_original_stays_frozen", test_original_stays_frozen},
    {"test_thawed_is_mutable", test_thawed_is_mutable},
    {"test_thaw_mutable_returns_self", test_thaw_mutable_returns_self},
    {"test_thawed_preserves_name", test_thawed_preserves_name},
    {"test_thaw_null_returns_null", test_thaw_null_returns_null},
    {NULL, NULL}
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║   COW Phase 2.4: Copy-on-Write Modifications Test Suite    ║\n");
    printf("║   Testing path copying and frozen node semantics           ║\n");
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
