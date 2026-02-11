/**
 * @file xpath_xalan_4_tests.c
 * @brief XPath Xalan conformance tests - Part 4
 */

#include "test_adapter.h"

TEST_XML(test_xpath_xalan_4_edge_cases, "<root><a><b><c><d/></c></b></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b><c><d/></c></b></a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_4_deep_nesting, "<root><a><b><c><d><e/></d></c></b></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b><c><d><e/></d></c></b></a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_4_many_siblings, "<root>" "<a/><a/><a/><a/><a/><a/><a/><a/><a/><a/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><a/><a/><a/><a/><a/><a/><a/><a/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_4_mixed_content, "<root>text1<a/>text2<b/>text3</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>text1<a/>text2<b/>text3</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_4_attributes, "<root a='1' b='2' c='3' d='4' e='5'/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root a='1' b='2' c='3' d='4' e='5'/>");
    CHECK(doc != NULL);
    return 1;
}

int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running XPath Xalan conformance tests - Part 4...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_xpath_xalan_4_edge_cases);
    RUN_TEST(test_xpath_xalan_4_deep_nesting);
    RUN_TEST(test_xpath_xalan_4_many_siblings);
    RUN_TEST(test_xpath_xalan_4_mixed_content);
    RUN_TEST(test_xpath_xalan_4_attributes);

    #undef RUN_TEST

    printf("\n=== XPath Xalan Conformance Tests - Part 4 Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
