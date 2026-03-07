/* test_navigation_any.cpp - Tests for "any" navigation functions (without name filtering)
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for the "any" versions of navigation functions:
 * - taurus_element_next_sibling_any()
 * - taurus_element_previous_sibling_any()
 * - taurus_element_first_child_any()
 * - taurus_element_last_child_any()
 */

#include <gtest/gtest.h>
#include <string>
#include "../../src/include/taurus.h"

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
 * Base class for navigation tests
 */
class NavigationAnyTest : public ::testing::Test {
protected:
    TaurusDocument doc;
    TaurusElement root;
    std::string xml_buffer;

    void SetUp() override {
        doc = nullptr;
        root = ELEM_NULL();
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
        ASSERT_TRUE(ELEM_NOT_NULL(root));
    }
};

/* ============================================================================
 * First Child Any Tests
 * ============================================================================ */

TEST_F(NavigationAnyTest, FirstChildAny) {
    parse_xml("<root><a/><b/><c/></root>");

    TaurusElement child = taurus_element_first_child_any(root);
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "a");
}

TEST_F(NavigationAnyTest, FirstChildAnyWithText) {
    parse_xml("<root>text1<a/>text2<b/></root>");

    TaurusElement child = taurus_element_first_child_any(root);
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "a");  // Skips text nodes
}

TEST_F(NavigationAnyTest, FirstChildAnyEmpty) {
    parse_xml("<root>only text</root>");

    TaurusElement child = taurus_element_first_child_any(root);
    EXPECT_TRUE(ELEM_IS_NULL_TMP(child));  // No element children
}

TEST_F(NavigationAnyTest, FirstChildAnyNullElement) {
    TaurusElement child = taurus_element_first_child_any(ELEM_NULL());
    EXPECT_TRUE(ELEM_IS_NULL_TMP(child));
}

/* ============================================================================
 * Last Child Any Tests
 * ============================================================================ */

TEST_F(NavigationAnyTest, LastChildAny) {
    parse_xml("<root><a/><b/><c/></root>");

    TaurusElement child = taurus_element_last_child_any(root);
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "c");
}

TEST_F(NavigationAnyTest, LastChildAnyWithText) {
    parse_xml("<root><a/>text1<b/>text2</root>");

    TaurusElement child = taurus_element_last_child_any(root);
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "b");  // Last element child
}

TEST_F(NavigationAnyTest, LastChildAnyEmpty) {
    parse_xml("<root>only text</root>");

    TaurusElement child = taurus_element_last_child_any(root);
    EXPECT_TRUE(ELEM_IS_NULL_TMP(child));  // No element children
}

TEST_F(NavigationAnyTest, LastChildAnyNullElement) {
    TaurusElement child = taurus_element_last_child_any(ELEM_NULL());
    EXPECT_TRUE(ELEM_IS_NULL_TMP(child));
}

/* ============================================================================
 * Next Sibling Any Tests
 * ============================================================================ */

TEST_F(NavigationAnyTest, NextSiblingAny) {
    parse_xml("<root><a/><b/><c/></root>");

    TaurusElement a = taurus_element_find_child(root, "a");
    ASSERT_TRUE(ELEM_NOT_NULL(a));

    TaurusElement b = taurus_element_next_sibling_any(a);
    ASSERT_TRUE(ELEM_NOT_NULL(b));
    EXPECT_STREQ(taurus_element_name(b), "b");

    TaurusElement c = taurus_element_next_sibling_any(b);
    ASSERT_TRUE(ELEM_NOT_NULL(c));
    EXPECT_STREQ(taurus_element_name(c), "c");

    TaurusElement null_elem = taurus_element_next_sibling_any(c);
    EXPECT_TRUE(ELEM_IS_NULL_TMP(null_elem));  // No more siblings
}

TEST_F(NavigationAnyTest, NextSiblingAnyWithText) {
    parse_xml("<root><a/>text1<b/>text2<c/></root>");

    TaurusElement a = taurus_element_find_child(root, "a");
    ASSERT_TRUE(ELEM_NOT_NULL(a));

    TaurusElement b = taurus_element_next_sibling_any(a);
    ASSERT_TRUE(ELEM_NOT_NULL(b));
    EXPECT_STREQ(taurus_element_name(b), "b");  // Skips text nodes
}

TEST_F(NavigationAnyTest, NextSiblingAnyNullElement) {
    TaurusElement sibling = taurus_element_next_sibling_any(ELEM_NULL());
    EXPECT_TRUE(ELEM_IS_NULL_TMP(sibling));
}

/* ============================================================================
 * Previous Sibling Any Tests
 * ============================================================================ */

