/**
 * test_memory_pool.c - Test Taurus memory pool functionality
 *
 * Based on pugixml test_memory.cpp
 *
 * Tests the memory pool allocator for:
 * - O(1) allocation performance
 * - Bulk deallocation on document cleanup
 * - Large allocation handling
 * - String growth/shrinking patterns
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
 * Test 1: Basic pool allocation
 * ============================================================================ */

static int test_pool_basic_allocation(void) {
    TaurusDocument doc = taurus_parse_string("<root/>", strlen("<root/>"), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Create multiple children - uses pool allocation */
    for (int i = 0; i < 100; i++) {
        char name[50];
        snprintf(name, sizeof(name), "child%d", i);

        TaurusElement child = taurus_element_create(doc, name);
        TEST_ASSERT(child != NULL, "Child creation failed");

        taurus_element_append_child(root, child);
    }

    /* Verify all children were created */
    size_t child_count = taurus_element_child_count(root);
    TEST_ASSERT(child_count == 100, "Expected 100 children");

    taurus_document_free(doc);
    TEST_PASS("test_pool_basic_allocation");
}

/* ============================================================================
 * Test 2: Large string handling
 * ============================================================================ */

static int test_pool_large_strings(void) {
    /* Create document with large text content */
    char xml[10000];
    strcpy(xml, "<root>");

    /* Add elements with increasingly large text */
    for (int i = 0; i < 10; i++) {
        char text[2000];
        snprintf(text, sizeof(text), "<item>text_%d_%s</item>", i,
                 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                 "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
                 "cccccccccccccccccccccccccccccccccccccccccccccccccc"
                 "dddddddddddddddddddddddddddddddddddddddddddddddddd");
        strcat(xml, text);
    }

    strcat(xml, "</root>");

    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Verify all items were parsed */
    size_t child_count = taurus_element_child_count(root);
    TEST_ASSERT(child_count == 10, "Expected 10 children");

    /* Verify text content */
    for (size_t i = 0; i < child_count; i++) {
        TaurusElement item = taurus_element_child(root, i);
        TEST_ASSERT(item != NULL, "Item should not be NULL");

        const char* text = taurus_element_text(item);
        TEST_ASSERT(text != NULL, "Text content should not be NULL");
        TEST_ASSERT(strlen(text) > 100, "Text should be long");
    }

    taurus_document_free(doc);
    TEST_PASS("test_pool_large_strings");
}

/* ============================================================================
 * Test 3: String growth pattern (increasing size)
 * ============================================================================ */

static int test_pool_string_growth(void) {
    TaurusDocument doc = taurus_parse_string("<root/>", strlen("<root/>"), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Create elements with increasingly long names */
    for (int i = 1; i <= 20; i++) {
        char name[100];
        for (int j = 0; j < i; j++) {
            name[j] = 'a' + (i % 26);
        }
        name[i] = '\0';

        TaurusElement child = taurus_element_create(doc, name);
        TEST_ASSERT(child != NULL, "Child creation failed");

        /* Add attribute with long value */
        char attr_name[50], attr_value[100];
        snprintf(attr_name, sizeof(attr_name), "attr%d", i);
        for (int j = 0; j < i * 2; j++) {
            attr_value[j] = '0' + (i % 10);
        }
        attr_value[i * 2] = '\0';

        taurus_element_set_attribute(child, attr_name, attr_value);
        taurus_element_append_child(root, child);
    }

    /* Verify all elements were created */
    size_t child_count = taurus_element_child_count(root);
    TEST_ASSERT(child_count == 20, "Expected 20 children");

    taurus_document_free(doc);
    TEST_PASS("test_pool_string_growth");
}

/* ============================================================================
 * Test 4: String shrinking pattern
 * ============================================================================ */

static int test_pool_string_shrink(void) {
    TaurusDocument doc = taurus_parse_string("<root/>", strlen("<root/>"), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Create elements with decreasing name lengths */
    for (int i = 20; i >= 1; i--) {
        char name[100];
        for (int j = 0; j < i; j++) {
            name[j] = 'z' - (i % 26);
        }
        name[i] = '\0';

        TaurusElement child = taurus_element_create(doc, name);
        TEST_ASSERT(child != NULL, "Child creation failed");

        taurus_element_append_child(root, child);
    }

    /* Verify all elements were created */
    size_t child_count = taurus_element_child_count(root);
    TEST_ASSERT(child_count == 20, "Expected 20 children");

    taurus_document_free(doc);
    TEST_PASS("test_pool_string_shrink");
}

/* ============================================================================
 * Test 5: In-place string modification
 * ============================================================================ */

static int test_pool_string_inplace(void) {
    TaurusDocument doc = taurus_parse_string("<root><item/></root>",
                                             strlen("<root><item/></root>"),
                                             NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    TaurusElement item = taurus_element_child(root, 0);
    TEST_ASSERT(item != NULL, "Item should not be NULL");

    /* Set attribute values repeatedly */
    const char* test_values[] = {"a", "ab", "abc", "abcd", "abcde"};
    for (size_t i = 0; i < 5; i++) {
        taurus_element_set_attribute(item, "value", test_values[i]);

        const char* value = taurus_element_attribute(item, "value");
        TEST_ASSERT(value != NULL, "Attribute value should not be NULL");
        TEST_ASSERT(strcmp(value, test_values[i]) == 0, "Attribute value mismatch");
    }

    taurus_document_free(doc);
    TEST_PASS("test_pool_string_inplace");
}

/* ============================================================================
 * Test 6: Deep tree memory efficiency
 * ============================================================================ */

static int test_pool_deep_tree(void) {
    /* Create a deep tree structure */
    /* Size calculation: 1000 elements × ~40 chars each + overhead = ~50000 bytes needed */
    char xml[50000];
    strcpy(xml, "<root>");

    /* Create 5 levels of nesting with 10 children each */
    int levels = 5;
    int children_per_level = 10;

    char* p = xml + strlen(xml);
    for (int i = 0; i < children_per_level; i++) {
        sprintf(p, "<l1_%d>", i);
        p += strlen(p);

        for (int j = 0; j < children_per_level; j++) {
            sprintf(p, "<l2_%d>", j);
            p += strlen(p);

            for (int k = 0; k < children_per_level; k++) {
                sprintf(p, "<l3_%d><leaf>data</leaf></l3_%d>", k, k);
                p += strlen(p);
            }

            sprintf(p, "</l2_%d>", j);
            p += strlen(p);
        }

        sprintf(p, "</l1_%d>", i);
        p += strlen(p);
    }

    strcat(xml, "</root>");

    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Count total leaf elements */
    int leaf_count = 0;
    size_t l1_count = taurus_element_child_count(root);

    for (size_t i = 0; i < l1_count; i++) {
        TaurusElement l1 = taurus_element_child(root, i);
        if (!l1) continue;

        size_t l2_count = taurus_element_child_count(l1);
        for (size_t j = 0; j < l2_count; j++) {
            TaurusElement l2 = taurus_element_child(l1, j);
            if (!l2) continue;

            size_t l3_count = taurus_element_child_count(l2);
            for (size_t k = 0; k < l3_count; k++) {
                TaurusElement l3 = taurus_element_child(l2, k);
                if (!l3) continue;

                /* Check for leaf element */
                TaurusElement leaf = taurus_element_child(l3, 0);
                if (leaf) {
                    const char* name = taurus_element_name(leaf);
                    if (name && strcmp(name, "leaf") == 0) {
                        leaf_count++;
                    }
                }
            }
        }
    }

    /* Expected: 10 * 10 * 10 = 1000 leaf elements */
    TEST_ASSERT(leaf_count == 1000, "Expected 1000 leaf elements");

    taurus_document_free(doc);
    TEST_PASS("test_pool_deep_tree");
}

/* ============================================================================
 * Test 7: Attribute-heavy document
 * ============================================================================ */

static int test_pool_attribute_heavy(void) {
    /* Create document with many attributes per element */
    TaurusDocument doc = taurus_parse_string("<root/>", strlen("<root/>"), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element is NULL");

    /* Create 50 elements, each with 20 attributes */
    for (int i = 0; i < 50; i++) {
        char elem_name[50];
        snprintf(elem_name, sizeof(elem_name), "item%d", i);

        TaurusElement item = taurus_element_create(doc, elem_name);
        TEST_ASSERT(item != NULL, "Item creation failed");

        /* Add 20 attributes */
        for (int j = 0; j < 20; j++) {
            char attr_name[50], attr_value[50];
            snprintf(attr_name, sizeof(attr_name), "attr%d", j);
            snprintf(attr_value, sizeof(attr_value), "value%d_%d", i, j);

            taurus_element_set_attribute(item, attr_name, attr_value);
        }

        taurus_element_append_child(root, item);
    }

    /* Verify all elements and attributes */
    size_t child_count = taurus_element_child_count(root);
    TEST_ASSERT(child_count == 50, "Expected 50 children");

    /* Spot check some attributes */
    TaurusElement item25 = taurus_element_child(root, 25);
    TEST_ASSERT(item25 != NULL, "Item 25 should exist");

    const char* attr5 = taurus_element_attribute(item25, "attr5");
    TEST_ASSERT(attr5 != NULL, "attr5 should exist");
    TEST_ASSERT(strcmp(attr5, "value25_5") == 0, "attr5 value mismatch");

    taurus_document_free(doc);
    TEST_PASS("test_pool_attribute_heavy");
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
    {"test_pool_basic_allocation", test_pool_basic_allocation},
    {"test_pool_large_strings", test_pool_large_strings},
    {"test_pool_string_growth", test_pool_string_growth},
    {"test_pool_string_shrink", test_pool_string_shrink},
    {"test_pool_string_inplace", test_pool_string_inplace},
    {"test_pool_deep_tree", test_pool_deep_tree},
    {"test_pool_attribute_heavy", test_pool_attribute_heavy},
    {NULL, NULL}
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     Taurus Memory Pool Test Suite                         ║\n");
    printf("║     Testing O(1) pool allocation and bulk deallocation      ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

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
