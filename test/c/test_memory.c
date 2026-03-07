/* test_memory.c - Memory allocation hooks test
 * Copyright (c) 2024, Ribose Inc.
 *
 * Test memory allocation hooks API for pugixml compatibility
 */

#include "taurus.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Test macros */
#define TEST_ASSERT(expr, msg) do { \
    if (!(expr)) { \
        printf("FAIL: %s\n", msg); \
        return 1; \
    } \
} while(0)

#define RUN_TEST(func) do { \
    printf("Running %s...\n", #func); \
    if (func() != 0) { \
        printf("FAILED\n"); \
        return 1; \
    } \
    printf("PASSED\n\n"); \
} while(0)

#define TEST_PASS(name) printf("PASS: %s\n", name); return 0;

/* Test tracking variables */
static int alloc_calls = 0;
static int dealloc_calls = 0;

/* Custom allocation functions with tracking */
void* custom_allocate(size_t size) {
    void* ptr = malloc(size);
    if (ptr) {
        alloc_calls++;
    }
    return ptr;
}

void custom_deallocate(void* ptr) {
    if (ptr) {
        dealloc_calls++;
        free(ptr);
    }
}

/* Test: Custom memory management */
static int test_custom_memory_management(void) {
    alloc_calls = 0;
    dealloc_calls = 0;

    /* Remember old functions */
    taurus_allocation_function old_alloc = taurus_get_memory_allocation_function();
    taurus_deallocation_function old_dealloc = taurus_get_memory_deallocation_function();

    /* Set custom functions */
    taurus_set_memory_management_functions(custom_allocate, custom_deallocate);

    /* Parse document */
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string("<node/>", 7, &status);

    if (status != TAURUS_OK) {
        printf("Parse failed with status: %d\n", status);
    }

    TEST_ASSERT(doc != NULL, "Document should parse");
    TEST_ASSERT(status == TAURUS_OK, "Parse should succeed");
    TEST_ASSERT(alloc_calls > 0, "Should have allocations");

    /* Modify document - set name */
    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(root != NULL, "Root element should exist");

    TaurusStatus set_status = taurus_element_set_name(root, "foobars");
    TEST_ASSERT(set_status == TAURUS_OK, "Set name should succeed");

    /* Free document - this should trigger deallocations */
    taurus_document_free(doc);

    /* After free, we should have deallocations */
    TEST_ASSERT(dealloc_calls > 0, "Should have deallocations after free");

    /* Restore old functions */
    taurus_set_memory_management_functions(old_alloc, old_dealloc);

    TEST_PASS("test_custom_memory_management");
    return 0;
}

/* Test: Get/Set memory functions */
static int test_memory_function_getters(void) {
    /* Initial state should be NULL (using malloc/free) */
    taurus_allocation_function alloc_func = taurus_get_memory_allocation_function();
    taurus_deallocation_function dealloc_func = taurus_get_memory_deallocation_function();

    TEST_ASSERT(alloc_func == NULL, "Initial alloc function should be NULL");
    TEST_ASSERT(dealloc_func == NULL, "Initial dealloc function should be NULL");

    /* Set custom functions */
    taurus_set_memory_management_functions(custom_allocate, custom_deallocate);

    /* Verify they were set */
    alloc_func = taurus_get_memory_allocation_function();
    dealloc_func = taurus_get_memory_deallocation_function();

    TEST_ASSERT(alloc_func == custom_allocate, "Alloc function should match");
    TEST_ASSERT(dealloc_func == custom_deallocate, "Dealloc function should match");

    /* Restore to defaults */
    taurus_set_memory_management_functions(NULL, NULL);

    /* Verify they were cleared */
    alloc_func = taurus_get_memory_allocation_function();
    dealloc_func = taurus_get_memory_deallocation_function();

    TEST_ASSERT(alloc_func == NULL, "Alloc function should be NULL again");
    TEST_ASSERT(dealloc_func == NULL, "Dealloc function should be NULL again");

    TEST_PASS("test_memory_function_getters");
    return 0;
}

/* Test: String allocation with custom allocator */
static int test_string_allocation_with_custom_allocator(void) {
    /* Set custom allocator */
    taurus_set_memory_management_functions(custom_allocate, custom_deallocate);

    /* Create document with text content */
    const char* xml = "<root>This is a test string with some content</root>";
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);

    TEST_ASSERT(doc != NULL, "Document should parse");

    /* Verify it serializes correctly */
    char* output = taurus_document_serialize(doc, NULL);
    TEST_ASSERT(output != NULL, "Serialization should succeed");
    TEST_ASSERT(strstr(output, "test string") != NULL, "Content should be preserved");

    taurus_free_string(output);
    taurus_document_free(doc);

    /* Restore defaults */
    taurus_set_memory_management_functions(NULL, NULL);

    TEST_PASS("test_string_allocation_with_custom_allocator");
    return 0;
}

/* Main test runner */
int main(void) {
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║     Taurus Memory Allocation Hooks Test Suite         ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n\n");

    RUN_TEST(test_custom_memory_management);
    RUN_TEST(test_memory_function_getters);
    RUN_TEST(test_string_allocation_with_custom_allocator);

    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║     All Memory Tests Passed!                          ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");

    return 0;
}
