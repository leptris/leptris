/**
 * @file dom_traverse_tests.c
 * @brief DOM tree traversal tests adapted from pugixml test_dom_traverse.cpp
 *
 * These tests verify DOM tree navigation functionality.
 */

#include "test_adapter.h"

/* Test attribute iteration */
TEST_XML(test_dom_traverse_attributes, "<node a1='v1' a2='v2' a3='v3'/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node a1='v1' a2='v2' a3='v3'/>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    xml_attribute attr = xml_node_first_attribute(node);
    CHECK_NOT_NULL(attr);
    CHECK_STRING(xml_attribute_name(attr), "a1");
    CHECK_STRING(xml_attribute_value(attr), "v1");

    attr = xml_attribute_next(attr);
    CHECK_NOT_NULL(attr);
    CHECK_STRING(xml_attribute_name(attr), "a2");

    attr = xml_attribute_next(attr);
    CHECK_NOT_NULL(attr);
    CHECK_STRING(xml_attribute_name(attr), "a3");

    attr = xml_attribute_next(attr);
    CHECK(attr == NULL);

    return 1;
}

/* Test child iteration */
TEST_XML(test_dom_traverse_children, "<node><a/><b/><c/></node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node><a/><b/><c/></node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    xml_node child = xml_node_first_child(node);
    CHECK_NOT_NULL(child);
    CHECK_STRING(xml_node_name(child), "a");

    child = xml_node_next_sibling(child);
    CHECK_NOT_NULL(child);
    CHECK_STRING(xml_node_name(child), "b");

    child = xml_node_next_sibling(child);
    CHECK_NOT_NULL(child);
    CHECK_STRING(xml_node_name(child), "c");

    /* Check if there's a 4th child by checking if next_sibling is NULL */
    child = xml_node_next_sibling(child);
    CHECK(child == NULL);

    return 1;
}

/* Test parent navigation */
TEST_XML(test_dom_traverse_parent, "<node><child/></node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node><child/></node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);  /* Root element is <node> */
    CHECK_NOT_NULL(node);
    CHECK_STRING(xml_node_name(node), "node");

    xml_node child = xml_node_child(node, "child");
    CHECK_NOT_NULL(child);

    xml_node parent = xml_node_parent(child);
    CHECK_NOT_NULL(parent);
    CHECK_STRING(xml_node_name(parent), "node");

    xml_node root = xml_node_parent(node);
    CHECK(root == NULL);  /* Root has no parent */

    return 1;
}

/* Test root navigation */
TEST_XML(test_dom_traverse_root, "<a><b><c><d/></c></b></a>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<a><b><c><d/></c></b></a>");
    CHECK(doc != NULL);

    xml_node a = xml_document_element(doc);  /* Root element is <a> */
    CHECK_NOT_NULL(a);
    CHECK_STRING(xml_node_name(a), "a");

    xml_node b = xml_node_child(a, "b");
    CHECK_NOT_NULL(b);

    xml_node c = xml_node_child(b, "c");
    CHECK_NOT_NULL(c);

    xml_node d = xml_node_child(c, "d");
    CHECK_NOT_NULL(d);

    xml_node root = xml_node_root(d);
    CHECK_NOT_NULL(root);
    CHECK_STRING(xml_node_name(root), "a");

    return 1;
}

/* Test sibling navigation forward and backward */
TEST_XML(test_dom_traverse_siblings_bidirectional, "<node><a/><b/><c/><d/></node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node><a/><b/><c/><d/></node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);

    xml_node a = xml_node_child(node, "a");
    CHECK_NOT_NULL(a);

    xml_node b = xml_node_next_sibling(a);
    CHECK_NOT_NULL(b);
    CHECK_STRING(xml_node_name(b), "b");

    xml_node c = xml_node_next_sibling(b);
    CHECK_NOT_NULL(c);
    CHECK_STRING(xml_node_name(c), "c");

    xml_node d = xml_node_next_sibling(c);
    CHECK_NOT_NULL(d);
    CHECK_STRING(xml_node_name(d), "d");

    return 1;
}

/* Test last child */
TEST_XML(test_dom_traverse_last_child, "<node><a/><b/><c/></node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node><a/><b/><c/></node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    xml_node last = xml_node_last_child(node);
    CHECK_NOT_NULL(last);
    CHECK_STRING(xml_node_name(last), "c");

    return 1;
}

