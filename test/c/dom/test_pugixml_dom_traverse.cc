/**
 * Taurus DOM Traversal Tests
 * Adapted from pugixml test_dom_traverse.cpp
 *
 * Tests:
 * - Tree navigation (parent, child, sibling)
 * - Attribute iteration
 * - Child iteration
 * - Iterator patterns
 */

#include "gtest/gtest.h"
#include <taurus.h>
#include <string>
#include <vector>

/**
 * Helper to check if two TaurusElement handles are equal
 * Compares the underlying data (legacy pointer or compact offset + doc)
 */
static inline bool elements_equal(TaurusElement a, TaurusElement b) {
    return memcmp(&a, &b, sizeof(TaurusElement)) == 0;
}

/**
 * Helper to check if a TaurusElement is null (for use with function return values)
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
#define ASSERT_ELEM_NULL(elem) ASSERT_TRUE(taurus_element_is_null((elem)))
#define EXPECT_ELEM_EQ(a, b) EXPECT_TRUE(elements_equal((a), (b)))
#define ASSERT_ELEM_EQ(a, b) ASSERT_TRUE(elements_equal((a), (b)))
/* For use with function return values like taurus_element_parent() */
#define EXPECT_ELEM_RETURN_NULL(elem_expr) EXPECT_TRUE(element_is_null(elem_expr))

// Helper function to create a document from XML string
static TaurusDocument create_doc(const char* xml) {
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    EXPECT_NE(doc, nullptr);
    return doc;
}

// Helper function to serialize element to string
static std::string serialize_elem(TaurusElement elem) {
    char* xml = taurus_element_serialize(elem, NULL);
    if (!xml) return "";
    std::string result(xml);
    free(xml);
    return result;
}

// Test fixture class
class TaurusDomTraverse : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// ============================================================================
// PARENT & ROOT NAVIGATION
// ============================================================================

