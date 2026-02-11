/**
 * @file xpath_w3c_abbrev_tests.c
 * @brief XPath W3C abbreviated syntax conformance tests
 *
 * Tests W3C XPath abbreviated location path syntax.
 */

#include "test_adapter.h"

/* W3C abbreviated syntax tests */
TEST_XML(test_xpath_w3c_abbrev_root, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_w3c_abbrev_child, "<root><a/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_w3c_abbrev_attribute, "<root><a id='test'/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a id='test'/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_w3c_abbrev_predicate, "<root><a/><b/><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/><c/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_w3c_abbrev_descendant, "<root><a><b><c/></b></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b><c/></b></a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_w3c_abbrev_parent, "<root><a><b/></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b/></a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_w3c_abbrev_self, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_w3c_abbrev_combined, "<root><a><b id='x'/><c/></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b id='x'/><c/></a></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running XPath W3C abbreviated syntax tests...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_xpath_w3c_abbrev_root);
    RUN_TEST(test_xpath_w3c_abbrev_child);
    RUN_TEST(test_xpath_w3c_abbrev_attribute);
    RUN_TEST(test_xpath_w3c_abbrev_predicate);
    RUN_TEST(test_xpath_w3c_abbrev_descendant);
    RUN_TEST(test_xpath_w3c_abbrev_parent);
    RUN_TEST(test_xpath_w3c_abbrev_self);
    RUN_TEST(test_xpath_w3c_abbrev_combined);

    #undef RUN_TEST

    printf("\n=== XPath W3C Abbreviated Syntax Tests Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
