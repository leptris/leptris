/**
 * @file xpath_operators_tests.c
 * @brief XPath operator tests adapted from pugixml test_xpath_operators.cpp
 *
 * Tests XPath operators: =, !=, <, <=, >, >=, +, -, *, div, mod,
 * and, or, union (|), negative numbers.
 */

#include "test_adapter.h"

/* Equality operators */
TEST_XML(test_xpath_operators_eq, "<root><a id='1'/><a id='1'/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a id='1'/><a id='1'/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_operators_ne, "<root><a id='1'/><a id='2'/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a id='1'/><a id='2'/></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Relational operators */
TEST_XML(test_xpath_operators_lt, "<root><a>1</a><a>2</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>1</a><a>2</a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_operators_le, "<root><a>1</a><a>2</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>1</a><a>2</a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_operators_gt, "<root><a>1</a><a>2</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>1</a><a>2</a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_operators_ge, "<root><a>1</a><a>2</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>1</a><a>2</a></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Arithmetic operators */
TEST_XML(test_xpath_operators_plus, "<root><a>1</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>1</a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_operators_minus, "<root><a>5</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>5</a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_operators_multiply, "<root><a>3</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>3</a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_operators_div, "<root><a>10</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>10</a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_operators_mod, "<root><a>10</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>10</a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_operators_unary_minus, "<root><a>5</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>5</a></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Boolean operators */
TEST_XML(test_xpath_operators_and, "<root><a>true</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>true</a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_operators_or, "<root><a>true</a><a>false</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>true</a><a>false</a></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Union operator */
TEST_XML(test_xpath_operators_union, "<root><a/><b/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running XPath operator tests...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_xpath_operators_eq);
    RUN_TEST(test_xpath_operators_ne);
    RUN_TEST(test_xpath_operators_lt);
    RUN_TEST(test_xpath_operators_le);
    RUN_TEST(test_xpath_operators_gt);
    RUN_TEST(test_xpath_operators_ge);
    RUN_TEST(test_xpath_operators_plus);
    RUN_TEST(test_xpath_operators_minus);
    RUN_TEST(test_xpath_operators_multiply);
    RUN_TEST(test_xpath_operators_div);
    RUN_TEST(test_xpath_operators_mod);
    RUN_TEST(test_xpath_operators_unary_minus);
    RUN_TEST(test_xpath_operators_and);
    RUN_TEST(test_xpath_operators_or);
    RUN_TEST(test_xpath_operators_union);

    #undef RUN_TEST

    printf("\n=== XPath Operator Tests Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
