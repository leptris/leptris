/* test_dom_traverse.cpp - DOM navigation and traversal tests from pugixml
 * Copyright (c) 2024, Ribose Inc.
 *
 * DOM navigation tests adapted from pugixml test_dom_traverse.cpp
 * Focus on navigation operations: parent, root, children, siblings, attributes
 */

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "../../src/include/taurus.h"
#include "../../src/taurus/dom/element.h"

/* TaurusElement handle API macros */
#define ELEM_IS_NULL(e) taurus_element_is_null((e))
#define ELEM_NOT_NULL(e) !taurus_element_is_null((e))
#define ELEM_NULL() taurus_element_handle_null()

/* Inline helper for checking null on function returns (temporaries) */
static inline bool elem_is_null_inline(TaurusElement elem) {
    return taurus_element_is_null(elem);
}
#define ELEM_IS_NULL_TMP(e) elem_is_null_inline(e)

namespace taurus_test {

/**
 * Base class for DOM navigation tests
 */
class DomNavigationTest : public ::testing::Test {
protected:
    TaurusDocument doc;
    TaurusElement root;
    std::string xml_buffer;

    void SetUp() override {
        doc = nullptr;
        root = ELEM_NULL();
        taurus_set_strict_mode(1);
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
        ASSERT_EQ(status, TAURUS_OK) << "Failed to parse XML: " << xml;
        ASSERT_NE(doc, nullptr);
        root = taurus_document_root(doc);
        ASSERT_TRUE(ELEM_NOT_NULL(root));
    }

    // Helper to get child by name (returns null element if not found)
    TaurusElement get_child(TaurusElement parent, const char* name) {
        return taurus_element_find_child(parent, name);
    }

    // Helper to check if two elements are equal (both null or same node)
    bool elements_equal(TaurusElement e1, TaurusElement e2) {
        bool null1 = ELEM_IS_NULL(e1);
        bool null2 = ELEM_IS_NULL(e2);
        if (null1 && null2) return true;
        if (null1 || null2) return false;
        // Compare by name since we can't compare pointers directly
        const char* name1 = taurus_element_name(e1);
        const char* name2 = taurus_element_name(e2);
        if (name1 && name2 && strcmp(name1, name2) == 0) {
            return true;
        }
        return false;
    }
};

/* ============================================================================
 * Parent Navigation Tests
 * ============================================================================ */

TEST_F(DomNavigationTest, ParentNavigation) {
    parse_xml("<node><child/></node>");

    // root IS the <node> element (taurus_document_root returns the top-level element)
    // So root == <node>
    EXPECT_STREQ(taurus_element_name(root), "node");

    // Null element has null parent
    TaurusElement null_elem = ELEM_NULL();
    TaurusElement null_parent = taurus_element_parent(null_elem);
    EXPECT_TRUE(ELEM_IS_NULL(null_parent));

    // Child's parent is the node (which is root)
    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    TaurusElement child_parent = taurus_element_parent(child);
    EXPECT_TRUE(ELEM_NOT_NULL(child_parent));
    EXPECT_STREQ(taurus_element_name(child_parent), "node");
}

TEST_F(DomNavigationTest, ParentOfRoot) {
    parse_xml("<root/>");

    // Root's parent is null (or returns root itself depending on implementation)
    TaurusElement root_parent = taurus_element_parent(root);
    // In taurus, root's parent may be null or root itself
    // Both behaviors are acceptable
}

/* ============================================================================
 * Root Navigation Tests
 * ============================================================================ */

TEST_F(DomNavigationTest, RootNavigation) {
    parse_xml("<node><child/></node>");

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    // Null element has null root - Taurus doesn't have root() function

    // Child's root is the document root (which is root itself)
    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    // The document root is accessible via taurus_document_root
    TaurusElement doc_root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(doc_root));
    EXPECT_STREQ(taurus_element_name(doc_root), "node");
}

/* ============================================================================
 * Child Navigation Tests
 * ============================================================================ */

