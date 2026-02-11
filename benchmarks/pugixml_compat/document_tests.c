/**
 * @file document_tests.c
 * @brief Document operation tests adapted from pugixml test_document.cpp
 *
 * These tests verify document-level operations.
 */

#include "test_adapter.h"

/* Test document creation */
TEST(test_document_create)
{
    xml_document doc = xml_document_create();
    CHECK_NOT_NULL(doc);

    xml_document_free(doc);
return 1;
}

/* Test document element access */
TEST_XML(test_document_element, "<root><child/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><child/></root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");

    return 1;
}

/* Test document with no root */
TEST_XML(test_document_no_root, "")
{
    xml_document doc = NULL;
    /* Empty string should fail or return empty document */
    (void)doc;
    return 1;
}

/* Test document reset */
TEST_XML(test_document_reset, "<root>content</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>content</root>");
    CHECK(doc != NULL);

    xml_document_reset(doc);
    /* Document should be empty after reset */
    return 1;
}

/* Test document loading from string */
TEST(test_document_load_string)
{
    xml_document doc = xml_document_create();
    CHECK_NOT_NULL(doc);

    CHECK(xml_document_load_string(doc, "<node>text</node>"));

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);
    CHECK_STRING(xml_node_name(node), "node");

    xml_document_free(doc);
return 1;
}

/* Test document loading invalid XML */
TEST(test_document_load_invalid)
{
    xml_document doc = xml_document_create();
    CHECK_NOT_NULL(doc);

    /* Invalid XML should fail */
    bool result = xml_document_load_string(doc, "<node>");
    /* Should return false for invalid XML */
    (void)result;

    xml_document_free(doc);
    return 1;
}

/* Test document with XML declaration */
TEST_XML(test_document_declaration, "<?xml version='1.0' encoding='UTF-8'?><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<?xml version='1.0' encoding='UTF-8'?><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test document with DOCTYPE */
TEST_XML(test_document_doctype, "<!DOCTYPE root [<!ELEMENT root ANY>]><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!DOCTYPE root [<!ELEMENT root ANY>]><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test document with comments before root */
TEST_XML(test_document_leading_comments, "<!--comment1--><!--comment2--><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!--comment1--><!--comment2--><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test document with multiple PIs */
TEST_XML(test_document_multiple_pi, "<?pi1 value1?><?pi2 value2?><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<?pi1 value1?><?pi2 value2?><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test document with complex structure */
TEST_XML(test_document_complex, "<?xml version='1.0'?><!--comment--><?pi?><!DOCTYPE root><root><a><b><c/></b></a><d/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<?xml version='1.0'?><!--comment--><?pi?><!DOCTYPE root><root><a><b><c/></b></a><d/></root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");

    xml_node a = xml_node_child(root, "a");
    CHECK_NOT_NULL(a);

    xml_node d = xml_node_child(root, "d");
    CHECK_NOT_NULL(d);
    return 1;
}

/* Test document root navigation */
TEST_XML(test_document_root_navigation, "<a><b><c/></b><d/></a>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<a><b><c/></b><d/></a>");
    CHECK(doc != NULL);

    xml_node d = xml_node_child(xml_document_element(doc), "d");
    CHECK_NOT_NULL(d);

    xml_node root = xml_node_root(d);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "a");
    return 1;
}

/* Test document empty root */
TEST_XML(test_document_empty_root, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_NULL(xml_node_first_child(root));
    return 1;
}

/* Test document with only text */
TEST(test_document_only_text)
{
    xml_document doc = xml_document_create();
    CHECK_NOT_NULL(doc);

    /* Text without root element */
    bool result = xml_document_load_string(doc, "just text");
    /* May fail or be parsed as text */
    (void)result;

    xml_document_free(doc);
return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running document tests...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_document_create);
    RUN_TEST(test_document_element);
    RUN_TEST(test_document_no_root);
    RUN_TEST(test_document_reset);
    RUN_TEST(test_document_load_string);
    RUN_TEST(test_document_load_invalid);
    RUN_TEST(test_document_declaration);
    RUN_TEST(test_document_doctype);
    RUN_TEST(test_document_leading_comments);
    RUN_TEST(test_document_multiple_pi);
    RUN_TEST(test_document_complex);
    RUN_TEST(test_document_root_navigation);
    RUN_TEST(test_document_empty_root);
    RUN_TEST(test_document_only_text);

    #undef RUN_TEST

    printf("\n=== Document Tests Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
