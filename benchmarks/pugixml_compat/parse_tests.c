/**
 * @file parse_tests.c
 * @brief Basic XML parsing tests adapted from pugixml test_parse.cpp
 */

#include "test_adapter.h"

/* Test basic element parsing */
TEST_XML(test_parse_basic_element, "<root><child/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><child/></root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");

    xml_node child = xml_node_first_child(root);
    CHECK_NOT_NULL(child);
    CHECK_STRING(xml_node_name(child), "child");
    return 1;
}

/* Test nested elements */
TEST_XML(test_parse_nested_elements, "<root><a><b><c/></b></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b><c/></b></a></root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_STRING(xml_node_name(root), "root");

    xml_node a = xml_node_first_child(root);
    CHECK_STRING(xml_node_name(a), "a");

    xml_node b = xml_node_first_child(a);
    CHECK_STRING(xml_node_name(b), "b");

    xml_node c = xml_node_first_child(b);
    CHECK_STRING(xml_node_name(c), "c");
    return 1;
}

/* Test text content parsing */
TEST_XML(test_parse_text_content, "<root>Hello world</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>Hello world</root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    const char* text = xml_node_text(root);
    CHECK_NOT_NULL(text);
    CHECK_STRING(text, "Hello world");
    return 1;
}

/* Test attribute parsing */
TEST_XML(test_parse_attributes, "<root id='123' name='test'/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root id='123' name='test'/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    const char* id = xml_node_attribute_value(root, "id");
    CHECK_STRING(id, "123");

    const char* name = xml_node_attribute_value(root, "name");
    CHECK_STRING(name, "test");
    return 1;
}

/* Test mixed content */
TEST_XML(test_parse_mixed_content, "<root>Text1<child/>Text2</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>Text1<child/>Text2</root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    xml_node first = xml_node_first_child(root);
    CHECK_NOT_NULL(first);

    xml_node second = xml_node_next_sibling(first);
    CHECK_NOT_NULL(second);
    CHECK_STRING(xml_node_name(second), "child");
    return 1;
}

/* Test sibling elements */
TEST_XML(test_parse_siblings, "<root><a/><b/><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/><c/></root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    xml_node a = xml_node_first_child(root);
    CHECK_STRING(xml_node_name(a), "a");

    xml_node b = xml_node_next_sibling(a);
    CHECK_STRING(xml_node_name(b), "b");

    xml_node c = xml_node_next_sibling(b);
    CHECK_STRING(xml_node_name(c), "c");
    return 1;
}

/* Test CDATA sections */
TEST_XML(test_parse_cdata, "<root><![CDATA[<escaped>]]></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><![CDATA[<escaped>]]></root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    const char* text = xml_node_text(root);
    CHECK_NOT_NULL(text);
    CHECK_STRING(text, "<escaped>");
    return 1;
}

/* Test XML declaration */
TEST_XML(test_parse_declaration, "<?xml version='1.0'?><root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<?xml version='1.0'?><root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "root");
    return 1;
}

/* Test comments */
TEST_XML(test_parse_comment, "<root><!--comment--><child/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><!--comment--><child/></root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    /* Leptris always parses comments, find child element */
    xml_node child = xml_node_first_child(root);
    CHECK_NOT_NULL(child);

    /* Skip non-element nodes (comments, text, etc.), find element */
    while (child) {
        const char* child_name = xml_node_name(child);
        if (child_name && strcmp(child_name, "child") == 0) {
            break;
        }
        child = xml_node_next_sibling(child);
    }

    CHECK_NOT_NULL(child);
    CHECK_STRING(xml_node_name(child), "child");
    return 1;
}

/* Test empty document */
TEST_XML(test_parse_empty, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK(xml_node_first_child(root) == NULL);
    return 1;
}

/* Test whitespace preservation */
TEST_XML(test_parse_whitespace, "<root>  \n  </root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>  \n  </root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    const char* text = xml_node_text(root);
    CHECK_NOT_NULL(text);
    return 1;
}