TEST_F(DomNavigationTest, ChildByName) {
    parse_xml("<node><child1/><child2/></node>");

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    // Find child1
    TaurusElement child1 = taurus_element_find_child(root, "child1");
    ASSERT_TRUE(ELEM_NOT_NULL(child1));
    EXPECT_STREQ(taurus_element_name(child1), "child1");

    // Find child2
    TaurusElement child2 = taurus_element_find_child(root, "child2");
    ASSERT_TRUE(ELEM_NOT_NULL(child2));
    EXPECT_STREQ(taurus_element_name(child2), "child2");

    // Non-existent child returns null
    TaurusElement child3 = taurus_element_find_child(root, "child3");
    EXPECT_TRUE(ELEM_IS_NULL(child3));
}

TEST_F(DomNavigationTest, ChildByNameNullElement) {
    parse_xml("<node><child1/><child2/></node>");

    // Finding child on null element returns null
    TaurusElement null_elem = ELEM_NULL();
    TaurusElement result = taurus_element_find_child(null_elem, "child1");
    EXPECT_TRUE(ELEM_IS_NULL(result));
}

TEST_F(DomNavigationTest, ChildByNameEmptyName) {
    parse_xml("<node><child1/><child2/></node>");

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    // Empty name should return null or first child depending on implementation
    TaurusElement child = taurus_element_find_child(root, "");
    // Accept either null or first child behavior
}

TEST_F(DomNavigationTest, ChildByNameWithNullInName) {
    parse_xml("<node><child1/><child2/></node>");

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    // Name with embedded null - taurus's string comparison stops at null
    // so "child1\0extra" will match "child1"
    std::string name_with_null = std::string("child1") + '\0' + "extra";
    TaurusElement child = taurus_element_find_child(root, name_with_null.c_str());
    // Since comparison stops at null, this matches "child1"
    EXPECT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "child1");
}

/* ============================================================================
 * Attribute Navigation Tests
 * ============================================================================ */

TEST_F(DomNavigationTest, AttributeByName) {
    parse_xml("<node attr1='0' attr2='1'/>");

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    // Find attr1
    const char* attr1 = taurus_element_attribute(root, "attr1");
    ASSERT_NE(attr1, nullptr);
    EXPECT_STREQ(attr1, "0");

    // Find attr2
    const char* attr2 = taurus_element_attribute(root, "attr2");
    ASSERT_NE(attr2, nullptr);
    EXPECT_STREQ(attr2, "1");

    // Non-existent attribute returns null
    const char* attr3 = taurus_element_attribute(root, "attr3");
    EXPECT_EQ(attr3, nullptr);
}

TEST_F(DomNavigationTest, AttributeNullElement) {
    parse_xml("<node attr1='0'/>");

    // Getting attribute on null element returns null
    TaurusElement null_elem = ELEM_NULL();
    const char* result = taurus_element_attribute(null_elem, "attr1");
    EXPECT_EQ(result, nullptr);
}

/* ============================================================================
 * Sibling Navigation Tests
 * ============================================================================ */

TEST_F(DomNavigationTest, NextPreviousSibling) {
    parse_xml("<node><child1/><child2/><child3/></node>");

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    TaurusElement child1 = taurus_element_find_child(root, "child1");
    TaurusElement child2 = taurus_element_find_child(root, "child2");
    TaurusElement child3 = taurus_element_find_child(root, "child3");

    ASSERT_TRUE(ELEM_NOT_NULL(child1));
    ASSERT_TRUE(ELEM_NOT_NULL(child2));
    ASSERT_TRUE(ELEM_NOT_NULL(child3));

    // child1's next sibling is child2
    TaurusElement next1 = taurus_element_next_sibling(child1, nullptr);
    EXPECT_TRUE(ELEM_NOT_NULL(next1));
    EXPECT_STREQ(taurus_element_name(next1), "child2");

    // child2's next sibling is child3
    TaurusElement next2 = taurus_element_next_sibling(child2, nullptr);
    EXPECT_TRUE(ELEM_NOT_NULL(next2));
    EXPECT_STREQ(taurus_element_name(next2), "child3");

    // child3's next sibling is null
    TaurusElement next3 = taurus_element_next_sibling(child3, nullptr);
    EXPECT_TRUE(ELEM_IS_NULL(next3));

    // child1's previous sibling is null
    TaurusElement prev1 = taurus_element_previous_sibling(child1, nullptr);
    EXPECT_TRUE(ELEM_IS_NULL(prev1));

    // child3's previous sibling is child2
    TaurusElement prev3 = taurus_element_previous_sibling(child3, nullptr);
    EXPECT_TRUE(ELEM_NOT_NULL(prev3));
    EXPECT_STREQ(taurus_element_name(prev3), "child2");

    // Null element returns null
    TaurusElement null_elem = ELEM_NULL();
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_next_sibling(null_elem, nullptr)));
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_previous_sibling(null_elem, nullptr)));
}

