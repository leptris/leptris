/* test_dom_modify.cpp - DOM modification tests from pugixml
 * Copyright (c) 2024, Ribose Inc.
 *
 * DOM modification tests adapted from pugixml test_dom_modify.cpp
 * Tests for element tree manipulation, attribute operations, and node copying
 */

#include <gtest/gtest.h>
#include <string>
#include "../../src/include/taurus.h"

namespace taurus_test {

/**
 * Helper to check if a TaurusElement is null
 */
static inline bool element_is_null(TaurusElement elem) {
    return taurus_element_is_null(elem);
}

/**
 * Helper macros for TaurusElement assertions
 */
#define EXPECT_ELEM_NOT_NULL(elem) EXPECT_TRUE(!taurus_element_is_null((elem)))
#define ASSERT_ELEM_NOT_NULL(elem) ASSERT_TRUE(!taurus_element_is_null((elem)))
#define EXPECT_ELEM_NULL(elem) EXPECT_TRUE(taurus_element_is_null((elem)))
/* For use with function return values */
#define EXPECT_ELEM_RETURN_NULL(elem_expr) EXPECT_TRUE(element_is_null(elem_expr))

/**
 * Base class for DOM modification tests
 */
class DomOperationsTest : public ::testing::Test {
protected:
    TaurusDocument doc;
    TaurusElement root;
    std::string xml_buffer;

    void SetUp() override {
        doc = nullptr;
        root = taurus_element_handle_null();
    }

    void TearDown() override {
        if (doc) {
            taurus_document_free(doc);
            doc = nullptr;
        }
        xml_buffer.clear();
    }

    void parse_xml(const std::string& xml) {
        xml_buffer = xml;
        TaurusStatus status;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &status);
        ASSERT_EQ(status, TAURUS_OK);
        ASSERT_NE(doc, nullptr);
        root = taurus_document_root(doc);
        ASSERT_ELEM_NOT_NULL(root);
    }

    // Helper: serialize element for comparison
    std::string serialize(TaurusElement elem) {
        char* xml = taurus_element_serialize(elem, NULL);
        std::string result(xml ? xml : "");
        if (xml) taurus_free_string(xml);
        return result;
    }

    // Helper: serialize document for comparison
    std::string serialize_doc() {
        char* xml = taurus_document_serialize(doc, NULL);
        std::string result(xml ? xml : "");
        if (xml) taurus_free_string(xml);
        return result;
    }
};

/* ============================================================================
 * Node Name Modification
 * ============================================================================ */

TEST_F(DomOperationsTest, NodeSetName) {
    parse_xml("<node>text</node>");

    EXPECT_EQ(taurus_element_set_name(root, "n"), TAURUS_OK);
    EXPECT_STREQ(taurus_element_name(root), "n");

    EXPECT_EQ(serialize_doc(), "<n>text</n>");
}

TEST_F(DomOperationsTest, NodeSetNameMultiple) {
    parse_xml("<node>text</node>");

    EXPECT_EQ(taurus_element_set_name(root, "n1"), TAURUS_OK);
    EXPECT_STREQ(taurus_element_name(root), "n1");

    EXPECT_EQ(taurus_element_set_name(root, "n2"), TAURUS_OK);
    EXPECT_STREQ(taurus_element_name(root), "n2");

    EXPECT_EQ(serialize_doc(), "<n2>text</n2>");
}

/* ============================================================================
 * Node Text Content Modification
 * ============================================================================ */

TEST_F(DomOperationsTest, NodeSetText) {
    parse_xml("<node>text</node>");

    EXPECT_EQ(taurus_element_set_text(root, "no text"), TAURUS_OK);
    EXPECT_STREQ(taurus_element_text(root), "no text");

    EXPECT_EQ(serialize_doc(), "<node>no text</node>");
}

TEST_F(DomOperationsTest, NodeSetTextEmpty) {
    parse_xml("<node>text</node>");

    EXPECT_EQ(taurus_element_set_text(root, ""), TAURUS_OK);
    EXPECT_STREQ(taurus_element_text(root), "");

    EXPECT_EQ(serialize_doc(), "<node></node>");
}

