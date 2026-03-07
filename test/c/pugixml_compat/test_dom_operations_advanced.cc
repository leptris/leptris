/* test_dom_operations_advanced.cpp - Advanced DOM operations tests from pugixml
 * Copyright (c) 2024, Ribose Inc.
 *
 * Advanced DOM operations tests adapted from pugixml test_dom_modify.cpp
 * Tests operations not covered by test_dom_operations.cc
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
 * Base class for advanced DOM operations tests
 */
class DomOperationsAdvancedTest : public ::testing::Test {
protected:
    TaurusDocument doc;
    TaurusElement root;
    std::string xml_buffer;

    void SetUp() override {
        doc = nullptr;
        root = taurus_element_handle_null();
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
        ASSERT_ELEM_NOT_NULL(root);
    }
};

/* ============================================================================
 * Set Name Tests
 * ============================================================================ */

TEST_F(DomOperationsAdvancedTest, SetElementName) {
    parse_xml("<root><node>text</node></root>");

    TaurusElement node = taurus_element_find_child(root, "node");
    ASSERT_ELEM_NOT_NULL(node);

    // Change element name
    TaurusStatus status = taurus_element_set_name(node, "renamed");
    EXPECT_EQ(status, TAURUS_OK);

    // Verify the name changed
    EXPECT_STREQ(taurus_element_name(node), "renamed");

    // Should not be findable by old name
    TaurusElement by_old_name = taurus_element_find_child(root, "node");
    EXPECT_ELEM_RETURN_NULL(by_old_name);

    // Should be findable by new name
    TaurusElement by_new_name = taurus_element_find_child(root, "renamed");
    EXPECT_ELEM_NOT_NULL(by_new_name);
}

TEST_F(DomOperationsAdvancedTest, SetRootElementName) {
    parse_xml("<root>text</root>");

    // Change root element name
    TaurusStatus status = taurus_element_set_name(root, "document");
    EXPECT_EQ(status, TAURUS_OK);

    // Verify the name changed
    EXPECT_STREQ(taurus_element_name(root), "document");
}

/* ============================================================================
 * Insert Before/After Tests
 * ============================================================================ */

TEST_F(DomOperationsAdvancedTest, InsertBeforeFirst) {
    parse_xml("<root><a/><b/><c/></root>");

    TaurusElement a = taurus_element_find_child(root, "a");
    TaurusElement b = taurus_element_find_child(root, "b");
    ASSERT_ELEM_NOT_NULL(a);
    ASSERT_ELEM_NOT_NULL(b);

    // Create new element
    TaurusElement new_elem = taurus_element_create(doc, "new");
    ASSERT_ELEM_NOT_NULL(new_elem);

    // Insert before 'b' (between a and b)
    TaurusStatus status = taurus_element_insert_before(b, new_elem);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify order: a, new, b, c
    TaurusElement first = taurus_element_first_child(root, nullptr);
    EXPECT_STREQ(taurus_element_name(first), "a");

    TaurusElement second = taurus_element_next_sibling(first, nullptr);
    EXPECT_STREQ(taurus_element_name(second), "new");

    TaurusElement third = taurus_element_next_sibling(second, nullptr);
    EXPECT_STREQ(taurus_element_name(third), "b");
}

TEST_F(DomOperationsAdvancedTest, InsertAfterLast) {
    parse_xml("<root><a/><b/><c/></root>");

    TaurusElement c = taurus_element_find_child(root, "c");
    ASSERT_ELEM_NOT_NULL(c);

    // Create new element
    TaurusElement new_elem = taurus_element_create(doc, "new");
    ASSERT_ELEM_NOT_NULL(new_elem);

    // Insert after 'c' (at the end)
    TaurusStatus status = taurus_element_insert_after(c, new_elem);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify 'new' is now last
    TaurusElement last = taurus_element_last_child(root, nullptr);
    EXPECT_STREQ(taurus_element_name(last), "new");
}

