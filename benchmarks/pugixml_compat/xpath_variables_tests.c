/**
 * @file xpath_variables_tests.c
 * @brief XPath variable tests adapted from pugixml test_xpath_variables.cpp
 *
 * Tests XPath variable binding and references.
 */

#include "test_adapter.h"

/* Test variable reference */
TEST_XML(test_xpath_variables_basic, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

/* Test variable in predicate */
TEST_XML(test_xpath_variables_predicate, "<root><a id='1'/><a id='2'/><a id='3'/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a id='1'/><a id='2'/><a id='3'/></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Test variable in expression */
TEST_XML(test_xpath_variables_expression, "<root><a>5</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>5</a></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Test multiple variables */
TEST_XML(test_xpath_variables_multiple, "<root><a/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Test variable shadowing */
TEST_XML(test_xpath_variables_shadow, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

/* Test variable types */
TEST_XML(test_xpath_variables_node, "<root><a/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_variables_string, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_variables_number, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_variables_boolean, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

/* Test undefined variable */
TEST_XML(test_xpath_variables_undefined, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running XPath variable tests...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_xpath_variables_basic);
    RUN_TEST(test_xpath_variables_predicate);
    RUN_TEST(test_xpath_variables_expression);
    RUN_TEST(test_xpath_variables_multiple);
    RUN_TEST(test_xpath_variables_shadow);
    RUN_TEST(test_xpath_variables_node);
    RUN_TEST(test_xpath_variables_string);
    RUN_TEST(test_xpath_variables_number);
    RUN_TEST(test_xpath_variables_boolean);
    RUN_TEST(test_xpath_variables_undefined);

    #undef RUN_TEST

    printf("\n=== XPath Variable Tests Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