TEST_F(DomOperationsTest, NodeSetTextMultiple) {
    parse_xml("<node>text</node>");

    EXPECT_EQ(taurus_element_set_text(root, "no text"), TAURUS_OK);
    EXPECT_EQ(taurus_element_set_text(root, "no text at all"), TAURUS_OK);

    EXPECT_STREQ(taurus_element_text(root), "no text at all");
    EXPECT_EQ(serialize_doc(), "<node>no text at all</node>");
}

/* ============================================================================
 * Attribute Modification
 * ============================================================================ */

TEST_F(DomOperationsTest, SetAttribute) {
    parse_xml("<node/>");

    EXPECT_EQ(taurus_element_set_attribute(root, "attr1", "v1"), TAURUS_OK);
    EXPECT_STREQ(taurus_element_attribute(root, "attr1"), "v1");

    EXPECT_EQ(taurus_element_set_attribute(root, "attr2", "v2"), TAURUS_OK);
    EXPECT_STREQ(taurus_element_attribute(root, "attr2"), "v2");

    // Attribute order is not significant in XML - check both attributes are present
    std::string result = serialize_doc();
    EXPECT_TRUE(result.find("attr1=\"v1\"") != std::string::npos);
    EXPECT_TRUE(result.find("attr2=\"v2\"") != std::string::npos);
    EXPECT_TRUE(result.find("<node ") != std::string::npos);
    EXPECT_TRUE(result.find("/>") != std::string::npos);
}

TEST_F(DomOperationsTest, SetAttributeUpdate) {
    parse_xml("<node attr='value'/>");

    EXPECT_EQ(taurus_element_set_attribute(root, "attr", "newvalue"), TAURUS_OK);
    EXPECT_STREQ(taurus_element_attribute(root, "attr"), "newvalue");

    EXPECT_EQ(serialize_doc(), "<node attr=\"newvalue\"/>");
}

TEST_F(DomOperationsTest, SetAttributeEmpty) {
    parse_xml("<node/>");

    EXPECT_EQ(taurus_element_set_attribute(root, "attr", ""), TAURUS_OK);
    EXPECT_STREQ(taurus_element_attribute(root, "attr"), "");

    EXPECT_EQ(serialize_doc(), "<node attr=\"\"/>");
}

TEST_F(DomOperationsTest, SetAttributeNumeric) {
    parse_xml("<node/>");

    EXPECT_EQ(taurus_element_set_attribute(root, "attr1", "123"), TAURUS_OK);
    EXPECT_EQ(taurus_element_set_attribute(root, "attr2", "-456"), TAURUS_OK);
    EXPECT_EQ(taurus_element_set_attribute(root, "attr3", "3.14"), TAURUS_OK);

    EXPECT_EQ(serialize_doc(), "<node attr1=\"123\" attr2=\"-456\" attr3=\"3.14\"/>");
}

TEST_F(DomOperationsTest, RemoveAttribute) {
    parse_xml("<node a1='v1' a2='v2' a3='v3'/>");

    EXPECT_EQ(taurus_element_remove_attribute(root, "a1"), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute(root, "a1"), nullptr);

    EXPECT_EQ(serialize_doc(), "<node a2=\"v2\" a3=\"v3\"/>");

    EXPECT_EQ(taurus_element_remove_attribute(root, "a3"), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute(root, "a3"), nullptr);

    EXPECT_EQ(serialize_doc(), "<node a2=\"v2\"/>");
}

TEST_F(DomOperationsTest, RemoveAttributeNotFound) {
    parse_xml("<node a1='v1'/>");

    EXPECT_EQ(taurus_element_remove_attribute(root, "nonexistent"), TAURUS_ERROR_NOT_FOUND);
    EXPECT_STREQ(taurus_element_attribute(root, "a1"), "v1");
}

TEST_F(DomOperationsTest, RemoveAllAttributes) {
    parse_xml("<node a1='v1' a2='v2' a3='v3'/>");

    EXPECT_EQ(taurus_element_remove_all_attributes(root), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute(root, "a1"), nullptr);
    EXPECT_EQ(taurus_element_attribute(root, "a2"), nullptr);
    EXPECT_EQ(taurus_element_attribute(root, "a3"), nullptr);

    EXPECT_EQ(serialize_doc(), "<node/>");
}

