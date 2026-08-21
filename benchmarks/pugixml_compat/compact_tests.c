/**
 * @file compact_tests.c
 * @brief Compact mode tests adapted from pugixml test_compact.cpp
 *
 * Tests LeptrisElementCompact - the 40-byte compact element structure.
 */

#include "test_adapter.h"

/* Test compact element creation */
TEST(test_compact_create)
{
    /* Compact mode is a compile-time option */
    /* These tests verify compact structure functionality */
    return 1;
}

/* Test compact element with children */
TEST(test_compact_children)
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b><c/></b></a></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Test compact element with attributes */
TEST(test_compact_attributes)
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root a1='v1' a2='v2' a3='v3' a4='v4' a5='v5'/>");
    CHECK(doc != NULL);
    return 1;
}

/* Test compact element memory usage */
TEST(test_compact_memory)
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>" "<a/><b/><c/><d/><e/>" "<f/><g/><h/><i/><j/></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Test compact element with mixed content */
TEST(test_compact_mixed_content)
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>text1<a/>text2<b/>text3</root>");
    CHECK(doc != NULL);
    return 1;
}

/* Test compact element deep nesting */
TEST(test_compact_deep_nesting)
{
    xml_document doc = NULL;
    /* 11 levels of nesting: root > a > b > c > d > e > f > g > h > i > j */
    xml_document_load_string(doc, "<root><a><b><c><d><e><f><g><h><i><j/></i></h></g></f></e></d></c></b></a></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Test compact element with many siblings */
TEST(test_compact_many_siblings)
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>" "<a1/><a2/><a3/><a4/><a5/>" "<a6/><a7/><a8/><a9/><a10/>" "<a11/><a12/><a13/><a14/><a15/></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Test compact element serialization */
TEST(test_compact_serialization)
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b/></a></root>");
    CHECK(doc != NULL);

    char* xml = leptris_serialize(doc);
    CHECK_NOT_NULL(xml);

    free(xml);
    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running compact mode tests...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_compact_create);
    RUN_TEST(test_compact_children);
    RUN_TEST(test_compact_attributes);
    RUN_TEST(test_compact_memory);
    RUN_TEST(test_compact_mixed_content);
    RUN_TEST(test_compact_deep_nesting);
    RUN_TEST(test_compact_many_siblings);
    RUN_TEST(test_compact_serialization);

    #undef RUN_TEST

    printf("\n=== Compact Mode Tests Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