TEST_F(TaurusDomTraverse, ParentNavigation) {
    TaurusDocument doc = create_doc("<node><child/></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_first_child(root, NULL);

    // Child's parent should be root
    TaurusElement parent = taurus_element_parent(child);
    EXPECT_ELEM_NOT_NULL(parent);
    EXPECT_STREQ(taurus_element_name(parent), "node");

    // Root's parent should be document (null element)
    TaurusElement root_parent = taurus_element_parent(root);
    EXPECT_ELEM_RETURN_NULL(root_parent);

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, RootNavigation) {
    TaurusDocument doc = create_doc("<node><child><grandchild/></child></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_first_child(root, NULL);
    TaurusElement grandchild = taurus_element_first_child(child, NULL);

    // All nodes should have same root
    EXPECT_STREQ(taurus_element_name(root), "node");
    EXPECT_STREQ(taurus_element_name(child), "child");
    EXPECT_STREQ(taurus_element_name(grandchild), "grandchild");

    // Parent chain should work
    TaurusElement parent = taurus_element_parent(grandchild);
    EXPECT_STREQ(taurus_element_name(parent), "child");

    parent = taurus_element_parent(parent);
    EXPECT_STREQ(taurus_element_name(parent), "node");

    taurus_document_free(doc);
}

// ============================================================================
// CHILD NAVIGATION
// ============================================================================

TEST_F(TaurusDomTraverse, FirstChild) {
    TaurusDocument doc = create_doc("<node><child1/><child2/></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement first = taurus_element_first_child(root, NULL);

    EXPECT_ELEM_NOT_NULL(first);
    EXPECT_STREQ(taurus_element_name(first), "child1");

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, LastChild) {
    TaurusDocument doc = create_doc("<node><child1/><child2/></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement last = taurus_element_last_child(root, NULL);

    EXPECT_ELEM_NOT_NULL(last);
    EXPECT_STREQ(taurus_element_name(last), "child2");

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, ChildByIndex) {
    TaurusDocument doc = create_doc("<node><child1/><child2/><child3/></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);

    TaurusElement child0 = taurus_element_child(root, 0);
    EXPECT_STREQ(taurus_element_name(child0), "child1");

    TaurusElement child1 = taurus_element_child(root, 1);
    EXPECT_STREQ(taurus_element_name(child1), "child2");

    TaurusElement child2 = taurus_element_child(root, 2);
    EXPECT_STREQ(taurus_element_name(child2), "child3");

    // Out of bounds should return null
    TaurusElement child3 = taurus_element_child(root, 3);
    EXPECT_ELEM_NULL(child3);

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, ChildCount) {
    TaurusDocument doc = create_doc("<node><child1/><child2/><child3/></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    int count = taurus_element_child_count(root);

    EXPECT_EQ(count, 3);

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, FindChildByName) {
    TaurusDocument doc = create_doc("<node><child1/><child2/><child3/></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);

    TaurusElement child = taurus_element_find_child(root, "child2");
    EXPECT_ELEM_NOT_NULL(child);
    EXPECT_STREQ(taurus_element_name(child), "child2");

    // Non-existent child should return null
    TaurusElement notfound = taurus_element_find_child(root, "child4");
    EXPECT_ELEM_NULL(notfound);

    taurus_document_free(doc);
}

// ============================================================================
// SIBLING NAVIGATION
// ============================================================================

TEST_F(TaurusDomTraverse, NextSibling) {
    TaurusDocument doc = create_doc("<node><child1/><child2/><child3/></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement child1 = taurus_element_first_child(root, NULL);

    TaurusElement child2 = taurus_element_next_sibling(child1, NULL);
    EXPECT_ELEM_NOT_NULL(child2);
    EXPECT_STREQ(taurus_element_name(child2), "child2");

    TaurusElement child3 = taurus_element_next_sibling(child2, NULL);
    EXPECT_ELEM_NOT_NULL(child3);
    EXPECT_STREQ(taurus_element_name(child3), "child3");

    // Last child's next sibling should be null
    TaurusElement after_last = taurus_element_next_sibling(child3, NULL);
    EXPECT_ELEM_NULL(after_last);

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, PreviousSibling) {
    TaurusDocument doc = create_doc("<node><child1/><child2/><child3/></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement child3 = taurus_element_last_child(root, NULL);

    TaurusElement child2 = taurus_element_previous_sibling(child3, NULL);
    EXPECT_ELEM_NOT_NULL(child2);
    EXPECT_STREQ(taurus_element_name(child2), "child2");

    TaurusElement child1 = taurus_element_previous_sibling(child2, NULL);
    EXPECT_ELEM_NOT_NULL(child1);
    EXPECT_STREQ(taurus_element_name(child1), "child1");

    // First child's previous sibling should be null
    TaurusElement before_first = taurus_element_previous_sibling(child1, NULL);
    EXPECT_ELEM_NULL(before_first);

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, SiblingNavigationBothDirections) {
    TaurusDocument doc = create_doc("<node><a/><b/><c/><d/><e/></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child(root, NULL);

    // Navigate forward
    TaurusElement b = taurus_element_next_sibling(a, NULL);
    TaurusElement c = taurus_element_next_sibling(b, NULL);
    EXPECT_STREQ(taurus_element_name(c), "c");

    // Navigate backward
    TaurusElement b_back = taurus_element_previous_sibling(c, NULL);
    EXPECT_STREQ(taurus_element_name(b_back), "b");

    TaurusElement a_back = taurus_element_previous_sibling(b_back, NULL);
    EXPECT_STREQ(taurus_element_name(a_back), "a");

    taurus_document_free(doc);
}

// ============================================================================
// ATTRIBUTE ITERATION
// ============================================================================

TEST_F(TaurusDomTraverse, AttributeAccess) {
    TaurusDocument doc = create_doc("<node attr1='value1' attr2='value2' attr3='value3'/>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);

    const char* attr1 = taurus_element_attribute(root, "attr1");
    EXPECT_STREQ(attr1, "value1");

    const char* attr2 = taurus_element_attribute(root, "attr2");
    EXPECT_STREQ(attr2, "value2");

    const char* attr3 = taurus_element_attribute(root, "attr3");
    EXPECT_STREQ(attr3, "value3");

    // Non-existent attribute should return null
    const char* attr4 = taurus_element_attribute(root, "attr4");
    EXPECT_EQ(attr4, nullptr);

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, AttributeCount) {
    TaurusDocument doc = create_doc("<node attr1='1' attr2='2' attr3='3'/>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);

    // Verify all three attributes exist
    EXPECT_STREQ(taurus_element_attribute(root, "attr1"), "1");
    EXPECT_STREQ(taurus_element_attribute(root, "attr2"), "2");
    EXPECT_STREQ(taurus_element_attribute(root, "attr3"), "3");

    // Verify non-existent attribute returns NULL
    EXPECT_EQ(taurus_element_attribute(root, "attr4"), nullptr);

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, RemoveAttribute) {
    TaurusDocument doc = create_doc("<node attr1='1' attr2='2' attr3='3'/>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);

    // Remove middle attribute
    taurus_element_remove_attribute(root, "attr2");

    // attr2 should be gone
    const char* attr2 = taurus_element_attribute(root, "attr2");
    EXPECT_EQ(attr2, nullptr);

    // attr1 and attr3 should still exist
    const char* attr1 = taurus_element_attribute(root, "attr1");
    EXPECT_STREQ(attr1, "1");

    const char* attr3 = taurus_element_attribute(root, "attr3");
    EXPECT_STREQ(attr3, "3");

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, RemoveAllAttributes) {
    TaurusDocument doc = create_doc("<node attr1='1' attr2='2' attr3='3'/>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);

    taurus_element_remove_all_attributes(root);

    // All attributes should be gone
    EXPECT_EQ(taurus_element_attribute(root, "attr1"), nullptr);
    EXPECT_EQ(taurus_element_attribute(root, "attr2"), nullptr);
    EXPECT_EQ(taurus_element_attribute(root, "attr3"), nullptr);

    taurus_document_free(doc);
}

// ============================================================================
// TREE WALKING
// ============================================================================

TEST_F(TaurusDomTraverse, WalkAllChildren) {
    TaurusDocument doc = create_doc("<node><a/><b/><c/><d/><e/></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    int count = 0;

    for (TaurusElement child = taurus_element_first_child(root, NULL);
         !element_is_null(child);
         child = taurus_element_next_sibling(child, NULL)) {
        count++;
    }

    EXPECT_EQ(count, 5);

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, WalkAllChildrenReverse) {
    TaurusDocument doc = create_doc("<node><a/><b/><c/><d/><e/></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    int count = 0;

    for (TaurusElement child = taurus_element_last_child(root, NULL);
         !element_is_null(child);
         child = taurus_element_previous_sibling(child, NULL)) {
        count++;
    }

    EXPECT_EQ(count, 5);

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, WalkDeepTree) {
    TaurusDocument doc = create_doc("<root><lvl1><lvl2><lvl3><leaf/></lvl3></lvl2></lvl1></root>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement lvl1 = taurus_element_first_child(root, NULL);
    TaurusElement lvl2 = taurus_element_first_child(lvl1, NULL);
    TaurusElement lvl3 = taurus_element_first_child(lvl2, NULL);
    TaurusElement leaf = taurus_element_first_child(lvl3, NULL);

    EXPECT_STREQ(taurus_element_name(leaf), "leaf");

    // Walk back up
    TaurusElement parent = taurus_element_parent(leaf);
    EXPECT_STREQ(taurus_element_name(parent), "lvl3");

    parent = taurus_element_parent(parent);
    EXPECT_STREQ(taurus_element_name(parent), "lvl2");

    parent = taurus_element_parent(parent);
    EXPECT_STREQ(taurus_element_name(parent), "lvl1");

    parent = taurus_element_parent(parent);
    EXPECT_STREQ(taurus_element_name(parent), "root");

    taurus_document_free(doc);
}

// ============================================================================
// COMPLEX TRAVERSAL
// ============================================================================

TEST_F(TaurusDomTraverse, CollectAllChildren) {
    TaurusDocument doc = create_doc("<node><a/><b/><c/><d/><e/></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    std::vector<std::string> children;

    for (TaurusElement child = taurus_element_first_child(root, NULL);
         !element_is_null(child);
         child = taurus_element_next_sibling(child, NULL)) {
        children.push_back(taurus_element_name(child));
    }

    EXPECT_EQ(children.size(), 5);
    EXPECT_EQ(children[0], "a");
    EXPECT_EQ(children[1], "b");
    EXPECT_EQ(children[2], "c");
    EXPECT_EQ(children[3], "d");
    EXPECT_EQ(children[4], "e");

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, FindSpecificChild) {
    TaurusDocument doc = create_doc("<node><a/><b/><target/><d/><e/></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement target = taurus_element_handle_null();

    for (TaurusElement child = taurus_element_first_child(root, NULL);
         !element_is_null(child) && element_is_null(target);
         child = taurus_element_next_sibling(child, NULL)) {
        if (strcmp(taurus_element_name(child), "target") == 0) {
            target = child;
        }
    }

    EXPECT_ELEM_NOT_NULL(target);
    EXPECT_STREQ(taurus_element_name(target), "target");

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, SiblingsWithChildren) {
    TaurusDocument doc = create_doc("<node><a><x/></a><b><y/></b><c><z/></c></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child(root, NULL);
    TaurusElement b = taurus_element_next_sibling(a, NULL);
    TaurusElement c = taurus_element_next_sibling(b, NULL);

    // Each sibling should have one child
    EXPECT_EQ(taurus_element_child_count(a), 1);
    EXPECT_EQ(taurus_element_child_count(b), 1);
    EXPECT_EQ(taurus_element_child_count(c), 1);

    EXPECT_STREQ(taurus_element_name(taurus_element_first_child(a, NULL)), "x");
    EXPECT_STREQ(taurus_element_name(taurus_element_first_child(b, NULL)), "y");
    EXPECT_STREQ(taurus_element_name(taurus_element_first_child(c, NULL)), "z");

    taurus_document_free(doc);
}

// ============================================================================
// EDGE CASES
// ============================================================================

TEST_F(TaurusDomTraverse, NavigateEmptyElement) {
    TaurusDocument doc = create_doc("<node/>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);

    EXPECT_ELEM_RETURN_NULL(taurus_element_first_child(root, NULL));
    EXPECT_ELEM_RETURN_NULL(taurus_element_last_child(root, NULL));
    EXPECT_EQ(taurus_element_child_count(root), 0);

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, NavigateSingleChild) {
    TaurusDocument doc = create_doc("<node><only/></node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement first = taurus_element_first_child(root, NULL);
    TaurusElement last = taurus_element_last_child(root, NULL);

    // First and last should be same element
    EXPECT_ELEM_EQ(first, last);
    EXPECT_STREQ(taurus_element_name(first), "only");

    // No siblings
    EXPECT_ELEM_RETURN_NULL(taurus_element_next_sibling(first, NULL));
    EXPECT_ELEM_RETURN_NULL(taurus_element_previous_sibling(first, NULL));

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, NavigateTextMixedContent) {
    TaurusDocument doc = create_doc("<node>text1<middle/>text2</node>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);

    // Taurus child_count counts element children only, not text nodes
    // So we expect 1 element child (<middle/>)
    EXPECT_EQ(taurus_element_child_count(root), 1);

    // First element child should be <middle/>
    TaurusElement middle = taurus_element_first_child(root, NULL);
    EXPECT_STREQ(taurus_element_name(middle), "middle");

    taurus_document_free(doc);
}

// ============================================================================
// PERFORMANCE TRAVERSAL
// ============================================================================

TEST_F(TaurusDomTraverse, LargeSiblingList) {
    // Create XML with 100 children
    std::string xml = "<node>";
    for (int i = 0; i < 100; i++) {
        xml += "<child" + std::to_string(i) + "/>";
    }
    xml += "</node>";

    TaurusDocument doc = create_doc(xml.c_str());
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    int count = 0;

    // Walk all children
    for (TaurusElement child = taurus_element_first_child(root, NULL);
         !element_is_null(child);
         child = taurus_element_next_sibling(child, NULL)) {
        count++;
    }

    EXPECT_EQ(count, 100);

    // Walk backwards
    count = 0;
    for (TaurusElement child = taurus_element_last_child(root, NULL);
         !element_is_null(child);
         child = taurus_element_previous_sibling(child, NULL)) {
        count++;
    }

    EXPECT_EQ(count, 100);

    taurus_document_free(doc);
}

TEST_F(TaurusDomTraverse, DeepTreeTraversal) {
    // Create a deep tree with 10 levels (simpler than 50 to avoid parser limits)
    std::string xml = "<root><a0>";
    xml += "<a1><a2><a3><a4><a5><a6><a7><a8><leaf/></a8></a7></a6></a5></a4></a3></a2></a1></a0></root>";

    TaurusDocument doc = create_doc(xml.c_str());
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement current = root;

    // Walk down to leaf through 10 levels
    const char* expected_names[] = {"root", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "leaf"};
    for (size_t i = 0; i < 10; i++) {
        current = taurus_element_first_child(current, NULL);
        EXPECT_ELEM_NOT_NULL(current);
        EXPECT_STREQ(taurus_element_name(current), expected_names[i + 1]);
    }

    EXPECT_STREQ(taurus_element_name(current), "leaf");

    // Walk back up to root
    for (int i = 0; i < 10; i++) {
        current = taurus_element_parent(current);
        EXPECT_ELEM_NOT_NULL(current);
    }

    EXPECT_STREQ(taurus_element_name(current), "root");

    taurus_document_free(doc);
}
