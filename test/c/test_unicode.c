/* test_unicode.c - Unicode Conversion Tests
 * Copyright (c) 2025, Ribose Inc.
 *
 * Tests for UTF-8 to UTF-16 conversion and vice versa.
 * Based on pugixml test_unicode.cpp
 */

#include <taurus.h>
#include <stdio.h>
#include <string.h>

/* Include internal unicode functions for testing */
#include "../../../src/taurus/encoding/unicode.h"

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
 * Test 1: Empty string
 * ============================================================================ */

static int test_utf8_to_utf16_empty(void) {
    uint16_t* utf16 = NULL;
    size_t count = taurus_utf8_to_utf16("", &utf16);

    TEST_ASSERT(utf16 != NULL, "UTF-16 allocation failed");
    TEST_ASSERT(count == 1, "Empty string should have 1 code unit (null terminator)");
    TEST_ASSERT(utf16[0] == 0, "First code unit should be null");

    taurus_utf16_free(utf16);
    TEST_PASS("test_utf8_to_utf16_empty");
}

/* ============================================================================
 * Test 2: ASCII string
 * ============================================================================ */

static int test_utf8_to_utf16_ascii(void) {
    const char* utf8_str = "hello";
    uint16_t* utf16 = NULL;
    size_t count = taurus_utf8_to_utf16(utf8_str, &utf16);

    TEST_ASSERT(utf16 != NULL, "UTF-16 allocation failed");
    TEST_ASSERT(count == 5, "ASCII string should have 5 code units");

    /* Verify characters */
    TEST_ASSERT(utf16[0] == 'h', "First character should be 'h'");
    TEST_ASSERT(utf16[1] == 'e', "Second character should be 'e'");
    TEST_ASSERT(utf16[2] == 'l', "Third character should be 'l'");
    TEST_ASSERT(utf16[3] == 'l', "Fourth character should be 'l'");
    TEST_ASSERT(utf16[4] == 'o', "Fifth character should be 'o'");
    TEST_ASSERT(utf16[5] == 0, "Sixth character should be null");

    taurus_utf16_free(utf16);
    TEST_PASS("test_utf8_to_utf16_ascii");
}

/* ============================================================================
 * Test 3: UTF-8 multi-byte (2-byte sequence)
 * ============================================================================ */

static int test_utf8_to_utf16_2byte(void) {
    /* Cyrillic small letter 'a' (U+0430) */
    const char* utf8_str = "\xD0\xB0";
    uint16_t* utf16 = NULL;
    size_t count = taurus_utf8_to_utf16(utf8_str, &utf16);

    TEST_ASSERT(utf16 != NULL, "UTF-16 allocation failed");
    TEST_ASSERT(count == 1, "2-byte UTF-8 should have 1 code unit");
    TEST_ASSERT(utf16[0] == 0x0430, "Cyrillic 'a' should be U+0430");
    TEST_ASSERT(utf16[1] == 0, "Should be null terminated");

    taurus_utf16_free(utf16);
    TEST_PASS("test_utf8_to_utf16_2byte");
}

/* ============================================================================
 * Test 4: UTF-8 multi-byte (3-byte sequence)
 * ============================================================================ */

static int test_utf8_to_utf16_3byte(void) {
    /* Interrobang (U+203D) */
    const char* utf8_str = "\xE2\x80\xBD";
    uint16_t* utf16 = NULL;
    size_t count = taurus_utf8_to_utf16(utf8_str, &utf16);

    TEST_ASSERT(utf16 != NULL, "UTF-16 allocation failed");
    TEST_ASSERT(count == 1, "3-byte UTF-8 should have 1 code unit");
    TEST_ASSERT(utf16[0] == 0x203D, "Interrobang should be U+203D");
    TEST_ASSERT(utf16[1] == 0, "Should be null terminated");

    taurus_utf16_free(utf16);
    TEST_PASS("test_utf8_to_utf16_3byte");
}

/* ============================================================================
 * Test 5: UTF-8 multi-byte (4-byte sequence - astral plane)
 * ============================================================================ */

static int test_utf8_to_utf16_4byte(void) {
    /* CJK UNIFIED IDEOGRAPH-20000 (U+20000) - 4-byte UTF-8
     * This is in the astral plane (>= 0x10000), so needs surrogate pair */
    const char* utf8_str = "\xF0\xA0\x80\x80";
    uint16_t* utf16 = NULL;
    size_t count = taurus_utf8_to_utf16(utf8_str, &utf16);

    TEST_ASSERT(utf16 != NULL, "UTF-16 allocation failed");
    TEST_ASSERT(count == 2, "4-byte UTF-8 should have 2 code units (surrogate pair)");
    /* U+20000 - 0x20000 = 0x00000, encode as surrogate pair: D840 DC00 */
    TEST_ASSERT(utf16[0] == 0xD840, "High surrogate should be D840");
    TEST_ASSERT(utf16[1] == 0xDC00, "Low surrogate should be DC00");
    TEST_ASSERT(utf16[2] == 0, "Should be null terminated");

    taurus_utf16_free(utf16);
    TEST_PASS("test_utf8_to_utf16_4byte");
}

/* ============================================================================
 * Test 6: Invalid UTF-8 sequence
 * ============================================================================ */

