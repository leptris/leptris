/* test_xpath_variables.c - XPath Variables Tests
 * Copyright (c) 2025, Ribose Inc.
 *
 * Tests for XPath 1.0 variable support.
 * Based on pugixml test_xpath_variables.cpp
 */

#include <taurus.h>
#include <stdio.h>
#include <string.h>

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
        printf("PASS: %s\n", name); \
        return 1; \
    } while(0)

/* ============================================================================
 * Test 1: Variable set creation and free
 * ============================================================================ */

static int test_variable_set_create_free(void) {
    TaurusXPathVariableSet set = taurus_xpath_variable_set_new();
    TEST_ASSERT(set != NULL, "Variable set creation failed");

    taurus_xpath_variable_set_free(set);
    TEST_PASS("test_variable_set_create_free");
}

/* ============================================================================
 * Test 2: Boolean variable
 * ============================================================================ */

static int test_variable_set_boolean(void) {
    TaurusXPathVariableSet set = taurus_xpath_variable_set_new();
    TEST_ASSERT(set != NULL, "Variable set creation failed");

    TaurusStatus status = taurus_xpath_variable_set_boolean(set, "var", 1);
    TEST_ASSERT(status == TAURUS_OK, "Failed to set boolean variable");

    taurus_xpath_variable_set_free(set);
    TEST_PASS("test_variable_set_boolean");
}

/* ============================================================================
 * Test 3: Number variable
 * ============================================================================ */

static int test_variable_set_number(void) {
    TaurusXPathVariableSet set = taurus_xpath_variable_set_new();
    TEST_ASSERT(set != NULL, "Variable set creation failed");

    TaurusStatus status = taurus_xpath_variable_set_number(set, "var", 3.14);
    TEST_ASSERT(status == TAURUS_OK, "Failed to set number variable");

    taurus_xpath_variable_set_free(set);
    TEST_PASS("test_variable_set_number");
}

/* ============================================================================
 * Test 4: String variable
 * ============================================================================ */

static int test_variable_set_string(void) {
    TaurusXPathVariableSet set = taurus_xpath_variable_set_new();
    TEST_ASSERT(set != NULL, "Variable set creation failed");

    TaurusStatus status = taurus_xpath_variable_set_string(set, "var", "hello");
    TEST_ASSERT(status == TAURUS_OK, "Failed to set string variable");

    taurus_xpath_variable_set_free(set);
    TEST_PASS("test_variable_set_string");
}

/* ============================================================================
 * Test 5: NULL checks
 * ============================================================================ */

static int test_variable_set_null_checks(void) {
    /* NULL set with boolean */
    TaurusStatus status = taurus_xpath_variable_set_boolean(NULL, "var", 1);
    TEST_ASSERT(status == TAURUS_ERROR_NULL_ARG, "Should fail with NULL set");

    /* NULL set with number */
    status = taurus_xpath_variable_set_number(NULL, "var", 1.0);
    TEST_ASSERT(status == TAURUS_ERROR_NULL_ARG, "Should fail with NULL set");

    /* NULL set with string */
    status = taurus_xpath_variable_set_string(NULL, "var", "test");
    TEST_ASSERT(status == TAURUS_ERROR_NULL_ARG, "Should fail with NULL set");

    /* NULL name */
    TaurusXPathVariableSet set = taurus_xpath_variable_set_new();
    TEST_ASSERT(set != NULL, "Variable set creation failed");

    status = taurus_xpath_variable_set_boolean(set, NULL, 1);
    TEST_ASSERT(status == TAURUS_ERROR_NULL_ARG, "Should fail with NULL name");

    taurus_xpath_variable_set_free(set);
    TEST_PASS("test_variable_set_null_checks");
}

/* ============================================================================
 * Test 6: Multiple variables
 * ============================================================================ */

