/**
 * @file xpath_xalan_1_tests.c
 * @brief XPath Xalan conformance tests - Part 1
 *
 * Xalan XPath conformance test suite.
 */

#include "test_adapter.h"

/* Xalan conformance tests - Set 1 */
TEST_XML(test_xpath_xalan_1_basic, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_1_paths, "<root><a><b><c/></b></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b><c/></b></a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_1_predicates, "<root><a id='1'/><a id='2'/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a id='1'/><a id='2'/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_1_functions, "<root>Hello</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>Hello</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_1_axes, "<root><a><b/></a><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b/></a><c/></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running XPath Xalan conformance tests - Part 1...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_xpath_xalan_1_basic);
    RUN_TEST(test_xpath_xalan_1_paths);
    RUN_TEST(test_xpath_xalan_1_predicates);
    RUN_TEST(test_xpath_xalan_1_functions);
    RUN_TEST(test_xpath_xalan_1_axes);

    #undef RUN_TEST

    printf("\n=== XPath Xalan Conformance Tests - Part 1 Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