static int test_utf8_to_utf16_invalid(void) {
    /* Invalid continuation byte */
    const char* utf8_str = "a\xB0";
    uint16_t* utf16 = NULL;
    size_t count = taurus_utf8_to_utf16(utf8_str, &utf16);

    TEST_ASSERT(utf16 != NULL, "UTF-16 allocation failed");
    TEST_ASSERT(count == 2, "Should have 2 code units (1 char + replacement)");
    TEST_ASSERT(utf16[0] == 'a', "First character should be 'a'");
    TEST_ASSERT(utf16[1] == 0xFFFD, "Second character should be replacement character");
    TEST_ASSERT(utf16[2] == 0, "Should be null terminated");

    taurus_utf16_free(utf16);
    TEST_PASS("test_utf8_to_utf16_invalid");
}

/* ============================================================================
 * Test 7: UTF-16 to UTF-8 empty
 * ============================================================================ */

static int test_utf16_to_utf8_empty(void) {
    char* utf8 = NULL;
    uint16_t utf16_str[] = {0};

    size_t count = taurus_utf16_to_utf8(utf16_str, 0, &utf8);

    TEST_ASSERT(utf8 != NULL, "UTF-8 allocation failed");
    TEST_ASSERT(count == 0, "Empty UTF-16 should have 0 bytes");
    TEST_ASSERT(utf8[0] == '\0', "UTF-8 should be empty string");

    taurus_utf8_free(utf8);
    TEST_PASS("test_utf16_to_utf8_empty");
}

/* ============================================================================
 * Test 8: UTF-16 to UTF-8 ASCII
 * ============================================================================ */

static int test_utf16_to_utf8_ascii(void) {
    uint16_t utf16_str[] = {'h', 'e', 'l', 'l', 'o', 0};
    char* utf8 = NULL;

    size_t count = taurus_utf16_to_utf8(utf16_str, 5, &utf8);

    TEST_ASSERT(utf8 != NULL, "UTF-8 allocation failed");
    TEST_ASSERT(count == 5, "ASCII should have 5 bytes");
    TEST_ASSERT(strcmp(utf8, "hello") == 0, "UTF-8 should match 'hello'");

    taurus_utf8_free(utf8);
    TEST_PASS("test_utf16_to_utf8_ascii");
}

/* ============================================================================
 * Test 9: UTF-16 to UTF-8 surrogate pair
 * ============================================================================ */

static int test_utf16_to_utf8_surrogate(void) {
    /* Surrogate pair for U+10000 */
    uint16_t utf16_str[] = {0xD800, 0xDC00, 0};
    char* utf8 = NULL;

    size_t count = taurus_utf16_to_utf8(utf16_str, 2, &utf8);

    TEST_ASSERT(utf8 != NULL, "UTF-8 allocation failed");
    TEST_ASSERT(count == 4, "Surrogate pair should encode to 4 bytes");
    /* U+10000 in UTF-8 is F0 90 80 80 */
    TEST_ASSERT((unsigned char)utf8[0] == 0xF0, "First byte should be F0");
    TEST_ASSERT((unsigned char)utf8[1] == 0x90, "Second byte should be 90");
    TEST_ASSERT((unsigned char)utf8[2] == 0x80, "Third byte should be 80");
    TEST_ASSERT((unsigned char)utf8[3] == 0x80, "Fourth byte should be 80");

    taurus_utf8_free(utf8);
    TEST_PASS("test_utf16_to_utf8_surrogate");
}

/* ============================================================================
 * Test 10: Isolated surrogate
 * ============================================================================ */

static int test_utf16_to_utf8_isolated_surrogate(void) {
    /* Isolated high surrogate - should be replaced */
    uint16_t utf16_str[] = {0xD800, 0};
    char* utf8 = NULL;

    size_t count = taurus_utf16_to_utf8(utf16_str, 1, &utf8);

    TEST_ASSERT(utf8 != NULL, "UTF-8 allocation failed");
    TEST_ASSERT(count == 3, "Isolated surrogate should be replaced (3 bytes)");
    /* Replacement character U+FFFD in UTF-8 is EF BF BD */
    TEST_ASSERT((unsigned char)utf8[0] == 0xEF, "First byte should be EF");
    TEST_ASSERT((unsigned char)utf8[1] == 0xBF, "Second byte should be BF");
    TEST_ASSERT((unsigned char)utf8[2] == 0xBD, "Third byte should be BD");

    taurus_utf8_free(utf8);
    TEST_PASS("test_utf16_to_utf8_isolated_surrogate");
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
    {"test_utf8_to_utf16_empty", test_utf8_to_utf16_empty},
    {"test_utf8_to_utf16_ascii", test_utf8_to_utf16_ascii},
    {"test_utf8_to_utf16_2byte", test_utf8_to_utf16_2byte},
    {"test_utf8_to_utf16_3byte", test_utf8_to_utf16_3byte},
    {"test_utf8_to_utf16_4byte", test_utf8_to_utf16_4byte},
    {"test_utf8_to_utf16_invalid", test_utf8_to_utf16_invalid},
    {"test_utf16_to_utf8_empty", test_utf16_to_utf8_empty},
    {"test_utf16_to_utf8_ascii", test_utf16_to_utf8_ascii},
    {"test_utf16_to_utf8_surrogate", test_utf16_to_utf8_surrogate},
    {"test_utf16_to_utf8_isolated_surrogate", test_utf16_to_utf8_isolated_surrogate},
    {NULL, NULL}
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     Taurus Unicode Conversion Test Suite                ║\n");
    printf("║     Testing UTF-8 to UTF-16 conversion and reverse       ║\n");
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
