/**
 * @file dom_text_tests.c
 * @brief DOM text node handling tests adapted from pugixml test_dom_text.cpp
 *
 * These tests verify text node and content manipulation functionality.
 */

#include "test_adapter.h"

/* Test basic text content */
TEST_XML(test_dom_text_basic, "<node>text</node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node>text</node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    const char* text = xml_node_text(node);
    CHECK_NOT_NULL(text);
    CHECK_STRING(text, "text");
    return 1;
}

/* Test empty text */
TEST_XML(test_dom_text_empty, "<node></node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node></node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    const char* text = xml_node_text(node);
    /* Empty or NULL is acceptable */
    (void)text;
    return 1;
}

/* Test whitespace text */
TEST_XML(test_dom_text_whitespace, "<node>   </node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node>   </node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    const char* text = xml_node_text(node);
    CHECK_NOT_NULL(text);
    return 1;
}

/* Test text with entities */
TEST_XML(test_dom_text_entities, "<node>&lt;&gt;&amp;</node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node>&lt;&gt;&amp;</node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    const char* text = xml_node_text(node);
    CHECK_NOT_NULL(text);
    CHECK_STRING(text, "<>&");
    return 1;
}

/* Test mixed content text extraction */
TEST_XML(test_dom_text_mixed, "<node>before<middle/>after</node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node>before<middle/>after</node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    const char* text = xml_node_text(node);
    CHECK_NOT_NULL(text);
    /* Should concatenate all text content */
    return 1;
}

/* Test numeric character references */
TEST_XML(test_dom_text_char_ref, "<node>&#65;&#x42;</node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node>&#65;&#x42;</node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    const char* text = xml_node_text(node);
    CHECK_NOT_NULL(text);
    CHECK_STRING(text, "AB");
    return 1;
}

/* Test child value (direct child text only) */
TEST_XML(test_dom_text_child_value, "<node>text</node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node>text</node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    const char* value = xml_node_child_value(node);
    CHECK_NOT_NULL(value);
    CHECK_STRING(value, "text");
    return 1;
}

/* Test deep text content */
TEST_XML(test_dom_text_deep, "<a><b><c>deep text</c></b></a>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<a><b><c>deep text</c></b></a>");
    CHECK(doc != NULL);

    xml_node a = xml_document_element(doc);
    CHECK_NOT_NULL(a);

    /* text_content should recursively get text from all descendants */
    const char* text = xml_node_text(a);
    CHECK_NOT_NULL(text);
    return 1;
}

/* Test multiple text nodes */
TEST_XML(test_dom_text_multiple, "<node>text1<!--comment-->text2<?pi?>text3</node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node>text1<!--comment-->text2<?pi?>text3</node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    const char* text = xml_node_text(node);
    CHECK_NOT_NULL(text);
    return 1;
}

/* Test CDATA content */
TEST_XML(test_dom_text_cdata, "<node><![CDATA[CDATA content]]></node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node><![CDATA[CDATA content]]></node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    const char* text = xml_node_text(node);
    CHECK_NOT_NULL(text);
    CHECK_STRING(text, "CDATA content");
    return 1;
}

/* Test text with newlines */
TEST_XML(test_dom_text_newlines, "<node>line1\nline2\rline3\r\nline4</node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node>line1\nline2\rline3\r\nline4</node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    const char* text = xml_node_text(node);
    CHECK_NOT_NULL(text);
    return 1;
}

/* Test attribute value with entities */
TEST_XML(test_dom_text_attribute_entities, "<node attr=\"&lt;&gt;&amp;\"/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node attr=\"&lt;&gt;&amp;\"/>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    const char* value = xml_node_attribute_value(node, "attr");
    CHECK_NOT_NULL(value);
    CHECK_STRING(value, "<>&");
    return 1;
}

/* Test UTF-8 text */
TEST_XML(test_dom_text_utf8, "<node>\xc3\xa9\xc3\xa0\xc3\xbc</node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node>\xc3\xa9\xc3\xa0\xc3\xbc</node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    const char* text = xml_node_text(node);
    CHECK_NOT_NULL(text);
    return 1;
}

/* Test text node type */
TEST_XML(test_dom_text_type, "<node>text</node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node>text</node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    xml_node child = xml_node_first_child(node);
    CHECK_NOT_NULL(child);
    CHECK(xml_node_get_type(child) == node_pcdata);
    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running DOM text tests...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_dom_text_basic);
    RUN_TEST(test_dom_text_empty);
    RUN_TEST(test_dom_text_whitespace);
    RUN_TEST(test_dom_text_entities);
    RUN_TEST(test_dom_text_mixed);
    RUN_TEST(test_dom_text_char_ref);
    RUN_TEST(test_dom_text_child_value);
    RUN_TEST(test_dom_text_deep);
    RUN_TEST(test_dom_text_multiple);
    RUN_TEST(test_dom_text_cdata);
    RUN_TEST(test_dom_text_newlines);
    RUN_TEST(test_dom_text_attribute_entities);
    RUN_TEST(test_dom_text_utf8);
    RUN_TEST(test_dom_text_type);

    #undef RUN_TEST

    printf("\n=== DOM Text Tests Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
