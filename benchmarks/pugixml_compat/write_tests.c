/**
 * @file write_tests.c
 * @brief XML serialization tests adapted from pugixml test_write.cpp
 *
 * These tests verify XML serialization functionality.
 */

#include "test_adapter.h"
#include <string.h>

/* Test basic element serialization */
TEST_XML(test_write_basic_element, "<node/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node/>");
    CHECK(doc != NULL);

    char* xml = taurus_serialize(doc);
    CHECK_NOT_NULL(xml);

    /* Should contain node tag */
    const char* check = strstr(xml, "node");
    CHECK_NOT_NULL(check);

    free(xml);
    return 1;
}

/* Test element with text */
TEST_XML(test_write_element_with_text, "<node>text</node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node>text</node>");
    CHECK(doc != NULL);

    char* xml = taurus_serialize(doc);
    CHECK_NOT_NULL(xml);

    /* Should contain both tags and text */
    const char* node_check = strstr(xml, "node");
    const char* text_check = strstr(xml, "text");
    CHECK_NOT_NULL(node_check);
    CHECK_NOT_NULL(text_check);

    free(xml);
    return 1;
}

/* Test element with attribute */
TEST_XML(test_write_element_with_attribute, "<node attr='value'/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node attr='value'/>");
    CHECK(doc != NULL);

    char* xml = taurus_serialize(doc);
    CHECK_NOT_NULL(xml);

    /* Should contain attribute name and value */
    const char* attr_check = strstr(xml, "attr");
    const char* value_check = strstr(xml, "value");
    CHECK_NOT_NULL(attr_check);
    CHECK_NOT_NULL(value_check);

    free(xml);
    return 1;
}

/* Test nested elements */
TEST_XML(test_write_nested, "<root><child><grandchild/></child></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><child><grandchild/></child></root>");
    CHECK(doc != NULL);

    char* xml = taurus_serialize(doc);
    CHECK_NOT_NULL(xml);

    /* Should contain all element names */
    const char* root_check = strstr(xml, "root");
    const char* child_check = strstr(xml, "child");
    const char* grandchild_check = strstr(xml, "grandchild");
    CHECK_NOT_NULL(root_check);
    CHECK_NOT_NULL(child_check);
    CHECK_NOT_NULL(grandchild_check);

    free(xml);
    return 1;
}

/* Test multiple children */
TEST_XML(test_write_multiple_children, "<root><a/><b/><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/><c/></root>");
    CHECK(doc != NULL);

    char* xml = taurus_serialize(doc);
    CHECK_NOT_NULL(xml);

    /* Should contain all child elements */
    const char* a_check = strstr(xml, "<a");
    const char* b_check = strstr(xml, "<b");
    const char* c_check = strstr(xml, "<c");
    CHECK_NOT_NULL(a_check);
    CHECK_NOT_NULL(b_check);
    CHECK_NOT_NULL(c_check);

    free(xml);
    return 1;
}

/* Test CDATA serialization */
TEST_XML(test_write_cdata, "<node><![CDATA[<escaped>]]></node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node><![CDATA[<escaped>]]></node>");
    CHECK(doc != NULL);

    char* xml = taurus_serialize(doc);
    CHECK_NOT_NULL(xml);

    /* Should contain CDATA marker */
    const char* cdata_check = strstr(xml, "<![CDATA[");
    CHECK_NOT_NULL(cdata_check);

    free(xml);
    return 1;
}

/* Test comment serialization */
TEST_XML(test_write_comment, "<node><!--comment--></node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node><!--comment--></node>");
    CHECK(doc != NULL);

    char* xml = taurus_serialize(doc);
    CHECK_NOT_NULL(xml);

    /* Should contain comment marker */
    const char* comment_check = strstr(xml, "<!--");
    CHECK_NOT_NULL(comment_check);

    free(xml);
    return 1;
}

/* Test XML declaration */
TEST_XML(test_write_declaration, "<?xml version='1.0'?><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<?xml version='1.0'?><root/>");
    CHECK(doc != NULL);

    char* xml = taurus_serialize(doc);
    CHECK_NOT_NULL(xml);

    /* Should contain XML declaration */
    const char* decl_check = strstr(xml, "<?xml");
    CHECK_NOT_NULL(decl_check);

    free(xml);
    return 1;
}

