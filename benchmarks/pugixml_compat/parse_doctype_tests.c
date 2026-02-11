/**
 * @file parse_doctype_tests.c
 * @brief DOCTYPE parsing tests adapted from pugixml test_parse_doctype.cpp
 *
 * These tests verify DOCTYPE declaration parsing functionality.
 */

#include "test_adapter.h"

/* Test basic DOCTYPE */
TEST_XML(test_parse_doctype_basic, "<!DOCTYPE root><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!DOCTYPE root><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test DOCTYPE with system identifier */
TEST_XML(test_parse_doctype_system, "<!DOCTYPE root SYSTEM 'http://example.com/dtd'><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!DOCTYPE root SYSTEM 'http://example.com/dtd'><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test DOCTYPE with public identifier */
TEST_XML(test_parse_doctype_public, "<!DOCTYPE root PUBLIC 'public-id' 'system-id'><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!DOCTYPE root PUBLIC 'public-id' 'system-id'><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test DOCTYPE with internal subset */
TEST_XML(test_parse_doctype_internal, "<!DOCTYPE root [<!ELEMENT root ANY>]><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!DOCTYPE root [<!ELEMENT root ANY>]><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test DOCTYPE with multiple declarations */
TEST_XML(test_parse_doctype_multiple, "<!DOCTYPE root [<!ELEMENT root ANY><!ATTLIST root attr CDATA #IMPLIED>]><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!DOCTYPE root [<!ELEMENT root ANY><!ATTLIST root attr CDATA #IMPLIED>]><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test DOCTYPE with entity declaration */
TEST_XML(test_parse_doctype_entity, "<!DOCTYPE root [<!ENTITY test 'value']><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!DOCTYPE root [<!ENTITY test 'value']><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test DOCTYPE with notation declaration */
TEST_XML(test_parse_doctype_notation, "<!DOCTYPE root [<!NOTATION gif SYSTEM 'image/gif'>]><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!DOCTYPE root [<!NOTATION gif SYSTEM 'image/gif'>]><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test DOCTYPE in document with content */
TEST_XML(test_parse_doctype_with_content, "<!DOCTYPE html><html><head/><body/></html>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!DOCTYPE html><html><head/><body/></html>");
    CHECK(doc != NULL);

    xml_node html = xml_document_element(doc);
    CHECK_NOT_NULL(html);
    CHECK_STRING(xml_node_name(html), "html");
    return 1;
}

/* Test DOCTYPE before XML declaration */
TEST_XML(test_parse_doctype_after_declaration, "<?xml version='1.0'?><!DOCTYPE root><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<?xml version='1.0'?><!DOCTYPE root><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test DOCTYPE with comments */
TEST_XML(test_parse_doctype_with_comments, "<!DOCTYPE root <!--comment--><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!DOCTYPE root <!--comment--><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test DOCTYPE with parameter entities */
TEST_XML(test_parse_doctype_param_entity, "<!DOCTYPE root [<!ENTITY % pe 'value'>]><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!DOCTYPE root [<!ENTITY % pe 'value'>]><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test HTML5 DOCTYPE */
TEST_XML(test_parse_doctype_html5, "<!DOCTYPE html><html><head/><body/></html>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!DOCTYPE html><html><head/><body/></html>");
    CHECK(doc != NULL);

    xml_node html = xml_document_element(doc);
    CHECK_NOT_NULL(html);
    CHECK_STRING(xml_node_name(html), "html");
    return 1;
}

/* Test DOCTYPE with different quote styles */
TEST_XML(test_parse_doctype_quotes, "<!DOCTYPE root PUBLIC \"public-id\" 'system-id'><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!DOCTYPE root PUBLIC \"public-id\" 'system-id'><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test DOCTYPE with nested brackets */
TEST_XML(test_parse_doctype_nested, "<!DOCTYPE root [<!ELEMENT root (a,b)>]><root><a/><b/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<!DOCTYPE root [<!ELEMENT root (a,b)>]><root><a/><b/></root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running DOCTYPE parsing tests...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_parse_doctype_basic);
    RUN_TEST(test_parse_doctype_system);
    RUN_TEST(test_parse_doctype_public);
    RUN_TEST(test_parse_doctype_internal);
    RUN_TEST(test_parse_doctype_multiple);
    RUN_TEST(test_parse_doctype_entity);
    RUN_TEST(test_parse_doctype_notation);
    RUN_TEST(test_parse_doctype_with_content);
    RUN_TEST(test_parse_doctype_after_declaration);
    RUN_TEST(test_parse_doctype_with_comments);
    RUN_TEST(test_parse_doctype_param_entity);
    RUN_TEST(test_parse_doctype_html5);
    RUN_TEST(test_parse_doctype_quotes);
    RUN_TEST(test_parse_doctype_nested);

    #undef RUN_TEST

    printf("\n=== DOCTYPE Parsing Tests Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