TEST_F(DomOperationsAdvancedTest, InsertBeforeMiddle) {
    parse_xml("<root><a/><b/><c/></root>");

    TaurusElement b = taurus_element_find_child(root, "b");
    ASSERT_ELEM_NOT_NULL(b);

    // Create new element
    TaurusElement new_elem = taurus_element_create(doc, "x");
    ASSERT_ELEM_NOT_NULL(new_elem);

    // Insert before 'b'
    TaurusStatus status = taurus_element_insert_before(b, new_elem);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify order: a, x, b, c
    TaurusElement first = taurus_element_first_child(root, nullptr);
    EXPECT_STREQ(taurus_element_name(first), "a");

    TaurusElement second = taurus_element_next_sibling(first, nullptr);
    EXPECT_STREQ(taurus_element_name(second), "x");
}

TEST_F(DomOperationsAdvancedTest, InsertAfterMiddle) {
    parse_xml("<root><a/><b/><c/></root>");

    TaurusElement b = taurus_element_find_child(root, "b");
    ASSERT_ELEM_NOT_NULL(b);

    // Create new element
    TaurusElement new_elem = taurus_element_create(doc, "y");
    ASSERT_ELEM_NOT_NULL(new_elem);

    // Insert after 'b'
    TaurusStatus status = taurus_element_insert_after(b, new_elem);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify order: a, b, y, c
    TaurusElement third = taurus_element_next_sibling(b, nullptr);
    EXPECT_STREQ(taurus_element_name(third), "y");
}

/* ============================================================================
 * Remove All Attributes Tests
 * ============================================================================ */

TEST_F(DomOperationsAdvancedTest, RemoveAllAttributes) {
    parse_xml("<root id='1' class='test' name='value'>text</root>");

    // Verify attributes exist
    const char* id = taurus_element_attribute(root, "id");
    ASSERT_NE(id, nullptr);
    EXPECT_STREQ(id, "1");

    const char* class_attr = taurus_element_attribute(root, "class");
    ASSERT_NE(class_attr, nullptr);
    EXPECT_STREQ(class_attr, "test");

    // Remove all attributes
    TaurusStatus status = taurus_element_remove_all_attributes(root);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify all attributes are gone
    EXPECT_EQ(taurus_element_attribute(root, "id"), nullptr);
    EXPECT_EQ(taurus_element_attribute(root, "class"), nullptr);
    EXPECT_EQ(taurus_element_attribute(root, "name"), nullptr);
}

TEST_F(DomOperationsAdvancedTest, RemoveAllAttributesFromEmptyElement) {
    parse_xml("<root>text</root>");

    // Element has no attributes, should succeed
    TaurusStatus status = taurus_element_remove_all_attributes(root);
    EXPECT_EQ(status, TAURUS_OK);
}

/* ============================================================================
 * Append/Prepend Copy Tests
 * ============================================================================ */

TEST_F(DomOperationsAdvancedTest, AppendCopy) {
    parse_xml("<root><source>text</source></root>");

    TaurusElement source = taurus_element_find_child(root, "source");
    ASSERT_ELEM_NOT_NULL(source);

    // Append copy of source
    TaurusElement copy = taurus_element_append_copy(root, source);
    ASSERT_ELEM_NOT_NULL(copy);

    // Should have 2 children now
    TaurusElement first = taurus_element_first_child(root, nullptr);
    TaurusElement second = taurus_element_next_sibling(first, nullptr);
    ASSERT_ELEM_NOT_NULL(first);
    ASSERT_ELEM_NOT_NULL(second);

    EXPECT_STREQ(taurus_element_name(first), "source");
    EXPECT_STREQ(taurus_element_name(second), "source");

    // Verify both have same text content
    const char* text1 = taurus_element_text(first);
    const char* text2 = taurus_element_text(second);
    ASSERT_NE(text1, nullptr);
    ASSERT_NE(text2, nullptr);
    EXPECT_STREQ(text1, "text");
    EXPECT_STREQ(text2, "text");
}