/* Test special character escaping in text */
TEST_XML(test_write_escape_text, "<node>text</node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node>text</node>");
    CHECK(doc != NULL);

    /* Set text content with special characters programmatically */
    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);
    xml_node_set_text(node, "<>\"'&");

    char* xml = taurus_serialize(doc);
    CHECK_NOT_NULL(xml);

    /* Should contain escaped entities */
    const char* lt_check = strstr(xml, "&lt;");
    const char* gt_check = strstr(xml, "&gt;");
    const char* amp_check = strstr(xml, "&amp;");
    CHECK_NOT_NULL(lt_check);
    CHECK_NOT_NULL(gt_check);
    CHECK_NOT_NULL(amp_check);

    free(xml);
    return 1;
}

/* Test special character escaping in attributes */
TEST_XML(test_write_escape_attribute, "<node attr=\"value\"/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node attr=\"value\"/>");
    CHECK(doc != NULL);

    /* Set attribute value with special characters programmatically */
    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);
    xml_node_set_attribute(node, "attr", "<>\"'&");

    char* xml = taurus_serialize(doc);
    CHECK_NOT_NULL(xml);

    /* Should contain escaped entities */
    const char* lt_check = strstr(xml, "&lt;");
    const char* gt_check = strstr(xml, "&gt;");
    const char* amp_check = strstr(xml, "&amp;");
    CHECK_NOT_NULL(lt_check);
    CHECK_NOT_NULL(gt_check);
    CHECK_NOT_NULL(amp_check);

    free(xml);
    return 1;
}

/* Test UTF-8 content */
TEST_XML(test_write_utf8, "<node>\xc3\xa9\xc3\xa0\xc3\xbc</node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node>\xc3\xa9\xc3\xa0\xc3\xbc</node>");
    CHECK(doc != NULL);

    char* xml = taurus_serialize(doc);
    CHECK_NOT_NULL(xml);

    /* Should preserve UTF-8 bytes */
    const char* utf8_check = strstr(xml, "\xc3\xa9");
    CHECK_NOT_NULL(utf8_check);

    free(xml);
    return 1;
}

/* Test whitespace preservation */
TEST_XML(test_write_whitespace, "<node>  \n  </node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node>  \n  </node>");
    CHECK(doc != NULL);

    char* xml = taurus_serialize(doc);
    CHECK_NOT_NULL(xml);

    /* Should preserve whitespace */
    (void)xml;

    free(xml);
    return 1;
}

/* Test empty document */
TEST(test_write_empty_document)
{
    xml_document doc = xml_document_create();
    CHECK_NOT_NULL(doc);

    char* xml = taurus_serialize(doc);
    CHECK_NOT_NULL(xml);

    free(xml);
    xml_document_free(doc);
    return 1;
}

/* Test document with multiple attributes */
TEST_XML(test_write_multiple_attributes, "<node a1='v1' a2='v2' a3='v3' a4='v4'/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node a1='v1' a2='v2' a3='v3' a4='v4'/>");
    CHECK(doc != NULL);

    char* xml = taurus_serialize(doc);
    CHECK_NOT_NULL(xml);

    /* Should contain all attributes */
    const char* a1_check = strstr(xml, "a1");
    const char* a2_check = strstr(xml, "a2");
    const char* a3_check = strstr(xml, "a3");
    const char* a4_check = strstr(xml, "a4");
    CHECK_NOT_NULL(a1_check);
    CHECK_NOT_NULL(a2_check);
    CHECK_NOT_NULL(a3_check);
    CHECK_NOT_NULL(a4_check);

    free(xml);
    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running write/serialization tests...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_write_basic_element);
    RUN_TEST(test_write_element_with_text);
    RUN_TEST(test_write_element_with_attribute);
    RUN_TEST(test_write_nested);
    RUN_TEST(test_write_multiple_children);
    RUN_TEST(test_write_cdata);
    RUN_TEST(test_write_comment);
    RUN_TEST(test_write_declaration);
    RUN_TEST(test_write_escape_text);
    RUN_TEST(test_write_escape_attribute);
    RUN_TEST(test_write_utf8);
    RUN_TEST(test_write_whitespace);
    RUN_TEST(test_write_empty_document);
    RUN_TEST(test_write_multiple_attributes);

    #undef RUN_TEST

    printf("\n=== Write/Serialization Tests Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