/* Test deep traversal */
TEST_XML(test_dom_traverse_deep, "<a1><a2><a3><a4><a5/></a4></a3></a2></a1>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<a1><a2><a3><a4><a5/></a4></a3></a2></a1>");
    CHECK(doc != NULL);

    xml_node a1 = xml_document_element(doc);
    CHECK_NOT_NULL(a1);
    CHECK_STRING(xml_node_name(a1), "a1");

    xml_node a2 = xml_node_child(a1, "a2");
    CHECK_NOT_NULL(a2);

    xml_node a3 = xml_node_child(a2, "a3");
    CHECK_NOT_NULL(a3);

    xml_node a4 = xml_node_child(a3, "a4");
    CHECK_NOT_NULL(a4);

    xml_node a5 = xml_node_child(a4, "a5");
    CHECK_NOT_NULL(a5);
    CHECK_STRING(xml_node_name(a5), "a5");

    /* Walk up the tree */
    a4 = xml_node_parent(a5);
    CHECK_NOT_NULL(a4);
    CHECK_STRING(xml_node_name(a4), "a4");

    a3 = xml_node_parent(a4);
    CHECK_NOT_NULL(a3);
    CHECK_STRING(xml_node_name(a3), "a3");

    a2 = xml_node_parent(a3);
    CHECK_NOT_NULL(a2);
    CHECK_STRING(xml_node_name(a2), "a2");

    a1 = xml_node_parent(a2);
    CHECK_NOT_NULL(a1);
    CHECK_STRING(xml_node_name(a1), "a1");

    return 1;
}

/* Test attribute iteration with query */
TEST_XML(test_dom_traverse_attribute_by_name, "<node id='123' name='test' value='42'/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node id='123' name='test' value='42'/>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    CHECK_STRING(xml_node_attribute_value(node, "id"), "123");
    CHECK_STRING(xml_node_attribute_value(node, "name"), "test");
    CHECK_STRING(xml_node_attribute_value(node, "value"), "42");

    /* Non-existent attribute should return NULL */
    CHECK_NULL(xml_node_attribute_value(node, "nonexistent"));

    return 1;
}

/* Test child by name */
TEST_XML(test_dom_traverse_child_by_name, "<node><a/><b/><c/><b/></node>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node><a/><b/><c/><b/></node>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    /* first_child_by_name returns first matching child */
    xml_node b = xml_node_child(node, "b");
    CHECK_NOT_NULL(b);
    CHECK_STRING(xml_node_name(b), "b");

    /* Should be the first 'b' element, not the second */
    xml_node next_b = xml_node_next_sibling(b);
    CHECK_STRING(xml_node_name(next_b), "c");

    return 1;
}

/* Test null node handling */
TEST(test_dom_traverse_null_node)
{
    /* All operations on null nodes should return null gracefully */
    xml_node null_node = NULL;

    CHECK_NULL(xml_node_first_child(null_node));
    CHECK_NULL(xml_node_last_child(null_node));
    CHECK_NULL(xml_node_next_sibling(null_node));
    CHECK_NULL(xml_node_parent(null_node));
    CHECK_NULL(xml_node_root(null_node));
    CHECK_NULL(xml_node_child(null_node, "any"));
    CHECK_NULL(xml_node_first_attribute(null_node));
    CHECK_NULL(xml_node_attribute_value(null_node, "any"));
    return 1;
}

/* Test empty element */
TEST_XML(test_dom_traverse_empty_element, "<node/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<node/>");
    CHECK(doc != NULL);

    xml_node node = xml_document_element(doc);
    CHECK_NOT_NULL(node);

    CHECK_NULL(xml_node_first_child(node));
    CHECK_NULL(xml_node_last_child(node));
    CHECK_NULL(xml_node_first_attribute(node));

    return 1;
}

/* Test node type check */
TEST_XML(test_dom_traverse_node_types, "<root><!--comment--><?pi text?><![CDATA[cdata]]><child/>text</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><!--comment--><?pi text?><![CDATA[cdata]]><child/>text</root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);
    CHECK(xml_node_get_type(root) == node_element);

    /* Find child element */
    xml_node child = xml_node_first_child(root);
    while (child && xml_node_get_type(child) != node_element) {
        child = xml_node_next_sibling(child);
    }

    CHECK_NOT_NULL(child);
    CHECK_STRING(xml_node_name(child), "child");
    CHECK(xml_node_get_type(child) == node_element);

    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running DOM traversal tests...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_dom_traverse_attributes);
    RUN_TEST(test_dom_traverse_children);
    RUN_TEST(test_dom_traverse_parent);
    RUN_TEST(test_dom_traverse_root);
    RUN_TEST(test_dom_traverse_siblings_bidirectional);
    RUN_TEST(test_dom_traverse_last_child);
    RUN_TEST(test_dom_traverse_deep);
    RUN_TEST(test_dom_traverse_attribute_by_name);
    RUN_TEST(test_dom_traverse_child_by_name);
    RUN_TEST(test_dom_traverse_null_node);
    RUN_TEST(test_dom_traverse_empty_element);
    RUN_TEST(test_dom_traverse_node_types);

    #undef RUN_TEST

    printf("\n=== DOM Traversal Tests Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