TEST_F(DomOperationsTest, RemoveAllAttributesEmpty) {
    parse_xml("<node/>");

    EXPECT_EQ(taurus_element_remove_all_attributes(root), TAURUS_OK);
    EXPECT_EQ(serialize_doc(), "<node/>");
}

/* ============================================================================
 * Child Element Operations - Append
 * ============================================================================ */

TEST_F(DomOperationsTest, AppendChild) {
    parse_xml("<node>foo<child/></node>");

    TaurusElement n1 = taurus_element_create(doc, "n1");
    ASSERT_ELEM_NOT_NULL(n1);
    EXPECT_EQ(taurus_element_append_child(root, n1), TAURUS_OK);

    TaurusElement n2 = taurus_element_create(doc, "n2");
    ASSERT_ELEM_NOT_NULL(n2);
    EXPECT_EQ(taurus_element_append_child(root, n2), TAURUS_OK);

    // Note: taurus_element_child_count() only counts element children, not text nodes
    EXPECT_EQ(taurus_element_child_count(root), 3); // "child", "n1", "n2"
    EXPECT_EQ(serialize_doc(), "<node>foo<child/><n1/><n2/></node>");
}

TEST_F(DomOperationsTest, AppendChildWithAttributes) {
    parse_xml("<node/>");

    TaurusElement child = taurus_element_create(doc, "child");
    ASSERT_ELEM_NOT_NULL(child);
    EXPECT_EQ(taurus_element_set_attribute(child, "attr", "value"), TAURUS_OK);
    EXPECT_EQ(taurus_element_append_child(root, child), TAURUS_OK);

    EXPECT_EQ(taurus_element_child_count(root), 1);
    EXPECT_EQ(serialize_doc(), "<node><child attr=\"value\"/></node>");
}

TEST_F(DomOperationsTest, AppendChildNested) {
    parse_xml("<node><child/></node>");

    TaurusElement grandchild = taurus_element_create(doc, "grandchild");
    ASSERT_ELEM_NOT_NULL(grandchild);

    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_ELEM_NOT_NULL(child);
    EXPECT_EQ(taurus_element_append_child(child, grandchild), TAURUS_OK);

    EXPECT_EQ(serialize_doc(), "<node><child><grandchild/></child></node>");
}

/* ============================================================================
 * Child Element Operations - Prepend
 * ============================================================================ */

TEST_F(DomOperationsTest, PrependChild) {
    parse_xml("<node>foo<child/></node>");

    TaurusElement n1 = taurus_element_create(doc, "n1");
    ASSERT_ELEM_NOT_NULL(n1);
    EXPECT_EQ(taurus_element_prepend_child(root, n1), TAURUS_OK);

    TaurusElement n2 = taurus_element_create(doc, "n2");
    ASSERT_ELEM_NOT_NULL(n2);
    EXPECT_EQ(taurus_element_prepend_child(root, n2), TAURUS_OK);

    // Note: taurus_element_child_count() only counts element children, not text nodes
    EXPECT_EQ(taurus_element_child_count(root), 3);
    EXPECT_EQ(serialize_doc(), "<node><n2/><n1/>foo<child/></node>");
}

TEST_F(DomOperationsTest, PrependChildWithAttributes) {
    parse_xml("<node/>");

    TaurusElement child = taurus_element_create(doc, "child");
    ASSERT_ELEM_NOT_NULL(child);
    EXPECT_EQ(taurus_element_set_attribute(child, "attr", "value"), TAURUS_OK);
    EXPECT_EQ(taurus_element_prepend_child(root, child), TAURUS_OK);

    EXPECT_EQ(serialize_doc(), "<node><child attr=\"value\"/></node>");
}

/* ============================================================================
 * Child Element Operations - Insert After
 * ============================================================================ */