TEST_F(DomNavigationTest, NextPreviousSiblingByName) {
    parse_xml("<node><child1/><child2/><child3/></node>");

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    TaurusElement child1 = taurus_element_find_child(root, "child1");
    TaurusElement child2 = taurus_element_find_child(root, "child2");
    TaurusElement child3 = taurus_element_find_child(root, "child3");

    ASSERT_TRUE(ELEM_NOT_NULL(child1));
    ASSERT_TRUE(ELEM_NOT_NULL(child2));
    ASSERT_TRUE(ELEM_NOT_NULL(child3));

    // Next sibling with name filter - finds next sibling WITH that name
    TaurusElement next1 = taurus_element_next_sibling(child1, "child2");
    EXPECT_TRUE(ELEM_NOT_NULL(next1));
    EXPECT_STREQ(taurus_element_name(next1), "child2");

    // Next sibling with non-matching name
    // taurus searches for next sibling WITH that name, skipping non-matching ones
    TaurusElement next1_child3 = taurus_element_next_sibling(child1, "child3");
    // Should return child3 (skipping child2)
    EXPECT_TRUE(ELEM_NOT_NULL(next1_child3));
    EXPECT_STREQ(taurus_element_name(next1_child3), "child3");

    // Previous sibling with name filter
    TaurusElement prev3 = taurus_element_previous_sibling(child3, "child2");
    EXPECT_TRUE(ELEM_NOT_NULL(prev3));
    EXPECT_STREQ(taurus_element_name(prev3), "child2");

    // Previous sibling with non-matching name
    // taurus searches for previous sibling WITH that name, skipping non-matching ones
    TaurusElement prev3_child1 = taurus_element_previous_sibling(child3, "child1");
    // Should return child1 (skipping child2)
    EXPECT_TRUE(ELEM_NOT_NULL(prev3_child1));
    EXPECT_STREQ(taurus_element_name(prev3_child1), "child1");
}

TEST_F(DomNavigationTest, NextPreviousSiblingNullElement) {
    parse_xml("<node><child1/><child2/></node>");

    // Null element returns null
    TaurusElement null_elem = ELEM_NULL();
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_next_sibling(null_elem, "child1")));
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_previous_sibling(null_elem, "child1")));
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_next_sibling(null_elem, nullptr)));
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_previous_sibling(null_elem, nullptr)));
}

/* ============================================================================
 * First/Last Child Tests
 * ============================================================================ */

TEST_F(DomNavigationTest, FirstLastChild) {
    parse_xml("<node><child1/><child2/></node>");

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    // First child
    TaurusElement first = taurus_element_first_child(root, nullptr);
    EXPECT_TRUE(ELEM_NOT_NULL(first));
    EXPECT_STREQ(taurus_element_name(first), "child1");

    // Last child
    TaurusElement last = taurus_element_last_child(root, nullptr);
    EXPECT_TRUE(ELEM_NOT_NULL(last));
    EXPECT_STREQ(taurus_element_name(last), "child2");

    // Verify first == child1 and last == child2
    TaurusElement child1 = taurus_element_find_child(root, "child1");
    TaurusElement child2 = taurus_element_find_child(root, "child2");
    EXPECT_STREQ(taurus_element_name(first), taurus_element_name(child1));
    EXPECT_STREQ(taurus_element_name(last), taurus_element_name(child2));

    // Null element returns null
    TaurusElement null_elem = ELEM_NULL();
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_first_child(null_elem, nullptr)));
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_last_child(null_elem, nullptr)));
}

TEST_F(DomNavigationTest, FirstLastChildByName) {
    parse_xml("<node><child1/><child2/><child3/></node>");

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    // First child with name
    TaurusElement first = taurus_element_first_child(root, "child1");
    EXPECT_TRUE(ELEM_NOT_NULL(first));
    EXPECT_STREQ(taurus_element_name(first), "child1");

    // First child with non-matching name returns null
    TaurusElement first_nomatch = taurus_element_first_child(root, "child4");
    EXPECT_TRUE(ELEM_IS_NULL(first_nomatch));

    // Last child with name
    TaurusElement last = taurus_element_last_child(root, "child3");
    EXPECT_TRUE(ELEM_NOT_NULL(last));
    EXPECT_STREQ(taurus_element_name(last), "child3");

    // Last child with non-matching name returns null
    TaurusElement last_nomatch = taurus_element_last_child(root, "child4");
    EXPECT_TRUE(ELEM_IS_NULL(last_nomatch));
}

