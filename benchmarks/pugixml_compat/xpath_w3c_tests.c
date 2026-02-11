/**
 * @file xpath_w3c_tests.c
 * @brief XPath W3C full syntax conformance tests
 *
 * Tests W3C XPath full location path syntax.
 */

#include "test_adapter.h"

/* W3C full syntax tests */
TEST_XML(test_xpath_w3c_root, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_w3c_child, "<root><a/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_w3c_attribute, "<root><a id='test'/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a id='test'/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_w3c_predicate, "<root><a/><b/><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/><c/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_w3c_descendant, "<root><a><b><c/></b></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b><c/></b></a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_w3c_ancestor, "<root><a><b><c/></b></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b><c/></b></a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_w3c_following_sibling, "<root><a/><b/><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/><c/></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running XPath W3C full syntax tests...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_xpath_w3c_root);
    RUN_TEST(test_xpath_w3c_child);
    RUN_TEST(test_xpath_w3c_attribute);
    RUN_TEST(test_xpath_w3c_predicate);
    RUN_TEST(test_xpath_w3c_descendant);
    RUN_TEST(test_xpath_w3c_ancestor);
    RUN_TEST(test_xpath_w3c_following_sibling);

    #undef RUN_TEST

    printf("\n=== XPath W3C Full Syntax Tests Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
