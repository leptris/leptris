/**
 * @file dom_modify_tests.c
 * @brief DOM modification tests adapted from pugixml for Taurus
 *
 * Source: pugixml/tests/test_dom_modify.cpp
 * Adapted to test Taurus DOM manipulation APIs
 */

#include "test_adapter.h"

/* Test 1: dom_node_append_child (L699-723) */
BENCHMARK_TEST(dom_node_append_child) {
    xml_document doc = xml_document_create();

    /* Create root node */
    xml_node root = xml_document_append_child(doc, "node");
    CHECK(root != NULL);

    xml_node n1 = xml_node_append_child(root, "n1");
    CHECK(n1 != NULL);

    xml_node n2 = xml_node_append_child(root, "n2");
    CHECK(n2 != NULL);
    CHECK(n2 != n1);

    xml_document_free(doc);
    return 1;
}

/* Test 2: dom_node_prepend_child (L673-697) */
BENCHMARK_TEST(dom_node_prepend_child) {
    xml_document doc = xml_document_create();

    /* Create root node */
    xml_node root = xml_document_append_child(doc, "node");
    CHECK(root != NULL);

    xml_node n1 = xml_node_prepend_child(root, "n1");
    CHECK(n1 != NULL);

    xml_node n2 = xml_node_prepend_child(root, "n2");
    CHECK(n2 != NULL);
    CHECK(n2 != n1);

    xml_document_free(doc);
    return 1;
}

/* Test 3: dom_node_insert_child_after (L725-758) */
BENCHMARK_TEST(dom_node_insert_child_after) {
    xml_document doc = xml_document_create();

    /* Create root with child */
    xml_node root = xml_document_append_child(doc, "node");
    CHECK(root != NULL);

    xml_node child = xml_node_append_child(root, "child");
    CHECK(child != NULL);

    xml_node n1 = xml_node_insert_child_after(root, "n1", child);
    CHECK(n1 != NULL);
    CHECK(n1 != child);

    xml_document_free(doc);
    return 1;
}

/* Test 4: dom_node_insert_child_before (L760-793) */
BENCHMARK_TEST(dom_node_insert_child_before) {
    xml_document doc = xml_document_create();

    /* Create root with child */
    xml_node root = xml_document_append_child(doc, "node");
    CHECK(root != NULL);

    xml_node child = xml_node_append_child(root, "child");
    CHECK(child != NULL);

    xml_node n1 = xml_node_insert_child_before(root, "n1", child);
    CHECK(n1 != NULL);
    CHECK(n1 != child);

    xml_document_free(doc);
    return 1;
}

/* Test 5: dom_node_set_name (L240-247) */
BENCHMARK_TEST(dom_node_set_name) {
    xml_document doc = xml_document_create();

    xml_node root = xml_document_append_child(doc, "node");
    CHECK(root != NULL);

    CHECK(xml_node_set_name(root, "newname"));
    CHECK_STRING(xml_node_name(root), "newname");

    xml_document_free(doc);
    return 1;
}

/* Test 6: dom_node_append_copy (L1008-1029) */
BENCHMARK_TEST(dom_node_append_copy) {
    xml_document doc = xml_document_create();

    xml_node root = xml_document_append_child(doc, "node");
    CHECK(root != NULL);

    xml_node child = xml_node_append_child(root, "child");
    CHECK(child != NULL);

    xml_node copy = xml_node_append_copy(root, child);
    CHECK(copy != NULL);
    CHECK(copy != child);
    CHECK_STRING(xml_node_name(copy), "child");

    xml_document_free(doc);
    return 1;
}

/* Test 7: dom_node_prepend_copy (L985-1006) */
BENCHMARK_TEST(dom_node_prepend_copy) {
    xml_document doc = xml_document_create();

    xml_node root = xml_document_append_child(doc, "node");
    CHECK(root != NULL);

    xml_node child = xml_node_append_child(root, "child");
    CHECK(child != NULL);

    xml_node copy = xml_node_prepend_copy(root, child);
    CHECK(copy != NULL);
    CHECK(copy != child);
    CHECK_STRING(xml_node_name(copy), "child");

    xml_document_free(doc);
    return 1;
}

/* Test 8: dom_node_insert_copy_after (L1031-1056) */
BENCHMARK_TEST(dom_node_insert_copy_after) {
    xml_document doc = xml_document_create();

    xml_node root = xml_document_append_child(doc, "node");
    CHECK(root != NULL);

    xml_node child1 = xml_node_append_child(root, "child1");
    CHECK(child1 != NULL);

    xml_node child2 = xml_node_append_child(root, "child2");
    CHECK(child2 != NULL);

    xml_node copy = xml_node_insert_copy_after(root, child1, child1);
    CHECK(copy != NULL);
    CHECK(copy != child1);
    CHECK_STRING(xml_node_name(copy), "child1");

    xml_document_free(doc);
    return 1;
}

/* Test 9: dom_node_insert_copy_before (L1058-1083) */
BENCHMARK_TEST(dom_node_insert_copy_before) {
    xml_document doc = xml_document_create();

    xml_node root = xml_document_append_child(doc, "node");
    CHECK(root != NULL);

    xml_node child1 = xml_node_append_child(root, "child1");
    CHECK(child1 != NULL);

    xml_node child2 = xml_node_append_child(root, "child2");
    CHECK(child2 != NULL);

    xml_node copy = xml_node_insert_copy_before(root, child2, child2);
    CHECK(copy != NULL);
    CHECK(copy != child2);
    CHECK_STRING(xml_node_name(copy), "child2");

    xml_document_free(doc);
    return 1;
}

/* Test 10: dom_node_remove_child (L899-932) */
BENCHMARK_TEST(dom_node_remove_child) {
    xml_document doc = xml_document_create();

    xml_node root = xml_document_append_child(doc, "node");
    CHECK(root != NULL);

    xml_node n1 = xml_node_append_child(root, "n1");
    CHECK(n1 != NULL);

    xml_node n2 = xml_node_append_child(root, "n2");
    CHECK(n2 != NULL);

    CHECK(xml_node_remove_child(root, "n1"));

    xml_document_free(doc);
    return 1;
}

/* Test array for benchmarking */
static BenchmarkTest* all_tests[] = {
    &test_dom_node_append_child,
    &test_dom_node_prepend_child,
    &test_dom_node_insert_child_after,
    &test_dom_node_insert_child_before,
    &test_dom_node_set_name,
    &test_dom_node_append_copy,
    &test_dom_node_prepend_copy,
    &test_dom_node_insert_copy_after,
    &test_dom_node_insert_copy_before,
    &test_dom_node_remove_child,
    NULL
};

int get_test_count(void) {
    int count = 0;
    while (all_tests[count] != NULL) count++;
    return count;
}

BenchmarkTest* get_test(int index) {
    return all_tests[index];
}