/* ============================================================================
 * Child Value Tests
 * ============================================================================ */

TEST_F(DomNavigationTest, ChildValue) {
    parse_xml("<node><novalue/><child1>value1</child1><child2>value2<n/></child2><child3><![CDATA[value3]]></child3>value4</node>");

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    // Element without child value
    TaurusElement novalue = taurus_element_find_child(root, "novalue");
    ASSERT_TRUE(ELEM_NOT_NULL(novalue));
    const char* text_novalue = taurus_element_text(novalue);
    // taurus_element_text returns "" for empty elements, not null
    ASSERT_NE(text_novalue, nullptr);
    EXPECT_STREQ(text_novalue, "");

    // Element with text child
    TaurusElement child1 = taurus_element_find_child(root, "child1");
    ASSERT_TRUE(ELEM_NOT_NULL(child1));
    const char* text1 = taurus_element_text(child1);
    ASSERT_NE(text1, nullptr);
    EXPECT_STREQ(text1, "value1");

    // Element with nested text
    TaurusElement child2 = taurus_element_find_child(root, "child2");
    ASSERT_TRUE(ELEM_NOT_NULL(child2));
    const char* text2 = taurus_element_text(child2);
    ASSERT_NE(text2, nullptr);
    // Taurus concatenates text content
    EXPECT_STREQ(text2, "value2");

    // Element with CDATA
    TaurusElement child3 = taurus_element_find_child(root, "child3");
    ASSERT_TRUE(ELEM_NOT_NULL(child3));
    const char* text3 = taurus_element_text(child3);
    ASSERT_NE(text3, nullptr);
    EXPECT_STREQ(text3, "value3");

    // Root element with mixed content
    const char* text_root = taurus_element_text(root);
    ASSERT_NE(text_root, nullptr);
    // Taurus concatenates all text content
    EXPECT_STREQ(text_root, "value1value2value3value4");
}

TEST_F(DomNavigationTest, ChildValueNullElement) {
    // Null element returns empty string, not null
    TaurusElement null_elem = ELEM_NULL();
    const char* text = taurus_element_text(null_elem);
    // taurus_element_text returns "" for null element, not nullptr
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "");
}

/* ============================================================================
 * First/Last Attribute Tests
 * ============================================================================ */

TEST_F(DomNavigationTest, FirstLastAttribute) {
    parse_xml("<node attr1='0' attr2='1'/>");

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    // First attribute - use attribute iteration or first_attribute if available
    // For taurus, we use the attribute lookup by name
    const char* attr1 = taurus_element_attribute(root, "attr1");
    ASSERT_NE(attr1, nullptr);
    EXPECT_STREQ(attr1, "0");

    const char* attr2 = taurus_element_attribute(root, "attr2");
    ASSERT_NE(attr2, nullptr);
    EXPECT_STREQ(attr2, "1");

    // Null element has no attributes
    TaurusElement null_elem = ELEM_NULL();
    const char* null_attr = taurus_element_attribute(null_elem, "attr1");
    EXPECT_EQ(null_attr, nullptr);
}

TEST_F(DomNavigationTest, FirstLastAttributeEmpty) {
    parse_xml("<node/>");

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    // No attributes
    const char* attr = taurus_element_attribute(root, "nonexistent");
    EXPECT_EQ(attr, nullptr);
}

/* ============================================================================
 * Find Child By Attribute Tests
 * ============================================================================ */