TEST_F(DomOperationsTest, InsertAfter) {
    parse_xml("<node>foo<child/></node>");

    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_ELEM_NOT_NULL(child);

    TaurusElement n1 = taurus_element_create(doc, "n1");
    ASSERT_ELEM_NOT_NULL(n1);
    EXPECT_EQ(taurus_element_insert_after(child, n1), TAURUS_OK);

    TaurusElement n2 = taurus_element_create(doc, "n2");
    ASSERT_ELEM_NOT_NULL(n2);
    EXPECT_EQ(taurus_element_insert_after(child, n2), TAURUS_OK);

    // Taurus insert_after inserts immediately after the sibling
    // So n2 goes between child and n1
    EXPECT_EQ(serialize_doc(), "<node>foo<child/><n2/><n1/></node>");
}

TEST_F(DomOperationsTest, InsertAfterFirst) {
    parse_xml("<node><n1/><n2/></node>");

    TaurusElement n1 = taurus_element_find_child(root, "n1");
    ASSERT_ELEM_NOT_NULL(n1);

    TaurusElement n3 = taurus_element_create(doc, "n3");
    ASSERT_ELEM_NOT_NULL(n3);
    EXPECT_EQ(taurus_element_insert_after(n1, n3), TAURUS_OK);

    EXPECT_EQ(serialize_doc(), "<node><n1/><n3/><n2/></node>");
}

/* ============================================================================
 * Child Element Operations - Insert Before
 * ============================================================================ */

TEST_F(DomOperationsTest, InsertBefore) {
    parse_xml("<node>foo<child/></node>");

    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_ELEM_NOT_NULL(child);

    TaurusElement n1 = taurus_element_create(doc, "n1");
    ASSERT_ELEM_NOT_NULL(n1);
    EXPECT_EQ(taurus_element_insert_before(child, n1), TAURUS_OK);

    TaurusElement n2 = taurus_element_create(doc, "n2");
    ASSERT_ELEM_NOT_NULL(n2);
    EXPECT_EQ(taurus_element_insert_before(child, n2), TAURUS_OK);

    EXPECT_EQ(serialize_doc(), "<node>foo<n1/><n2/><child/></node>");
}

TEST_F(DomOperationsTest, InsertBeforeLast) {
    parse_xml("<node><n1/><n2/></node>");

    TaurusElement n2 = taurus_element_find_child(root, "n2");
    ASSERT_ELEM_NOT_NULL(n2);

    TaurusElement n3 = taurus_element_create(doc, "n3");
    ASSERT_ELEM_NOT_NULL(n3);
    EXPECT_EQ(taurus_element_insert_before(n2, n3), TAURUS_OK);

    EXPECT_EQ(serialize_doc(), "<node><n1/><n3/><n2/></node>");
}

/* ============================================================================
 * Child Element Operations - Remove
 * ============================================================================ */

TEST_F(DomOperationsTest, RemoveChild) {
    parse_xml("<node><n1/><n2/><n3/></node>");

    TaurusElement n1 = taurus_element_find_child(root, "n1");
    ASSERT_ELEM_NOT_NULL(n1);
    EXPECT_EQ(taurus_element_remove_child(root, n1), TAURUS_OK);

    EXPECT_ELEM_RETURN_NULL(taurus_element_find_child(root, "n1"));
    EXPECT_EQ(serialize_doc(), "<node><n2/><n3/></node>");

    TaurusElement n3 = taurus_element_find_child(root, "n3");
    ASSERT_ELEM_NOT_NULL(n3);
    EXPECT_EQ(taurus_element_remove_child(root, n3), TAURUS_OK);

    EXPECT_ELEM_RETURN_NULL(taurus_element_find_child(root, "n3"));
    EXPECT_EQ(serialize_doc(), "<node><n2/></node>");
}

TEST_F(DomOperationsTest, RemoveChildWithAttributes) {
    parse_xml("<node><child attr='value'/><n2/></node>");

    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_ELEM_NOT_NULL(child);
    EXPECT_EQ(taurus_element_remove_child(root, child), TAURUS_OK);

    EXPECT_ELEM_RETURN_NULL(taurus_element_find_child(root, "child"));
    EXPECT_EQ(serialize_doc(), "<node><n2/></node>");
}

