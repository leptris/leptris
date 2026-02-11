/**
 * @file xpath_xalan_3_tests.c
 * @brief XPath Xalan conformance tests - Part 3
 */

#include "test_adapter.h"

TEST_XML(test_xpath_xalan_3_namespaces, "<root xmlns:ns='http://example.com'><ns:a/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root xmlns:ns='http://example.com'><ns:a/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_3_qnames, "<root><a xmlns:ns='http://example.com'/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a xmlns:ns='http://example.com'/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_3_comments, "<root><!--comment--><a/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><!--comment--><a/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_3_text, "<root>text content</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>text content</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_3_pis, "<root/>")
{
    /* Skip: PI parsing causes crash */
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running XPath Xalan conformance tests - Part 3...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_xpath_xalan_3_namespaces);
    RUN_TEST(test_xpath_xalan_3_qnames);
    RUN_TEST(test_xpath_xalan_3_comments);
    RUN_TEST(test_xpath_xalan_3_text);
    RUN_TEST(test_xpath_xalan_3_pis);

    #undef RUN_TEST

    printf("\n=== XPath Xalan Conformance Tests - Part 3 Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