/* Test multiple attributes */
TEST_XML(test_parse_multiple_attributes, "<root a1='v1' a2='v2' a3='v3' a4='v4'/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root a1='v1' a2='v2' a3='v3' a4='v4'/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    CHECK_STRING(xml_node_attribute_value(root, "a1"), "v1");
    CHECK_STRING(xml_node_attribute_value(root, "a2"), "v2");
    CHECK_STRING(xml_node_attribute_value(root, "a3"), "v3");
    CHECK_STRING(xml_node_attribute_value(root, "a4"), "v4");
    return 1;
}

/* Test attribute with special characters */
TEST_XML(test_parse_attribute_special, "<root attr='&lt;&gt;&amp;&quot;&apos;'/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root attr='&lt;&gt;&amp;&quot;&apos;'/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    const char* value = xml_node_attribute_value(root, "attr");
    CHECK_NOT_NULL(value);
    /* Input: &lt;&gt;&amp;&quot;&apos; decodes to: <>&"' */
    CHECK_STRING(value, "<>&\"'");
    return 1;
}

/* Test text with entities */
TEST_XML(test_parse_text_entities, "<root>&lt;tag&gt; &amp; text</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>&lt;tag&gt; &amp; text</root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    const char* text = xml_node_text(root);
    CHECK_NOT_NULL(text);
    CHECK_STRING(text, "<tag> & text");
    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;

    printf("Running parse tests...\n");

    /* Run all tests */
    if (!run_test_parse_basic_element()) { printf("FAILED: test_parse_basic_element\n"); failed++; }
    else { printf("PASSED: test_parse_basic_element\n"); }

    if (!run_test_parse_nested_elements()) { printf("FAILED: test_parse_nested_elements\n"); failed++; }
    else { printf("PASSED: test_parse_nested_elements\n"); }

    if (!run_test_parse_text_content()) { printf("FAILED: test_parse_text_content\n"); failed++; }
    else { printf("PASSED: test_parse_text_content\n"); }

    if (!run_test_parse_attributes()) { printf("FAILED: test_parse_attributes\n"); failed++; }
    else { printf("PASSED: test_parse_attributes\n"); }

    if (!run_test_parse_mixed_content()) { printf("FAILED: test_parse_mixed_content\n"); failed++; }
    else { printf("PASSED: test_parse_mixed_content\n"); }

    if (!run_test_parse_siblings()) { printf("FAILED: test_parse_siblings\n"); failed++; }
    else { printf("PASSED: test_parse_siblings\n"); }

    if (!run_test_parse_cdata()) { printf("FAILED: test_parse_cdata\n"); failed++; }
    else { printf("PASSED: test_parse_cdata\n"); }

    if (!run_test_parse_declaration()) { printf("FAILED: test_parse_declaration\n"); failed++; }
    else { printf("PASSED: test_parse_declaration\n"); }

    if (!run_test_parse_comment()) { printf("FAILED: test_parse_comment\n"); failed++; }
    else { printf("PASSED: test_parse_comment\n"); }

    if (!run_test_parse_empty()) { printf("FAILED: test_parse_empty\n"); failed++; }
    else { printf("PASSED: test_parse_empty\n"); }

    if (!run_test_parse_whitespace()) { printf("FAILED: test_parse_whitespace\n"); failed++; }
    else { printf("PASSED: test_parse_whitespace\n"); }

    if (!run_test_parse_multiple_attributes()) { printf("FAILED: test_parse_multiple_attributes\n"); failed++; }
    else { printf("PASSED: test_parse_multiple_attributes\n"); }

    if (!run_test_parse_attribute_special()) { printf("FAILED: test_parse_attribute_special\n"); failed++; }
    else { printf("PASSED: test_parse_attribute_special\n"); }

    if (!run_test_parse_text_entities()) { printf("FAILED: test_parse_text_entities\n"); failed++; }
    else { printf("PASSED: test_parse_text_entities\n"); }

    printf("\n=== Parse Tests Summary ===\n");
    printf("Passed: %d\n", 14 - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