TEST_F(DomOperationsTest, RemoveChildNested) {
    parse_xml("<node><child><n4/></child><n2/></node>");

    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_ELEM_NOT_NULL(child);

    TaurusElement n4 = taurus_element_find_child(child, "n4");
    ASSERT_ELEM_NOT_NULL(n4);
    EXPECT_EQ(taurus_element_remove_child(child, n4), TAURUS_OK);

    EXPECT_ELEM_RETURN_NULL(taurus_element_find_child(child, "n4"));
    EXPECT_EQ(serialize_doc(), "<node><child/><n2/></node>");
}

/* ============================================================================
 * Copy Operations - Append Copy
 * ============================================================================ */

TEST_F(DomOperationsTest, AppendCopyElement) {
    parse_xml("<node>foo<child/></node>");

    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_ELEM_NOT_NULL(child);

    TaurusElement copy = taurus_element_append_copy(root, child);
    ASSERT_ELEM_NOT_NULL(copy);
    EXPECT_STREQ(taurus_element_name(copy), "child");

    // Note: taurus_element_child_count() only counts element children, not text nodes
    EXPECT_EQ(taurus_element_child_count(root), 2);
    EXPECT_EQ(serialize_doc(), "<node>foo<child/><child/></node>");
}

TEST_F(DomOperationsTest, AppendCopyWithAttributes) {
    parse_xml("<node><child attr='value'/></node>");

    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_ELEM_NOT_NULL(child);

    TaurusElement copy = taurus_element_append_copy(root, child);
    ASSERT_ELEM_NOT_NULL(copy);
    EXPECT_STREQ(taurus_element_attribute(copy, "attr"), "value");

    EXPECT_EQ(serialize_doc(), "<node><child attr=\"value\"/><child attr=\"value\"/></node>");
}

TEST_F(DomOperationsTest, AppendCopyNested) {
    parse_xml("<node><child><grandchild>text</grandchild></child></node>");

    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_ELEM_NOT_NULL(child);

    TaurusElement copy = taurus_element_append_copy(root, child);
    ASSERT_ELEM_NOT_NULL(copy);

    TaurusElement grandchild = taurus_element_find_child(copy, "grandchild");
    ASSERT_ELEM_NOT_NULL(grandchild);
    EXPECT_STREQ(taurus_element_text(grandchild), "text");

    EXPECT_EQ(serialize_doc(), "<node><child><grandchild>text</grandchild></child><child><grandchild>text</grandchild></child></node>");
}

/* ============================================================================
 * Copy Operations - Prepend Copy
 * ============================================================================ */

TEST_F(DomOperationsTest, PrependCopyElement) {
    parse_xml("<node>foo<child/></node>");

    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_ELEM_NOT_NULL(child);

    TaurusElement copy = taurus_element_prepend_copy(root, child);
    ASSERT_ELEM_NOT_NULL(copy);
    EXPECT_STREQ(taurus_element_name(copy), "child");

    EXPECT_EQ(serialize_doc(), "<node><child/>foo<child/></node>");
}

TEST_F(DomOperationsTest, PrependCopyMultiple) {
    parse_xml("<node><child1/><child2/></node>");

    TaurusElement child2 = taurus_element_find_child(root, "child2");
    ASSERT_ELEM_NOT_NULL(child2);

    TaurusElement copy1 = taurus_element_prepend_copy(root, child2);
    ASSERT_ELEM_NOT_NULL(copy1);

    TaurusElement copy2 = taurus_element_prepend_copy(root, child2);
    ASSERT_ELEM_NOT_NULL(copy2);

    EXPECT_EQ(serialize_doc(), "<node><child2/><child2/><child1/><child2/></node>");
}

/* ============================================================================
 * Copy Operations - Insert Copy After
 * ============================================================================ */

TEST_F(DomOperationsTest, InsertCopyAfter) {
    parse_xml("<node>foo<child/></node>");

    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_ELEM_NOT_NULL(child);

    TaurusElement copy = taurus_element_insert_copy_after(child, child);
    ASSERT_ELEM_NOT_NULL(copy);

    EXPECT_EQ(serialize_doc(), "<node>foo<child/><child/></node>");
}

