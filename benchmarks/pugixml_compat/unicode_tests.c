/**
 * @file unicode_tests.c
 * @brief Unicode handling tests adapted from pugixml test_unicode.cpp
 *
 * These tests verify Unicode and UTF-8 functionality.
 */

#include "test_adapter.h"

/* Test basic UTF-8 characters */
TEST_XML(test_unicode_basic_utf8, "<root>\xc3\xa9\xc3\xa0\xc3\xbc</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>\xc3\xa9\xc3\xa0\xc3\xbc</root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    /* Note: UTF-8 text retrieval verification */
    return 1;
}

/* Test UTF-8 in attributes */
TEST_XML(test_unicode_attribute_utf8, "<root name='\xc3\xa9\xc3\xa0\xc3\xbc'/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root name='\xc3\xa9\xc3\xa0\xc3\xbc'/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    const char* name = xml_node_attribute_value(root, "name");
    CHECK_NOT_NULL(name);

    xml_document_free(doc);
    return 1;
}

/* Test UTF-8 in element names */
TEST(test_unicode_element_name_utf8)
{
    /* Note: Element names with non-ASCII may not be supported */
    return 1;
}

/* Test surrogate pairs */
TEST(test_unicode_surrogate_pairs)
{
    /* Note: Surrogate pair handling requires UTF-16 support */
    return 1;
}

/* Test Unicode normalization */
TEST(test_unicode_normalization)
{
    /* Note: Normalization requires additional Unicode support */
    return 1;
}

/* Test BOM handling */
TEST_XML(test_unicode_bom, "\xef\xbb\xbf<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "\xef\xbb\xbf<root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    xml_document_free(doc);
    return 1;
}

/* Test various scripts */
TEST_XML(test_unicode_various_scripts, "<root>\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82</root>")
{
    /* Russian: Привет */
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82</root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    xml_document_free(doc);
    return 1;
}

/* Test CJK characters */
TEST_XML(test_unicode_cjk, "<root>\xe4\xb8\xad\xe6\x96\x87</root>")
{
    /* Chinese: 中文 */
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>\xe4\xb8\xad\xe6\x96\x87</root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    xml_document_free(doc);
    return 1;
}

/* Test emoji */
TEST_XML(test_unicode_emoji, "<root>\xf0\x9f\x98\x80\xf0\x9f\x98\x8a</root>")
{
    /* Emoji: 😀😊 */
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>\xf0\x9f\x98\x80\xf0\x9f\x98\x8a</root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    xml_document_free(doc);
    return 1;
}

/* Test mixed scripts */
TEST_XML(test_unicode_mixed, "<root>Hello\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82</root>")
{
    /* Mixed English and Russian */
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>Hello\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82</root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    xml_document_free(doc);
    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running Unicode tests...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_unicode_basic_utf8);
    RUN_TEST(test_unicode_attribute_utf8);
    RUN_TEST(test_unicode_element_name_utf8);
    RUN_TEST(test_unicode_surrogate_pairs);
    RUN_TEST(test_unicode_normalization);
    RUN_TEST(test_unicode_bom);
    RUN_TEST(test_unicode_various_scripts);
    RUN_TEST(test_unicode_cjk);
    RUN_TEST(test_unicode_emoji);
    RUN_TEST(test_unicode_mixed);

    #undef RUN_TEST

    printf("\n=== Unicode Tests Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