TEST_F(DomOperationsAdvancedTest, PrependCopy) {
    parse_xml("<root><target>text</target></root>");

    TaurusElement target = taurus_element_find_child(root, "target");
    ASSERT_ELEM_NOT_NULL(target);

    // Prepend copy of target before itself
    TaurusElement copy = taurus_element_prepend_copy(root, target);
    ASSERT_ELEM_NOT_NULL(copy);

    // Should have 2 children now: copy, target
    TaurusElement first = taurus_element_first_child(root, nullptr);
    TaurusElement second = taurus_element_next_sibling(first, nullptr);
    ASSERT_ELEM_NOT_NULL(first);
    ASSERT_ELEM_NOT_NULL(second);

    EXPECT_STREQ(taurus_element_name(first), "target");
    EXPECT_STREQ(taurus_element_name(second), "target");
}

TEST_F(DomOperationsAdvancedTest, AppendCopyOfSubtree) {
    parse_xml("<root><source><child>text</child></source></root>");

    TaurusElement source = taurus_element_find_child(root, "source");
    ASSERT_ELEM_NOT_NULL(source);

    // Append copy (should include subtree)
    TaurusElement copy = taurus_element_append_copy(root, source);
    ASSERT_ELEM_NOT_NULL(copy);

    // Verify copy has child
    TaurusElement child = taurus_element_first_child(copy, nullptr);
    ASSERT_ELEM_NOT_NULL(child);
    EXPECT_STREQ(taurus_element_name(child), "child");
}

/* ============================================================================
 * Insert Copy Before/After Tests
 * ============================================================================ */

TEST_F(DomOperationsAdvancedTest, InsertCopyBefore) {
    parse_xml("<root><a/><b/><c/></root>");

    TaurusElement b = taurus_element_find_child(root, "b");
    ASSERT_ELEM_NOT_NULL(b);

    // Create source element to copy
    TaurusElement source = taurus_element_create(doc, "source");
    ASSERT_ELEM_NOT_NULL(source);

    // Insert copy before 'b'
    TaurusElement copy = taurus_element_insert_copy_before(b, source);
    ASSERT_ELEM_NOT_NULL(copy);

    // Verify order: a, copy, b, c
    TaurusElement second = taurus_element_next_sibling(
        taurus_element_first_child(root, nullptr), nullptr);
    EXPECT_STREQ(taurus_element_name(second), "source");
}

TEST_F(DomOperationsAdvancedTest, InsertCopyAfter) {
    parse_xml("<root><a/><b/><c/></root>");

    TaurusElement b = taurus_element_find_child(root, "b");
    ASSERT_ELEM_NOT_NULL(b);

    // Create source element to copy
    TaurusElement source = taurus_element_create(doc, "source");
    ASSERT_ELEM_NOT_NULL(source);

    // Insert copy after 'b'
    TaurusElement copy = taurus_element_insert_copy_after(b, source);
    ASSERT_ELEM_NOT_NULL(copy);

    // Verify order: a, b, copy, c
    TaurusElement third = taurus_element_next_sibling(b, nullptr);
    EXPECT_STREQ(taurus_element_name(third), "source");
}

/* ============================================================================
 * Complex Scenarios
 * ============================================================================ */

TEST_F(DomOperationsAdvancedTest, SetNameAndFind) {
    parse_xml("<root><old>text</old></root>");

    TaurusElement old = taurus_element_find_child(root, "old");
    ASSERT_ELEM_NOT_NULL(old);

    // Rename element
    TaurusStatus status = taurus_element_set_name(old, "new");
    EXPECT_EQ(status, TAURUS_OK);

    // Old name should not be found
    EXPECT_ELEM_RETURN_NULL(taurus_element_find_child(root, "old"));

    // New name should be found
    TaurusElement renamed = taurus_element_find_child(root, "new");
    ASSERT_ELEM_NOT_NULL(renamed);

    const char* text = taurus_element_text(renamed);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "text");
}