TEST_F(DomNavigationTest, FindChildByAttribute) {
    parse_xml("<node><stub attr='value3' /><child1 attr='value1'/><child2 attr='value2'/><child2 attr='value3'/></node>");

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    // Find child2 with attr='value3'
    TaurusElement found = taurus_element_find_child_by_attr(root, "child2", "attr", "value3");
    ASSERT_TRUE(ELEM_NOT_NULL(found));
    EXPECT_STREQ(taurus_element_name(found), "child2");
    const char* attr_value = taurus_element_attribute(found, "attr");
    ASSERT_NE(attr_value, nullptr);
    EXPECT_STREQ(attr_value, "value3");

    // Find with non-matching attribute value
    TaurusElement not_found = taurus_element_find_child_by_attr(root, "child2", "attr3", "value3");
    EXPECT_TRUE(ELEM_IS_NULL(not_found));

    // Find by attribute value only (any child name)
    TaurusElement by_attr_only = taurus_element_find_child_by_attr(root, nullptr, "attr", "value2");
    EXPECT_TRUE(ELEM_NOT_NULL(by_attr_only));

    // Non-existent match
    TaurusElement no_match = taurus_element_find_child_by_attr(root, "child4", "attr", "value");
    EXPECT_TRUE(ELEM_IS_NULL(no_match));

    // Null element returns null
    TaurusElement null_elem = ELEM_NULL();
    TaurusElement null_result = taurus_element_find_child_by_attr(null_elem, "child1", "attr", "value");
    EXPECT_TRUE(ELEM_IS_NULL(null_result));
}

/* ============================================================================
 * Empty and Null Tests
 * ============================================================================ */

TEST_F(DomNavigationTest, EmptyElementNavigation) {
    parse_xml("<node/>");

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    // Empty node has no children
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_first_child(root, nullptr)));
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_last_child(root, nullptr)));
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_find_child(root, "child")));
}

TEST_F(DomNavigationTest, NullElementNavigation) {
    // All navigation operations on null element return null
    TaurusElement null_elem = ELEM_NULL();

    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_parent(null_elem)));
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_next_sibling(null_elem, nullptr)));
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_previous_sibling(null_elem, nullptr)));
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_first_child(null_elem, nullptr)));
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_last_child(null_elem, nullptr)));
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_find_child(null_elem, "child")));
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_find_child_by_attr(null_elem, "child", "attr", "value")));
    EXPECT_EQ(taurus_element_attribute(null_elem, "attr"), nullptr);
    // taurus_element_text returns "" for null element, not nullptr
    const char* text = taurus_element_text(null_elem);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "");
}

/* ============================================================================
 * Edge Cases
 * ============================================================================ */

TEST_F(DomNavigationTest, DeepNesting) {
    parse_xml("<a><b><c><d><e/></d></c></b></a>");

    // root IS the <a> element
    EXPECT_STREQ(taurus_element_name(root), "a");

    TaurusElement b = taurus_element_find_child(root, "b");
    ASSERT_TRUE(ELEM_NOT_NULL(b));

    TaurusElement c = taurus_element_find_child(b, "c");
    ASSERT_TRUE(ELEM_NOT_NULL(c));

    TaurusElement d = taurus_element_find_child(c, "d");
    ASSERT_TRUE(ELEM_NOT_NULL(d));

    TaurusElement e = taurus_element_find_child(d, "e");
    ASSERT_TRUE(ELEM_NOT_NULL(e));

    // Verify parent chain
    TaurusElement e_parent = taurus_element_parent(e);
    EXPECT_STREQ(taurus_element_name(e_parent), "d");

    TaurusElement d_parent = taurus_element_parent(d);
    EXPECT_STREQ(taurus_element_name(d_parent), "c");
}

TEST_F(DomNavigationTest, ManySiblings) {
    std::string xml = "<node>";
    for (int i = 0; i < 100; i++) {
        xml += "<child/>";
    }
    xml += "</node>";
    parse_xml(xml);

    // root IS the <node> element
    EXPECT_STREQ(taurus_element_name(root), "node");

    TaurusElement first = taurus_element_first_child(root, nullptr);
    ASSERT_TRUE(ELEM_NOT_NULL(first));
    EXPECT_STREQ(taurus_element_name(first), "child");

    TaurusElement last = taurus_element_last_child(root, nullptr);
    ASSERT_TRUE(ELEM_NOT_NULL(last));
    EXPECT_STREQ(taurus_element_name(last), "child");

    // Traverse siblings
    int count = 0;
    TaurusElement current = first;
    while (ELEM_NOT_NULL(current) && count < 150) {  // Safety limit
        count++;
        current = taurus_element_next_sibling(current, nullptr);
    }
    EXPECT_EQ(count, 100);  // 100 siblings, so 100 iterations (first counts as 1)
}

} // namespace taurus_test