TEST_F(DomOperationsTest, InsertCopyAfterDifferent) {
    parse_xml("<node><n1/><n2/></node>");

    TaurusElement n1 = taurus_element_find_child(root, "n1");
    ASSERT_ELEM_NOT_NULL(n1);

    TaurusElement n2 = taurus_element_find_child(root, "n2");
    ASSERT_ELEM_NOT_NULL(n2);

    TaurusElement copy = taurus_element_insert_copy_after(n1, n2);
    ASSERT_ELEM_NOT_NULL(copy);

    EXPECT_EQ(serialize_doc(), "<node><n1/><n2/><n2/></node>");
}

/* ============================================================================
 * Copy Operations - Insert Copy Before
 * ============================================================================ */

TEST_F(DomOperationsTest, InsertCopyBefore) {
    parse_xml("<node>foo<child/></node>");

    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_ELEM_NOT_NULL(child);

    TaurusElement copy = taurus_element_insert_copy_before(child, child);
    ASSERT_ELEM_NOT_NULL(copy);

    EXPECT_EQ(serialize_doc(), "<node>foo<child/><child/></node>");
}

TEST_F(DomOperationsTest, InsertCopyBeforeDifferent) {
    parse_xml("<node><n1/><n2/></node>");

    TaurusElement n2 = taurus_element_find_child(root, "n2");
    ASSERT_ELEM_NOT_NULL(n2);

    TaurusElement n1 = taurus_element_find_child(root, "n1");
    ASSERT_ELEM_NOT_NULL(n1);

    TaurusElement copy = taurus_element_insert_copy_before(n2, n1);
    ASSERT_ELEM_NOT_NULL(copy);

    EXPECT_EQ(serialize_doc(), "<node><n1/><n1/><n2/></node>");
}

/* ============================================================================
 * Complex Operations
 * ============================================================================ */

TEST_F(DomOperationsTest, ComplexModification) {
    parse_xml("<node><child1/><child2/></node>");

    // Modify attributes
    EXPECT_EQ(taurus_element_set_attribute(root, "id", "1"), TAURUS_OK);

    // Add new child
    TaurusElement child3 = taurus_element_create(doc, "child3");
    ASSERT_ELEM_NOT_NULL(child3);
    EXPECT_EQ(taurus_element_append_child(root, child3), TAURUS_OK);

    // Remove child
    TaurusElement child2 = taurus_element_find_child(root, "child2");
    ASSERT_ELEM_NOT_NULL(child2);
    EXPECT_EQ(taurus_element_remove_child(root, child2), TAURUS_OK);

    // Rename remaining child1
    TaurusElement child1 = taurus_element_find_child(root, "child1");
    ASSERT_ELEM_NOT_NULL(child1);
    EXPECT_EQ(taurus_element_set_name(child1, "modified"), TAURUS_OK);

    EXPECT_EQ(serialize_doc(), "<node id=\"1\"><modified/><child3/></node>");
}

TEST_F(DomOperationsTest, DeepCopyAndModify) {
    parse_xml("<node><child><grandchild attr='value'>text</grandchild></child></node>");

    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_ELEM_NOT_NULL(child);

    // Deep copy
    TaurusElement copy = taurus_element_append_copy(root, child);
    ASSERT_ELEM_NOT_NULL(copy);

    // Modify the copy
    EXPECT_EQ(taurus_element_set_name(copy, "copy"), TAURUS_OK);

    TaurusElement grandchild = taurus_element_find_child(copy, "grandchild");
    ASSERT_ELEM_NOT_NULL(grandchild);
    EXPECT_EQ(taurus_element_set_attribute(grandchild, "attr", "modified"), TAURUS_OK);
    EXPECT_EQ(taurus_element_set_text(grandchild, "newtext"), TAURUS_OK);

    // Original should be unchanged
    EXPECT_STREQ(taurus_element_name(child), "child");
    TaurusElement orig_grandchild = taurus_element_find_child(child, "grandchild");
    EXPECT_STREQ(taurus_element_attribute(orig_grandchild, "attr"), "value");
    EXPECT_STREQ(taurus_element_text(orig_grandchild), "text");

    EXPECT_EQ(serialize_doc(), "<node><child><grandchild attr=\"value\">text</grandchild></child><copy><grandchild attr=\"modified\">newtext</grandchild></copy></node>");
}

} // namespace taurus_test
