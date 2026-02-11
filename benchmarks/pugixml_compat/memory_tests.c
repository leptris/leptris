/**
 * @file memory_tests.c
 * @brief Memory management tests adapted from pugixml test_memory.cpp
 *
 * These tests verify memory management functionality.
 */

#include "test_adapter.h"

/* Test document creation and destruction */
TEST(test_memory_create_destroy)
{
    xml_document doc = xml_document_create();
    CHECK_NOT_NULL(doc);
    xml_document_free(doc);
    return 1;
}

/* Test multiple document lifecycle */
TEST(test_memory_multiple_documents)
{
    xml_document doc1 = xml_document_create();
    CHECK_NOT_NULL(doc1);

    xml_document doc2 = xml_document_create();
    CHECK_NOT_NULL(doc2);

    xml_document_free(doc1);
    xml_document_free(doc2);
    return 1;
}

/* Test large document handling */
TEST(test_memory_large_document)
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>" "<a>" "<b>" "<c>" "<d>" "<e>" "<f>" "<g>" "<h>" "<i>" "<j>" "</j>" "</i>" "</h>" "</g>" "</f>" "</e>" "</d>" "</c>" "</b>" "</a>" "</root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    xml_node a = xml_node_child(root, "a");
    CHECK_NOT_NULL(a);

    xml_document_free(doc);
    return 1;
}

/* Test many attributes */
TEST(test_memory_many_attributes)
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root a1='v1' a2='v2' a3='v3' a4='v4' a5='v5' a6='v6' a7='v7' a8='v8' a9='v9' a10='v10'/>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    CHECK_NOT_NULL(root);

    CHECK_STRING(xml_node_attribute_value(root, "a1"), "v1");
    CHECK_STRING(xml_node_attribute_value(root, "a5"), "v5");
    CHECK_STRING(xml_node_attribute_value(root, "a10"), "v10");

    xml_document_free(doc);
    return 1;
}

/* Test document reuse */
TEST(test_memory_document_reuse)
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/></root>");
    CHECK(doc != NULL);

    xml_document_load_string(doc, "<root><b/></root>");
    CHECK(doc != NULL);

    xml_node root = xml_document_element(doc);
    xml_node b = xml_node_child(root, "b");
    CHECK_NOT_NULL(b);

    xml_document_free(doc);
    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running memory tests...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_memory_create_destroy);
    RUN_TEST(test_memory_multiple_documents);
    RUN_TEST(test_memory_large_document);
    RUN_TEST(test_memory_many_attributes);
    RUN_TEST(test_memory_document_reuse);

    #undef RUN_TEST

    printf("\n=== Memory Tests Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