TEST_F(NavigationAnyTest, PreviousSiblingAny) {
    parse_xml("<root><a/><b/><c/></root>");

    TaurusElement c = taurus_element_find_child(root, "c");
    ASSERT_TRUE(ELEM_NOT_NULL(c));

    TaurusElement b = taurus_element_previous_sibling_any(c);
    ASSERT_TRUE(ELEM_NOT_NULL(b));
    EXPECT_STREQ(taurus_element_name(b), "b");

    TaurusElement a = taurus_element_previous_sibling_any(b);
    ASSERT_TRUE(ELEM_NOT_NULL(a));
    EXPECT_STREQ(taurus_element_name(a), "a");

    TaurusElement null_elem = taurus_element_previous_sibling_any(a);
    EXPECT_TRUE(ELEM_IS_NULL_TMP(null_elem));  // No more siblings
}

TEST_F(NavigationAnyTest, PreviousSiblingAnyWithText) {
    parse_xml("<root><a/>text1<b/>text2<c/></root>");

    TaurusElement c = taurus_element_find_child(root, "c");
    ASSERT_TRUE(ELEM_NOT_NULL(c));

    TaurusElement b = taurus_element_previous_sibling_any(c);
    ASSERT_TRUE(ELEM_NOT_NULL(b));
    EXPECT_STREQ(taurus_element_name(b), "b");  // Skips text nodes
}

TEST_F(NavigationAnyTest, PreviousSiblingAnyNullElement) {
    TaurusElement sibling = taurus_element_previous_sibling_any(ELEM_NULL());
    EXPECT_TRUE(ELEM_IS_NULL_TMP(sibling));
}

/* ============================================================================
 * Combined Tests
 * ============================================================================ */

TEST_F(NavigationAnyTest, TraverseChildrenForward) {
    parse_xml("<root><a/><b/><c/><d/></root>");

    TaurusElement child = taurus_element_first_child_any(root);
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "a");

    child = taurus_element_next_sibling_any(child);
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "b");

    child = taurus_element_next_sibling_any(child);
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "c");

    child = taurus_element_next_sibling_any(child);
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "d");

    child = taurus_element_next_sibling_any(child);
    EXPECT_TRUE(ELEM_IS_NULL_TMP(child));
}

TEST_F(NavigationAnyTest, TraverseChildrenBackward) {
    parse_xml("<root><a/><b/><c/><d/></root>");

    TaurusElement child = taurus_element_last_child_any(root);
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "d");

    child = taurus_element_previous_sibling_any(child);
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "c");

    child = taurus_element_previous_sibling_any(child);
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "b");

    child = taurus_element_previous_sibling_any(child);
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "a");

    child = taurus_element_previous_sibling_any(child);
    EXPECT_TRUE(ELEM_IS_NULL_TMP(child));
}

TEST_F(NavigationAnyTest, MixedContentWithElementsAndText) {
    parse_xml("<root>text1<a/>text2<b/>text3<c/>text4</root>");

    // Should find only element children, ignoring text nodes
    int count = 0;
    for (TaurusElement child = taurus_element_first_child_any(root);
         ELEM_NOT_NULL(child);
         child = taurus_element_next_sibling_any(child)) {
        count++;
    }

    EXPECT_EQ(count, 3);  // Only a, b, c (not text nodes)
}

TEST_F(NavigationAnyTest, NestedHierarchyTraversal) {
    parse_xml("<root><a><x1/><x2/></a><b><y1/><y2/></b><c><z1/><z2/></c></root>");

    // Traverse root children
    TaurusElement a = taurus_element_first_child_any(root);
    ASSERT_TRUE(ELEM_NOT_NULL(a));
    EXPECT_STREQ(taurus_element_name(a), "a");

    TaurusElement b = taurus_element_next_sibling_any(a);
    ASSERT_TRUE(ELEM_NOT_NULL(b));
    EXPECT_STREQ(taurus_element_name(b), "b");

    TaurusElement c = taurus_element_next_sibling_any(b);
    ASSERT_TRUE(ELEM_NOT_NULL(c));
    EXPECT_STREQ(taurus_element_name(c), "c");

    // Traverse b's children
    TaurusElement y1 = taurus_element_first_child_any(b);
    ASSERT_TRUE(ELEM_NOT_NULL(y1));
    EXPECT_STREQ(taurus_element_name(y1), "y1");

    TaurusElement y2 = taurus_element_next_sibling_any(y1);
    ASSERT_TRUE(ELEM_NOT_NULL(y2));
    EXPECT_STREQ(taurus_element_name(y2), "y2");
}

} // namespace taurus_test