static int test_variable_set_multiple(void) {
    TaurusXPathVariableSet set = taurus_xpath_variable_set_new();
    TEST_ASSERT(set != NULL, "Variable set creation failed");

    TaurusStatus status;

    status = taurus_xpath_variable_set_boolean(set, "flag1", 1);
    TEST_ASSERT(status == TAURUS_OK, "Failed to set flag1");

    status = taurus_xpath_variable_set_boolean(set, "flag2", 0);
    TEST_ASSERT(status == TAURUS_OK, "Failed to set flag2");

    status = taurus_xpath_variable_set_number(set, "value1", 42.0);
    TEST_ASSERT(status == TAURUS_OK, "Failed to set value1");

    status = taurus_xpath_variable_set_number(set, "value2", 3.14);
    TEST_ASSERT(status == TAURUS_OK, "Failed to set value2");

    status = taurus_xpath_variable_set_string(set, "text1", "hello");
    TEST_ASSERT(status == TAURUS_OK, "Failed to set text1");

    status = taurus_xpath_variable_set_string(set, "text2", "world");
    TEST_ASSERT(status == TAURUS_OK, "Failed to set text2");

    taurus_xpath_variable_set_free(set);
    TEST_PASS("test_variable_set_multiple");
}

/* ============================================================================
 * Test 7: Empty string variable
 * ============================================================================ */

static int test_variable_set_empty_string(void) {
    TaurusXPathVariableSet set = taurus_xpath_variable_set_new();
    TEST_ASSERT(set != NULL, "Variable set creation failed");

    TaurusStatus status = taurus_xpath_variable_set_string(set, "var", "");
    TEST_ASSERT(status == TAURUS_OK, "Failed to set empty string");

    taurus_xpath_variable_set_free(set);
    TEST_PASS("test_variable_set_empty_string");
}

/* ============================================================================
 * Test 8: Special characters in string
 * ============================================================================ */

static int test_variable_set_special_chars(void) {
    TaurusXPathVariableSet set = taurus_xpath_variable_set_new();
    TEST_ASSERT(set != NULL, "Variable set creation failed");

    TaurusStatus status;

    status = taurus_xpath_variable_set_string(set, "var", "hello <world>");
    TEST_ASSERT(status == TAURUS_OK, "Failed to set string with < >");

    status = taurus_xpath_variable_set_string(set, "var2", "\"quoted\"");
    TEST_ASSERT(status == TAURUS_OK, "Failed to set string with quotes");

    taurus_xpath_variable_set_free(set);
    TEST_PASS("test_variable_set_special_chars");
}

/* ============================================================================
 * Test 9: Unicode string variable
 * ============================================================================ */

static int test_variable_set_unicode_string(void) {
    TaurusXPathVariableSet set = taurus_xpath_variable_set_new();
    TEST_ASSERT(set != NULL, "Variable set creation failed");

    /* UTF-8 string with non-ASCII characters */
    TaurusStatus status = taurus_xpath_variable_set_string(set, "var", "hello 世界");
    TEST_ASSERT(status == TAURUS_OK, "Failed to set UTF-8 string");

    taurus_xpath_variable_set_free(set);
    TEST_PASS("test_variable_set_unicode_string");
}

/* ============================================================================
 * Test 10: Variable name with underscore
 * ============================================================================ */

static int test_variable_set_underscore_name(void) {
    TaurusXPathVariableSet set = taurus_xpath_variable_set_new();
    TEST_ASSERT(set != NULL, "Variable set creation failed");

    TaurusStatus status = taurus_xpath_variable_set_number(set, "my_var", 42);
    TEST_ASSERT(status == TAURUS_OK, "Failed to set variable with underscore");

    taurus_xpath_variable_set_free(set);
    TEST_PASS("test_variable_set_underscore_name");
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
    {"test_variable_set_create_free", test_variable_set_create_free},
    {"test_variable_set_boolean", test_variable_set_boolean},
    {"test_variable_set_number", test_variable_set_number},
    {"test_variable_set_string", test_variable_set_string},
    {"test_variable_set_null_checks", test_variable_set_null_checks},
    {"test_variable_set_multiple", test_variable_set_multiple},
    {"test_variable_set_empty_string", test_variable_set_empty_string},
    {"test_variable_set_special_chars", test_variable_set_special_chars},
    {"test_variable_set_unicode_string", test_variable_set_unicode_string},
    {"test_variable_set_underscore_name", test_variable_set_underscore_name},
    {NULL, NULL}
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     Taurus XPath Variables Test Suite                     ║\n");
    printf("║     Testing XPath 1.0 variable support                     ║\n");
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