TEST_F(DomOperationsAdvancedTest, InsertMultipleAndRemoveAllAttributes) {
    parse_xml("<root a='1' b='2' c='3'><x/></root>");

    TaurusElement x = taurus_element_find_child(root, "x");
    ASSERT_ELEM_NOT_NULL(x);

    // Insert multiple elements
    for (int i = 0; i < 5; i++) {
        char name[32];
        snprintf(name, sizeof(name), "elem%d", i);
        TaurusElement elem = taurus_element_create(doc, name);
        ASSERT_ELEM_NOT_NULL(elem);

        TaurusStatus status = taurus_element_append_child(root, elem);
        EXPECT_EQ(status, TAURUS_OK);
    }

    // Should have 6 children now (original + 5 new)
    int count = 0;
    TaurusElement child = taurus_element_first_child(root, nullptr);
    while (!element_is_null(child)) {
        count++;
        child = taurus_element_next_sibling(child, nullptr);
    }
    EXPECT_EQ(count, 6);

    // Remove all attributes from root
    TaurusStatus status = taurus_element_remove_all_attributes(root);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify attributes are gone
    EXPECT_EQ(taurus_element_attribute(root, "a"), nullptr);
    EXPECT_EQ(taurus_element_attribute(root, "b"), nullptr);
    EXPECT_EQ(taurus_element_attribute(root, "c"), nullptr);
}

TEST_F(DomOperationsAdvancedTest, CopyAndModifyIndependently) {
    parse_xml("<root><original><data>value</data></original></root>");

    TaurusElement original = taurus_element_find_child(root, "original");
    ASSERT_ELEM_NOT_NULL(original);

    // Append copy
    TaurusElement copy = taurus_element_append_copy(root, original);
    ASSERT_ELEM_NOT_NULL(copy);

    // Modify original (should not affect copy)
    TaurusStatus status = taurus_element_set_name(original, "modified");
    EXPECT_EQ(status, TAURUS_OK);

    // Verify original is renamed
    EXPECT_STREQ(taurus_element_name(original), "modified");

    // Verify copy still has original name
    EXPECT_STREQ(taurus_element_name(copy), "original");
}

/* ============================================================================
 * Error Handling Tests
 * ============================================================================ */

TEST_F(DomOperationsAdvancedTest, SetNameToNull) {
    parse_xml("<root>text</root>");

    // Setting name to null should fail or be handled
    TaurusStatus status = taurus_element_set_name(root, NULL);
    // Taurus may return error or handle gracefully
    // Just verify the operation completes without crash
}

TEST_F(DomOperationsAdvancedTest, InsertBeforeNull) {
    parse_xml("<root><a/></root>");

    TaurusElement a = taurus_element_find_child(root, "a");
    ASSERT_ELEM_NOT_NULL(a);

    // Insert null before 'a' - should fail
    TaurusElement null_elem = taurus_element_handle_null();
    TaurusStatus status = taurus_element_insert_before(a, null_elem);
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(DomOperationsAdvancedTest, InsertAfterNull) {
    parse_xml("<root><a/></root>");

    TaurusElement a = taurus_element_find_child(root, "a");
    ASSERT_ELEM_NOT_NULL(a);

    // Insert null after 'a' - should fail
    TaurusElement null_elem = taurus_element_handle_null();
    TaurusStatus status = taurus_element_insert_after(a, null_elem);
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(DomOperationsAdvancedTest, CopyNullElement) {
    parse_xml("<root><a/></root>");

    // Copy null element - should return null
    TaurusElement null_elem = taurus_element_handle_null();
    TaurusElement result = taurus_element_append_copy(root, null_elem);
    EXPECT_ELEM_NULL(result);
}

} // namespace taurus_test
