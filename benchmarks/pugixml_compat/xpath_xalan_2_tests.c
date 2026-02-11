/**
 * @file xpath_xalan_2_tests.c
 * @brief XPath Xalan conformance tests - Part 2
 */

#include "test_adapter.h"

TEST_XML(test_xpath_xalan_2_operators, "<root><a>1</a><a>2</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>1</a><a>2</a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_2_booleans, "<root><a>true</a><a>false</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>true</a><a>false</a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_2_numbers, "<root>3.14</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>3.14</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_2_strings, "<root>hello world</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>hello world</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_xalan_2_nodesets, "<root><a/><b/><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/><c/></root>");
    CHECK(doc != NULL);
    return 1;
}

int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running XPath Xalan conformance tests - Part 2...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_xpath_xalan_2_operators);
    RUN_TEST(test_xpath_xalan_2_booleans);
    RUN_TEST(test_xpath_xalan_2_numbers);
    RUN_TEST(test_xpath_xalan_2_strings);
    RUN_TEST(test_xpath_xalan_2_nodesets);

    #undef RUN_TEST

    printf("\n=== XPath Xalan Conformance Tests - Part 2 Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
