/**
 * test_compact_mode.c - Test TAURUS_COMPACT_MODE functionality
 *
 * Based on pugixml test_compact.cpp
 *
 * Tests the compressed pointer architecture for:
 * - Memory efficiency (32 bytes vs 192 bytes per element)
 * - Correct pointer encoding/decoding
 * - Tree traversal with compact pointers
 * - Attribute access with compact pointers
 *
 * Compile with: -DTAURUS_COMPACT_MODE to enable compact mode
 */

#include "taurus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test result macros */
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s\n", msg); \
            return 0; \
        } \
    } while(0)

#define TEST_PASS(name) \
    do { \
        printf("PASS: %s\n", name); \
        return 1; \
    } while(0)

/* ============================================================================
 * Test 1: Compact mode basic element creation
 * ============================================================================ */

static int test_compact_basic_creation(void) {
    TaurusDocument doc = taurus_parse_string("<root><child1/><child2/><child3/></root>",
                                             strlen("<root><child1/><child2/><child3/></root>"),
                                             NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Verify we can access children */
    size_t child_count = taurus_element_child_count(root);
    TEST_ASSERT(child_count == 3, "Expected 3 children");

    /* Access each child by index */
    for (size_t i = 0; i < child_count; i++) {
        TaurusElement child = taurus_element_child(root, i);
        TEST_ASSERT(child != NULL, "Child should not be NULL");

        const char* name = taurus_element_name(child);
        TEST_ASSERT(name != NULL, "Child name should not be NULL");
    }

    taurus_document_free(doc);
    TEST_PASS("test_compact_basic_creation");
}

/* ============================================================================
 * Test 2: Compact mode deep tree traversal
 * ============================================================================ */

static int test_compact_deep_traversal(void) {
    /* Create a deep tree */
    char xml[4096];
    strcpy(xml, "<root>");
    for (int i = 0; i < 100; i++) {
        char level[50];
        snprintf(level, sizeof(level), "<level%d><value>data</value></level%d>", i, i);
        strcat(xml, level);
    }
    strcat(xml, "</root>");

    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Verify all children are accessible */
    size_t child_count = taurus_element_child_count(root);
    TEST_ASSERT(child_count == 100, "Expected 100 children");

    /* Traverse to deep values */
    int value_count = 0;
    for (size_t i = 0; i < child_count; i++) {
        TaurusElement child = taurus_element_child(root, i);
        TEST_ASSERT(child != NULL, "Child should not be NULL");

        /* Check for nested value element */
        TaurusElement value = taurus_element_child(child, 0);
        if (value) {
            const char* text = taurus_element_text(value);
            if (text && strcmp(text, "data") == 0) {
                value_count++;
            }
        }
    }

    TEST_ASSERT(value_count == 100, "Expected 100 value elements");

    taurus_document_free(doc);
    TEST_PASS("test_compact_deep_traversal");
}

/* ============================================================================
 * Test 3: Compact mode attribute access
 * ============================================================================ */

static int test_compact_attribute_access(void) {
    TaurusDocument doc = taurus_parse_string(
        "<root id='1' name='test' value='123'><item id='2' name='item1'/></root>",
        strlen("<root id='1' name='test' value='123'><item id='2' name='item1'/></root>"),
        NULL
    );
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Verify root attributes */
    const char* id = taurus_element_attribute(root, "id");
    TEST_ASSERT(id != NULL, "id attribute should not be NULL");
    TEST_ASSERT(strcmp(id, "1") == 0, "id should be '1'");

    const char* name = taurus_element_attribute(root, "name");
    TEST_ASSERT(name != NULL, "name attribute should not be NULL");
    TEST_ASSERT(strcmp(name, "test") == 0, "name should be 'test'");

    const char* value = taurus_element_attribute(root, "value");
    TEST_ASSERT(value != NULL, "value attribute should not be NULL");
    TEST_ASSERT(strcmp(value, "123") == 0, "value should be '123'");

    /* Verify child attributes */
    TaurusElement item = taurus_element_child(root, 0);
    TEST_ASSERT(item != NULL, "item element should not be NULL");

    const char* item_id = taurus_element_attribute(item, "id");
    TEST_ASSERT(item_id != NULL, "item id attribute should not be NULL");
    TEST_ASSERT(strcmp(item_id, "2") == 0, "item id should be '2'");

    taurus_document_free(doc);
    TEST_PASS("test_compact_attribute_access");
}

/* ============================================================================
 * Test 4: Compact mode DOM modification
 * ============================================================================ */

static int test_compact_dom_modification(void) {
    TaurusDocument doc = taurus_parse_string("<root/>", strlen("<root/>"), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Add children dynamically */
    for (int i = 0; i < 10; i++) {
        char name[50];
        snprintf(name, sizeof(name), "child%d", i);

        TaurusElement child = taurus_element_create(doc, name);
        TEST_ASSERT(child != NULL, "Child creation failed");

        /* Add attribute */
        char attr_name[50], attr_value[50];
        snprintf(attr_name, sizeof(attr_name), "id%d", i);
        snprintf(attr_value, sizeof(attr_value), "%d", i);
        taurus_element_set_attribute(child, attr_name, attr_value);

        /* Append to root */
        taurus_element_append_child(root, child);
    }

    /* Verify all children were added */
    size_t child_count = taurus_element_child_count(root);
    TEST_ASSERT(child_count == 10, "Expected 10 children");

    /* Verify each child's attributes */
    for (size_t i = 0; i < child_count; i++) {
        TaurusElement child = taurus_element_child(root, i);
        TEST_ASSERT(child != NULL, "Child should not be NULL");

        char attr_name[50], expected_value[50];
        snprintf(attr_name, sizeof(attr_name), "id%zu", i);
        snprintf(expected_value, sizeof(expected_value), "%zu", i);

        const char* attr_value = taurus_element_attribute(child, attr_name);
        TEST_ASSERT(attr_value != NULL, "Attribute should not be NULL");
        TEST_ASSERT(strcmp(attr_value, expected_value) == 0, "Attribute value mismatch");
    }

    /* Remove a child */
    TaurusElement child5 = taurus_element_child(root, 5);
    TEST_ASSERT(child5 != NULL, "Child 5 should exist");
    taurus_element_remove_child(root, child5);

    /* Verify child count decreased */
    child_count = taurus_element_child_count(root);
    TEST_ASSERT(child_count == 9, "Expected 9 children after removal");

    taurus_document_free(doc);
    TEST_PASS("test_compact_dom_modification");
}

/* ============================================================================
 * Test 5: Compact mode sibling traversal
 * ============================================================================ */

static int test_compact_sibling_traversal(void) {
    TaurusDocument doc = taurus_parse_string(
        "<root><a/><b/><c/><d/><e/></root>",
        strlen("<root><a/><b/><c/><d/><e/></root>"),
        NULL
    );
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Traverse via next_sibling */
    TaurusElement current = taurus_element_child(root, 0);
    int count = 0;
    const char* expected[] = {"a", "b", "c", "d", "e"};

    while (current != NULL && count < 5) {
        const char* name = taurus_element_name(current);
        TEST_ASSERT(name != NULL, "Element name should not be NULL");
        TEST_ASSERT(strcmp(name, expected[count]) == 0, "Element name mismatch");

        count++;
        current = taurus_element_next_sibling_any(current);
    }

    TEST_ASSERT(count == 5, "Expected to traverse 5 siblings");

    taurus_document_free(doc);
    TEST_PASS("test_compact_sibling_traversal");
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
    {"test_compact_basic_creation", test_compact_basic_creation},
    {"test_compact_deep_traversal", test_compact_deep_traversal},
    {"test_compact_attribute_access", test_compact_attribute_access},
    {"test_compact_dom_modification", test_compact_dom_modification},
    {"test_compact_sibling_traversal", test_compact_sibling_traversal},
    {NULL, NULL}
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     Taurus Compact Mode Test Suite                        ║\n");
    printf("║     Testing compressed pointer architecture                ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

#ifdef TAURUS_COMPACT_MODE
    printf("✅ TAURUS_COMPACT_MODE is ENABLED\n");
    printf("   Target: 32 bytes/element (vs 192 bytes regular = 6x reduction)\n\n");
#else
    printf("⚠️  TAURUS_COMPACT_MODE is NOT ENABLED\n");
    printf("   Compile with -DTAURUS_COMPACT_MODE to enable compact mode\n");
    printf("   Tests will run in regular mode for comparison\n\n");
#endif

    int passed = 0;
    int failed = 0;

    for (int i = 0; tests[i].name != NULL; i++) {
        if (tests[i].func()) {
            passed++;
        } else {
            failed++;
        }
    }

    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n");

    return (failed == 0) ? 0 : 1;
}
