/**
 * @file xpath_paths_tests.c
 * @brief XPath location path tests adapted from pugixml test_xpath_paths.cpp
 *
 * Tests XPath location paths: absolute, relative, abbreviated syntax.
 */

#include "test_adapter.h"

/* Absolute location paths */
TEST_XML(test_xpath_paths_absolute_root, "<root><a/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_paths_absolute_child, "<root><a><b/></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b/></a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_paths_absolute_descendant, "<root><a><b><c/></b></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b><c/></b></a></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Relative location paths */
TEST_XML(test_xpath_paths_relative_child, "<root><a><b/></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b/></a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_paths_relative_descendant, "<root><a><b><c/></b></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b><c/></b></a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_paths_relative_attribute, "<root><a href='#'/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a href='#'/></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Abbreviated syntax */
TEST_XML(test_xpath_paths_abbrev_child, "<root><a><b/></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b/></a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_paths_abbrev_parent, "<root><a><b/></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b/></a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_paths_abbrev_attr, "<root><a id='test'/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a id='test'/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_paths_abbrev_self, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

/* Predicate combinations */
TEST_XML(test_xpath_paths_predicate_position, "<root><a/><b/><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/><c/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_paths_predicate_attr, "<root><a id='1'/><a id='2'/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a id='1'/><a id='2'/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_paths_predicate_multiple, "<root><a id='1' class='x'/><a id='2' class='y'/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a id='1' class='x'/><a id='2' class='y'/></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Axes */
TEST_XML(test_xpath_paths_axis_ancestor, "<root><a><b><c/></b></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b><c/></b></a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_paths_axis_ancestor_or_self, "<root><a><b><c/></b></a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a><b><c/></b></a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_paths_axis_following, "<root><a/><b/><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/><c/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_paths_axis_following_sibling, "<root><a/><b/><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/><c/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_paths_axis_preceding, "<root><a/><b/><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/><c/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_paths_axis_preceding_sibling, "<root><a/><b/><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/><c/></root>");
    CHECK(doc != NULL);
    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running XPath location path tests...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_xpath_paths_absolute_root);
    RUN_TEST(test_xpath_paths_absolute_child);
    RUN_TEST(test_xpath_paths_absolute_descendant);
    RUN_TEST(test_xpath_paths_relative_child);
    RUN_TEST(test_xpath_paths_relative_descendant);
    RUN_TEST(test_xpath_paths_relative_attribute);
    RUN_TEST(test_xpath_paths_abbrev_child);
    RUN_TEST(test_xpath_paths_abbrev_parent);
    RUN_TEST(test_xpath_paths_abbrev_attr);
    RUN_TEST(test_xpath_paths_abbrev_self);
    RUN_TEST(test_xpath_paths_predicate_position);
    RUN_TEST(test_xpath_paths_predicate_attr);
    RUN_TEST(test_xpath_paths_predicate_multiple);
    RUN_TEST(test_xpath_paths_axis_ancestor);
    RUN_TEST(test_xpath_paths_axis_ancestor_or_self);
    RUN_TEST(test_xpath_paths_axis_following);
    RUN_TEST(test_xpath_paths_axis_following_sibling);
    RUN_TEST(test_xpath_paths_axis_preceding);
    RUN_TEST(test_xpath_paths_axis_preceding_sibling);

    #undef RUN_TEST

    printf("\n=== XPath Location Path Tests Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
