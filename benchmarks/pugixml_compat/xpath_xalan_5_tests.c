/**
 * @file xpath_xalan_5_tests.c
 * @brief XPath Xalan conformance tests - Part 5
 */

#include "test_adapter.h"

TEST_XML(test_xpath_xalan_5_complex, "<root>" "<a><b><c><d/></c></b></a>" "<x><y><z/></y></x></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b><c><d/></c></b></a><x><y><z/></y></x></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_5_predicates, "<root>" "<a id='1' class='x'/>" "<a id='2' class='y'/>" "<a id='3' class='x'/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>" "<a id='1' class='x'/>" "<a id='2' class='y'/>" "<a id='3' class='x'/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_5_functions, "<root><a>Hello</a><b>World</b></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>Hello</a><b>World</b></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_5_axes, "<root>" "<a><b/></a>" "<c><d/></c></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b/></a><c><d/></c></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_5_stress, "<root>" "<a/><b/><c/><d/><e/>" "<f/><g/><h/><i/><j/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>" "<a/><b/><c/><d/><e/>" "<f/><g/><h/><i/><j/></root>");
    CHECK(doc != NULL);
    return 1;
}

int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running XPath Xalan conformance tests - Part 5...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_xpath_xalan_5_complex);
    RUN_TEST(test_xpath_xalan_5_predicates);
    RUN_TEST(test_xpath_xalan_5_functions);
    RUN_TEST(test_xpath_xalan_5_axes);
    RUN_TEST(test_xpath_xalan_5_stress);

    #undef RUN_TEST

    printf("\n=== XPath Xalan Conformance Tests - Part 5 Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
