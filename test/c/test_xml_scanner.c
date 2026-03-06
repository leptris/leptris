/* test_xml_scanner.c - Unit tests for XML scanner primitives
 * Copyright (c) 2026, Ribose Inc.
 *
 * Tests the shared XML scanning primitives in xml_scanner.h:
 * - Character classification
 * - Name scanning
 * - Whitespace handling
 * - SIMD optimizations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "xml_scanner.h"  /* Direct include since we're testing internal API */

/* ============================================================
 * Test Counters
 * ============================================================ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running: %s...", #name); \
    tests_run++; \
    test_##name(); \
    tests_passed++; \
    printf(" PASSED\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAILED at line %d: %s\n", __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_NULL(a) ASSERT((a) == NULL)
#define ASSERT_NOT_NULL(a) ASSERT((a) != NULL)
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)

/* ============================================================
 * Character Classification Tests
 * ============================================================ */

TEST(xml_is_space_basic) {
    /* Basic whitespace */
    ASSERT(xml_is_space(' ') == 1);
    ASSERT(xml_is_space('\t') == 1);
    ASSERT(xml_is_space('\n') == 1);
    ASSERT(xml_is_space('\r') == 1);

    /* Non-whitespace */
    ASSERT(xml_is_space('a') == 0);
    ASSERT(xml_is_space('0') == 0);
    ASSERT(xml_is_space('_') == 0);
    ASSERT(xml_is_space('\0') == 0);
    ASSERT(xml_is_space('<') == 0);
    ASSERT(xml_is_space('>') == 0);
}

TEST(xml_is_name_start_basic) {
    /* Valid name start characters */
    ASSERT(xml_is_name_start('a') == 1);
    ASSERT(xml_is_name_start('z') == 1);
    ASSERT(xml_is_name_start('A') == 1);
    ASSERT(xml_is_name_start('Z') == 1);
    ASSERT(xml_is_name_start('_') == 1);
    ASSERT(xml_is_name_start(':') == 1);

    /* Invalid name start characters */
    ASSERT(xml_is_name_start('0') == 0);
    ASSERT(xml_is_name_start('9') == 0);
    ASSERT(xml_is_name_start('-') == 0);
    ASSERT(xml_is_name_start('.') == 0);
    ASSERT(xml_is_name_start(' ') == 0);
    ASSERT(xml_is_name_start('<') == 0);
}

TEST(xml_is_name_char_basic) {
    /* Valid name characters (includes name start) */
    ASSERT(xml_is_name_char('a') == 1);
    ASSERT(xml_is_name_char('Z') == 1);
    ASSERT(xml_is_name_char('_') == 1);
    ASSERT(xml_is_name_char(':') == 1);
    ASSERT(xml_is_name_char('0') == 1);
    ASSERT(xml_is_name_char('9') == 1);
    ASSERT(xml_is_name_char('-') == 1);
    ASSERT(xml_is_name_char('.') == 1);

    /* Invalid name characters */
    ASSERT(xml_is_name_char(' ') == 0);
    ASSERT(xml_is_name_char('<') == 0);
    ASSERT(xml_is_name_char('>') == 0);
    ASSERT(xml_is_name_char('=') == 0);
    ASSERT(xml_is_name_char('/') == 0);
    ASSERT(xml_is_name_char('\0') == 0);
}

/* ============================================================
 * Name Scanning Tests
 * ============================================================ */

TEST(xml_scan_name_simple) {
    const char* xml = "element>";
    const char* end = xml + strlen(xml);
    const char* result = xml_scan_name(xml, end);

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result - xml, 7);  /* "element" length */
    ASSERT_EQ(*result, '>');
}

TEST(xml_scan_name_with_namespace) {
    const char* xml = "ns:element ";
    const char* end = xml + strlen(xml);
    const char* result = xml_scan_name(xml, end);

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result - xml, 10);  /* "ns:element" length */
    ASSERT(xml_is_space(*result));
}

TEST(xml_scan_name_with_digits) {
    const char* xml = "item123>";
    const char* end = xml + strlen(xml);
    const char* result = xml_scan_name(xml, end);

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result - xml, 7);  /* "item123" length */
}

TEST(xml_scan_name_with_hyphen_dot) {
    const char* xml = "xml-schema.xsd>";
    const char* end = xml + strlen(xml);
    const char* result = xml_scan_name(xml, end);

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result - xml, 14);  /* "xml-schema.xsd" length */
}

TEST(xml_scan_name_empty) {
    const char* xml = ">text";
    const char* end = xml + strlen(xml);
    const char* result = xml_scan_name(xml, end);

    ASSERT_EQ(result, xml);  /* No name found, returns start */
}

TEST(xml_scan_name_invalid_start) {
    const char* xml = "123abc>";
    const char* end = xml + strlen(xml);
    const char* result = xml_scan_name(xml, end);

    ASSERT_EQ(result, xml);  /* Invalid start char, returns start */
}

/* ============================================================
 * Whitespace Scanning Tests
 * ============================================================ */

TEST(xml_scan_whitespace_simple) {
    const char* xml = "   text";
    const char* end = xml + strlen(xml);
    const char* result = xml_scan_whitespace(xml, end);

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result - xml, 3);
    ASSERT_EQ(*result, 't');
}

TEST(xml_scan_whitespace_mixed) {
    const char* xml = " \t\n\r text";
    const char* end = xml + strlen(xml);
    const char* result = xml_scan_whitespace(xml, end);

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result - xml, 5);
    ASSERT_EQ(*result, 't');
}

