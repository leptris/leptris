/* test_cow_versioning.c - Phase 2.2: Node Versioning/Sharing Tests
 * Copyright (c) 2025, Ribose Inc.
 *
 * Tests for Copy-on-Write node versioning and reference counting:
 * - Version tracking on node modifications
 * - Reference counting for shared nodes
 * - Version increment on modifications
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

/* Access internal version field for testing */
static unsigned int test_get_node_version(TaurusElement elem) {
    if (!elem) return 0;
    TaurusElementNode* node = (TaurusElementNode*)elem;
    return node->base.version;
}

/* Access internal ref_count field for testing */
static int test_get_node_ref_count(TaurusElement elem) {
    if (!elem) return 0;
    TaurusElementNode* node = (TaurusElementNode*)elem;
    return node->base.ref_count;
}

/* ============================================================================
 * Test 1: Version starts at 0 for new nodes
 * ============================================================================ */

static int test_initial_version_zero(void) {
    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* New nodes should have version 0 */
    unsigned int version = test_get_node_version(root);
    TEST_ASSERT(version == 0, "Initial version should be 0");

    taurus_document_free(doc);
    TEST_PASS("test_initial_version_zero");
    return 1;
}

/* ============================================================================
 * Test 2: Ref count starts at 0 for new nodes
 * ============================================================================ */

static int test_initial_ref_count_zero(void) {
    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* New nodes should have ref_count 0 */
    int ref_count = test_get_node_ref_count(root);
    TEST_ASSERT(ref_count == 0, "Initial ref_count should be 0");

    taurus_document_free(doc);
    TEST_PASS("test_initial_ref_count_zero");
    return 1;
}

/* ============================================================================
 * Test 3: Newly created elements have version 0
 * ============================================================================ */

static int test_newly_created_has_version_zero(void) {
    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);

    /* Create new element */
    TaurusElement child = taurus_element_create(doc, "child");
    TEST_ASSERT(child != NULL, "Child creation failed");

    /* Newly created element should have version 0 */
    unsigned int version = test_get_node_version(child);
    TEST_ASSERT(version == 0, "Newly created element should have version 0");

    taurus_document_free(doc);
    TEST_PASS("test_newly_created_has_version_zero");
    return 1;
}

/* ============================================================================
 * Test 4: Version increment on append_child
 * ============================================================================ */

static int test_version_increment_on_append(void) {
    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Get initial version */
    unsigned int initial_version = test_get_node_version(root);
    TEST_ASSERT(initial_version == 0, "Initial version should be 0");

    /* Append child - this should increment parent's version */
    TaurusElement child = taurus_element_create(doc, "child");
    taurus_element_append_child(root, child);

    /* Version should be incremented */
    unsigned int new_version = test_get_node_version(root);
    TEST_ASSERT(new_version > initial_version, "Version should increment after append_child");

    taurus_document_free(doc);
    TEST_PASS("test_version_increment_on_append");
    return 1;
}

/* ============================================================================
 * Test 5: Reference counting - increment and decrement
 * ============================================================================ */

static int test_ref_count_increments(void) {
    const char* xml = "<root><child/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    TaurusElement child = taurus_element_child(root, 0);
    TEST_ASSERT(child != NULL, "Child element is NULL");

    /* Get internal node for ref counting tests */
    TaurusElementNode* child_node = (TaurusElementNode*)child;

    /* Initial ref_count should be 0 (nodes in tree don't use ref counting) */
    int initial_count = child_node->base.ref_count;
    TEST_ASSERT(initial_count == 0, "Initial ref_count should be 0");

    /* Test increment */
    taurus_node_ref((TaurusNode*)child_node);
    TEST_ASSERT(child_node->base.ref_count == 1, "ref_count should be 1 after increment");

    /* Test decrement */
    int should_free = taurus_node_unref((TaurusNode*)child_node);
    TEST_ASSERT(child_node->base.ref_count == 0, "ref_count should be 0 after decrement");
    TEST_ASSERT(should_free == 1, "should_free should be 1 when ref_count reaches 0");

    /* Create a standalone node to test tree-ownership case */
    TaurusElement standalone = taurus_element_create(doc, "standalone");
    TEST_ASSERT(standalone != NULL, "Standalone element creation failed");

    TaurusElementNode* standalone_node = (TaurusElementNode*)standalone;

    /* Standalone nodes start with ref_count 0 */
    TEST_ASSERT(standalone_node->base.ref_count == 0, "Standalone node ref_count should be 0");

    /* Add a reference and verify */
    taurus_node_ref((TaurusNode*)standalone_node);
    TEST_ASSERT(standalone_node->base.ref_count == 1, "Standalone node ref_count should be 1");

    taurus_document_free(doc);
    TEST_PASS("test_ref_count_increments");
    return 1;
}

/* ============================================================================
 * Test 6: Version tracking after multiple modifications
 * ============================================================================ */

static int test_version_tracking_multiple_modifications(void) {
    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    unsigned int initial_version = test_get_node_version(root);

    /* Perform multiple modifications */
    for (int i = 0; i < 5; i++) {
        char name[50];
        snprintf(name, sizeof(name), "child%d", i);
        TaurusElement child = taurus_element_create(doc, name);
        taurus_element_append_child(root, child);
    }

    /* Version should be higher after 5 modifications */
    unsigned int final_version = test_get_node_version(root);
    TEST_ASSERT(final_version > initial_version, "Version should increase after modifications");

    taurus_document_free(doc);
    TEST_PASS("test_version_tracking_multiple_modifications");
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
    {"test_initial_version_zero", test_initial_version_zero},
    {"test_initial_ref_count_zero", test_initial_ref_count_zero},
    {"test_newly_created_has_version_zero", test_newly_created_has_version_zero},
    {"test_version_increment_on_append", test_version_increment_on_append},
    {"test_ref_count_increments", test_ref_count_increments},
    {"test_version_tracking_multiple_modifications", test_version_tracking_multiple_modifications},
    {NULL, NULL}
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║   COW Phase 2.2: Node Versioning/Sharing Test Suite        ║\n");
    printf("║   Testing version tracking and reference counting          ║\n");
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