TEST(xml_scan_whitespace_none) {
    const char* xml = "text";
    const char* end = xml + strlen(xml);
    const char* result = xml_scan_whitespace(xml, end);

    ASSERT_EQ(result, xml);  /* No whitespace, returns start */
}

TEST(xml_scan_whitespace_all) {
    const char* xml = "   \t\n";
    const char* end = xml + strlen(xml);
    const char* result = xml_scan_whitespace(xml, end);

    ASSERT_EQ(result, end);  /* All whitespace, returns end */
}

TEST(xml_scan_whitespace_empty) {
    const char* xml = "";
    const char* end = xml;
    const char* result = xml_scan_whitespace(xml, end);

    ASSERT_EQ(result, xml);  /* Empty string, returns start */
}

/* ============================================================
 * Whitespace-Only Check Tests
 * ============================================================ */

TEST(xml_is_whitespace_only_basic) {
    const char* ws = "   \t\n\r   ";
    ASSERT(xml_is_whitespace_only(ws, ws + strlen(ws)) == 1);

    const char* not_ws = "   text   ";
    ASSERT(xml_is_whitespace_only(not_ws, not_ws + strlen(not_ws)) == 0);

    const char* empty = "";
    ASSERT(xml_is_whitespace_only(empty, empty) == 1);  /* Empty is whitespace-only */
}

TEST(xml_is_whitespace_only_long) {
    /* Test with long string to exercise SIMD path */
    char buf[256];
    memset(buf, ' ', sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    ASSERT(xml_is_whitespace_only(buf, buf + strlen(buf)) == 1);

    /* Add non-whitespace in middle */
    buf[128] = 'x';
    ASSERT(xml_is_whitespace_only(buf, buf + strlen(buf)) == 0);
}

/* ============================================================
 * SIMD Threshold Tests
 * ============================================================ */

TEST(simd_threshold_whitespace) {
    /* Create string just above SIMD threshold (64 bytes) */
    char buf[128];
    memset(buf, ' ', sizeof(buf));
    buf[127] = '\0';

    const char* start = buf;
    const char* end = buf + 127;

    /* Should use SIMD path */
    const char* result = xml_scan_whitespace(start, end);
    ASSERT_EQ(result, end);

    /* Verify whitespace-only detection uses SIMD */
    ASSERT(xml_is_whitespace_only(start, end) == 1);
}

/* ============================================================
 * Edge Cases
 * ============================================================ */

TEST(null_pointer_handling) {
    /* Scanner should handle NULL gracefully */
    const char* result = xml_scan_name(NULL, NULL);
    ASSERT_NULL(result);

    result = xml_scan_whitespace(NULL, NULL);
    ASSERT_NULL(result);
}

TEST(start_after_end) {
    const char* xml = "text";
    const char* end = xml + 2;

    /* Start after end should return start */
    const char* result = xml_scan_whitespace(xml + 4, end);
    ASSERT_EQ(result, xml + 4);
}

TEST(single_character_names) {
    const char* xml = "a>";
    const char* end = xml + strlen(xml);
    const char* result = xml_scan_name(xml, end);

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result - xml, 1);
    ASSERT_EQ(*result, '>');
}

TEST(unicode_name_start) {
    /* UTF-8 encoded non-ASCII character - should not be valid name start
     * in our basic classification (we don't do full XML 1.0 name chars) */
    const char* xml = "\xC3\xA9lement>";  /* 'é' in UTF-8 */
    const char* end = xml + strlen(xml);

    /* Our basic scanner only handles ASCII, so this should fail */
    const char* result = xml_scan_name(xml, end);
    ASSERT(xml_is_name_start(*xml) == 0 || result == xml);
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("XML Scanner Unit Tests\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("[Character Classification]\n");
    RUN_TEST(xml_is_space_basic);
    RUN_TEST(xml_is_name_start_basic);
    RUN_TEST(xml_is_name_char_basic);

    printf("\n[Name Scanning]\n");
    RUN_TEST(xml_scan_name_simple);
    RUN_TEST(xml_scan_name_with_namespace);
    RUN_TEST(xml_scan_name_with_digits);
    RUN_TEST(xml_scan_name_with_hyphen_dot);
    RUN_TEST(xml_scan_name_empty);
    RUN_TEST(xml_scan_name_invalid_start);

    printf("\n[Whitespace Scanning]\n");
    RUN_TEST(xml_scan_whitespace_simple);
    RUN_TEST(xml_scan_whitespace_mixed);
    RUN_TEST(xml_scan_whitespace_none);
    RUN_TEST(xml_scan_whitespace_all);
    RUN_TEST(xml_scan_whitespace_empty);

    printf("\n[Whitespace-Only Check]\n");
    RUN_TEST(xml_is_whitespace_only_basic);
    RUN_TEST(xml_is_whitespace_only_long);

    printf("\n[SIMD Thresholds]\n");
    RUN_TEST(simd_threshold_whitespace);

    printf("\n[Edge Cases]\n");
    RUN_TEST(null_pointer_handling);
    RUN_TEST(start_after_end);
    RUN_TEST(single_character_names);
    RUN_TEST(unicode_name_start);

    printf("\n═══════════════════════════════════════════════════════════════\n");
    printf("Results: %d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf("\n═══════════════════════════════════════════════════════════════\n");

    return tests_failed > 0 ? 1 : 0;
}
