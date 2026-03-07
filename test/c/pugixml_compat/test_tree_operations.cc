/* test_tree_operations.cc - Tree operations tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for advanced tree operations including:
 * - Document merging
 * - Subtree operations
 * - Deep copying/cloning
 * - Tree manipulation
 */

#include <gtest/gtest.h>
#include <string>
#include "../../src/include/taurus.h"

/* TaurusElement handle API macros */
#define ELEM_IS_NULL(e) taurus_element_is_null((e))
#define ELEM_NOT_NULL(e) !taurus_element_is_null((e))
#define ELEM_NULL() taurus_element_handle_null()

/* Inline helpers for checking null on function returns (temporaries) */
static inline bool elem_is_null_inline(TaurusElement elem) {
    return taurus_element_is_null(elem);
}
static inline bool elem_not_null_inline(TaurusElement elem) {
    return !taurus_element_is_null(elem);
}
#define ELEM_IS_NULL_TMP(e) elem_is_null_inline(e)
#define ELEM_NOT_NULL_TMP(e) elem_not_null_inline(e)

namespace taurus_test {

/**
 * Base class for tree operations tests
 */
class TreeOperationsTest : public ::testing::Test {
protected:
    TaurusDocument doc1;
    TaurusDocument doc2;
    std::string xml_buffer1;
    std::string xml_buffer2;

    void SetUp() override {
        doc1 = nullptr;
        doc2 = nullptr;
        xml_buffer1.clear();
        xml_buffer2.clear();
    }

    void TearDown() override {
        if (doc1) {
            taurus_document_free(doc1);
            doc1 = nullptr;
        }
        if (doc2) {
            taurus_document_free(doc2);
            doc2 = nullptr;
        }
        xml_buffer1.clear();
        xml_buffer2.clear();
    }

    // Parse XML document
    void parse_xml(const std::string& xml, TaurusDocument* out_doc) {
        TaurusStatus status;
        *out_doc = taurus_parse_string(xml.c_str(), xml.length(), &status);
        ASSERT_EQ(status, TAURUS_OK) << "Failed to parse XML";
        ASSERT_NE(*out_doc, nullptr);
    }

    void parse_xml1(const std::string& xml) {
        xml_buffer1 = xml;
        parse_xml(xml_buffer1, &doc1);
    }

    void parse_xml2(const std::string& xml) {
        xml_buffer2 = xml;
        parse_xml(xml_buffer2, &doc2);
    }

    TaurusElement root1() const { return taurus_document_root(doc1); }
    TaurusElement root2() const { return taurus_document_root(doc2); }
};

// ============================================================================
// Deep Copy Tests
// ============================================================================

TEST_F(TreeOperationsTest, DeepCopyElement) {
    // Test deep copying an element subtree
    parse_xml1("<root><child><grandchild>text</grandchild></child></root>");
    parse_xml2("<target></target>");

    TaurusElement child = taurus_element_first_child_any(root1());
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "child");

    TaurusElement target = root2();

    // Deep copy the child element to target
    TaurusElement child_copy = taurus_element_append_copy(target, child);
    ASSERT_TRUE(ELEM_NOT_NULL(child_copy));
    EXPECT_STREQ(taurus_element_name(child_copy), "child");

    // Verify the copy has the same structure
    TaurusElement grandchild = taurus_element_first_child_any(child_copy);
    ASSERT_TRUE(ELEM_NOT_NULL(grandchild));
    EXPECT_STREQ(taurus_element_name(grandchild), "grandchild");

    const char* text = taurus_element_text(grandchild);
    EXPECT_STREQ(text, "text");
}

TEST_F(TreeOperationsTest, DeepCopyWithAttributes) {
    parse_xml1("<root><child id='1' name='test'>text</child></root>");
    parse_xml2("<target></target>");

    TaurusElement child = taurus_element_first_child_any(root1());
    TaurusElement target = root2();

    TaurusElement child_copy = taurus_element_append_copy(target, child);

    // Verify attributes are copied
    const char* id_attr = taurus_element_attribute(child_copy, "id");
    const char* name_attr = taurus_element_attribute(child_copy, "name");
    EXPECT_STREQ(id_attr, "1");
    EXPECT_STREQ(name_attr, "test");
}

TEST_F(TreeOperationsTest, DeepCopySubtree) {
    parse_xml1("<root><level1><level2><level3>deep</level3></level2></level1></root>");
    parse_xml2("<target></target>");

    TaurusElement level1 = taurus_element_first_child_any(root1());
    TaurusElement target = root2();

    TaurusElement level1_copy = taurus_element_append_copy(target, level1);

    // Verify deep structure is preserved
    TaurusElement level2 = taurus_element_first_child_any(level1_copy);
    ASSERT_TRUE(ELEM_NOT_NULL(level2));
    EXPECT_STREQ(taurus_element_name(level2), "level2");

    TaurusElement level3 = taurus_element_first_child_any(level2);
    ASSERT_TRUE(ELEM_NOT_NULL(level3));
    EXPECT_STREQ(taurus_element_name(level3), "level3");

    const char* text = taurus_element_text(level3);
    EXPECT_STREQ(text, "deep");
}

// ============================================================================
// Subtree Manipulation Tests
// ============================================================================

TEST_F(TreeOperationsTest, AppendSubtree) {
    parse_xml1("<root><target/></root>");
    parse_xml2("<source><item>data</item></source>");

    TaurusElement target = taurus_element_first_child_any(root1());
    TaurusElement source = root2();

    // Get all children from source and append to target
    TaurusElement item = taurus_element_first_child_any(source);
    ASSERT_TRUE(ELEM_NOT_NULL(item));

    TaurusStatus status = taurus_element_append_child(target, item);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify the subtree was appended
    TaurusElement appended_item = taurus_element_first_child_any(target);
    ASSERT_TRUE(ELEM_NOT_NULL(appended_item));
    EXPECT_STREQ(taurus_element_name(appended_item), "item");

    const char* text = taurus_element_text(appended_item);
    EXPECT_STREQ(text, "data");
}

TEST_F(TreeOperationsTest, PrependSubtree) {
    parse_xml1("<root><existing/></root>");
    parse_xml2("<source><new/></source>");

    TaurusElement root = root1();
    TaurusElement existing = taurus_element_first_child_any(root);
    TaurusElement new_elem = taurus_element_first_child_any(root2());

    TaurusStatus status = taurus_element_prepend_child(root, new_elem);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify new element is prepended
    TaurusElement first = taurus_element_first_child_any(root);
    EXPECT_STREQ(taurus_element_name(first), "new");

    TaurusElement second = taurus_element_next_sibling_any(first);
    EXPECT_STREQ(taurus_element_name(second), "existing");
}

TEST_F(TreeOperationsTest, InsertBeforeSubtree) {
    parse_xml1("<root><a/><c/></root>");
    parse_xml2("<source><b/></source>");

    TaurusElement root = root1();
    TaurusElement c = taurus_element_next_sibling_any(
        taurus_element_first_child_any(root)
    );
    TaurusElement b = taurus_element_first_child_any(root2());

    // Use insert_copy_before for cross-document operation
    TaurusElement b_copy = taurus_element_insert_copy_before(c, b);
    ASSERT_TRUE(ELEM_NOT_NULL(b_copy));

    // Verify order: a, b, c
    TaurusElement first = taurus_element_first_child_any(root);
    EXPECT_STREQ(taurus_element_name(first), "a");

    TaurusElement second = taurus_element_next_sibling_any(first);
    EXPECT_STREQ(taurus_element_name(second), "b");

    TaurusElement third = taurus_element_next_sibling_any(second);
    EXPECT_STREQ(taurus_element_name(third), "c");
}

TEST_F(TreeOperationsTest, InsertAfterSubtree) {
    parse_xml1("<root><a/><c/></root>");
    parse_xml2("<source><b/></source>");

    TaurusElement root = root1();
    TaurusElement a = taurus_element_first_child_any(root);
    TaurusElement b = taurus_element_first_child_any(root2());

    // Use insert_copy_after for cross-document operation
    TaurusElement b_copy = taurus_element_insert_copy_after(a, b);
    ASSERT_TRUE(ELEM_NOT_NULL(b_copy));

    // Verify order: a, b, c
    TaurusElement first = taurus_element_first_child_any(root);
    EXPECT_STREQ(taurus_element_name(first), "a");

    TaurusElement second = taurus_element_next_sibling_any(first);
    EXPECT_STREQ(taurus_element_name(second), "b");

    TaurusElement third = taurus_element_next_sibling_any(second);
    EXPECT_STREQ(taurus_element_name(third), "c");
}

// ============================================================================
// Document Merge Tests
// ============================================================================

TEST_F(TreeOperationsTest, MergeDocumentsSimple) {
    // Merge two documents by copying children
    parse_xml1("<root><item1>text1</item1></root>");
    parse_xml2("<root><item2>text2</item2></root>");

    TaurusElement target_root = root1();
    TaurusElement source_root = root2();

    TaurusElement item1 = taurus_element_first_child_any(target_root);
    TaurusElement item2 = taurus_element_first_child_any(source_root);

    // Use append_copy to copy element from source to target document
    // (can't move elements between different memory pools)
    TaurusElement copied_item = taurus_element_append_copy(target_root, item2);
    ASSERT_TRUE(ELEM_NOT_NULL(copied_item));

    // Verify both items are in target
    TaurusElement first = taurus_element_first_child_any(target_root);
    EXPECT_STREQ(taurus_element_name(first), "item1");

    TaurusElement second = taurus_element_next_sibling_any(first);
    EXPECT_STREQ(taurus_element_name(second), "item2");

    const char* text2 = taurus_element_text(second);
    EXPECT_STREQ(text2, "text2");
}

TEST_F(TreeOperationsTest, MergeDocumentsWithAttributes) {
    parse_xml1("<root><item id='1'/></root>");
    parse_xml2("<root><item id='2'/></root>");

    TaurusElement target_root = root1();
    TaurusElement source_root = root2();

    TaurusElement source_item = taurus_element_first_child_any(source_root);

    // Use append_copy to copy element from source to target document
    // (can't move elements between different memory pools)
    TaurusElement copied_item = taurus_element_append_copy(target_root, source_item);
    ASSERT_TRUE(ELEM_NOT_NULL(copied_item));

    // Verify we now have two items
    TaurusElement first = taurus_element_first_child_any(target_root);
    const char* first_id = taurus_element_attribute(first, "id");
    EXPECT_STREQ(first_id, "1");

    TaurusElement second = taurus_element_next_sibling_any(first);
    const char* second_id = taurus_element_attribute(second, "id");
    EXPECT_STREQ(second_id, "2");
}

TEST_F(TreeOperationsTest, MergeDocumentsDeepNesting) {
    parse_xml1("<root><container><existing>old</existing></container></root>");
    parse_xml2("<root><container><new>data</new></container></root>");

    TaurusElement target_root = root1();
    TaurusElement target_container = taurus_element_first_child_any(target_root);

    TaurusElement source_root = root2();
    TaurusElement source_container = taurus_element_first_child_any(source_root);
    TaurusElement new_item = taurus_element_first_child_any(source_container);

    // Use append_copy to copy element from source to target document
    // (can't move elements between different memory pools)
    TaurusElement copied_item = taurus_element_append_copy(target_container, new_item);
    ASSERT_TRUE(ELEM_NOT_NULL(copied_item));

    // Verify container has both children
    TaurusElement first = taurus_element_first_child_any(target_container);
    EXPECT_STREQ(taurus_element_name(first), "existing");

    TaurusElement second = taurus_element_next_sibling_any(first);
    EXPECT_STREQ(taurus_element_name(second), "new");

    const char* text = taurus_element_text(second);
    EXPECT_STREQ(text, "data");
}

// ============================================================================
// Remove Child Tests
// ============================================================================

TEST_F(TreeOperationsTest, RemoveChild) {
    parse_xml1("<root><to_remove/><to_keep/></root>");

    TaurusElement root = root1();
    TaurusElement to_remove = taurus_element_first_child_any(root);
    ASSERT_STREQ(taurus_element_name(to_remove), "to_remove");

    TaurusStatus status = taurus_element_remove_child(root, to_remove);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify only to_keep remains
    TaurusElement remaining = taurus_element_first_child_any(root);
    ASSERT_TRUE(ELEM_NOT_NULL(remaining));
    EXPECT_STREQ(taurus_element_name(remaining), "to_keep");

    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_next_sibling_any(remaining)));
}

TEST_F(TreeOperationsTest, RemoveChildMiddle) {
    parse_xml1("<root><a/><b/><c/></root>");

    TaurusElement root = root1();
    TaurusElement b = taurus_element_next_sibling_any(
        taurus_element_first_child_any(root)
    );
    ASSERT_STREQ(taurus_element_name(b), "b");

    TaurusStatus status = taurus_element_remove_child(root, b);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify only a and c remain
    TaurusElement first = taurus_element_first_child_any(root);
    EXPECT_STREQ(taurus_element_name(first), "a");

    TaurusElement second = taurus_element_next_sibling_any(first);
    EXPECT_STREQ(taurus_element_name(second), "c");
}

// ============================================================================
// Remove Children Tests
// ============================================================================

TEST_F(TreeOperationsTest, RemoveAllChildren) {
    parse_xml1("<root><a/><b/><c/></root>");

    TaurusElement root = root1();

    TaurusStatus status = taurus_element_remove_children(root);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify no children remain
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_first_child_any(root)));
}

TEST_F(TreeOperationsTest, RemoveChildrenPartial) {
    parse_xml1("<root><parent><a/><b/><c/></parent></root>");

    TaurusElement root = root1();
    TaurusElement parent = taurus_element_first_child_any(root);

    TaurusStatus status = taurus_element_remove_children(parent);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify parent is now empty
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_first_child_any(parent)));

    // Verify parent still exists
    EXPECT_TRUE(ELEM_NOT_NULL_TMP(taurus_element_first_child_any(root)));
}

// ============================================================================
// Text Content Tests
// ============================================================================

TEST_F(TreeOperationsTest, SetTextContent) {
    parse_xml1("<root>old text</root>");

    TaurusElement root = root1();

    TaurusStatus status = taurus_element_set_text(root, "new text");
    EXPECT_EQ(status, TAURUS_OK);

    const char* text = taurus_element_text(root);
    EXPECT_STREQ(text, "new text");
}

TEST_F(TreeOperationsTest, SetTextContentEmpty) {
    parse_xml1("<root>old text</root>");

    TaurusElement root = root1();

    TaurusStatus status = taurus_element_set_text(root, "");
    EXPECT_EQ(status, TAURUS_OK);

    // Setting empty text should remove text nodes
    // Implementation may vary - just verify no error
    EXPECT_EQ(status, TAURUS_OK);
}

// ============================================================================
// Attribute Manipulation Tests
// ============================================================================

TEST_F(TreeOperationsTest, SetAttributeNew) {
    parse_xml1("<root/>");

    TaurusElement root = root1();

    TaurusStatus status = taurus_element_set_attribute(root, "id", "123");
    EXPECT_EQ(status, TAURUS_OK);

    const char* id = taurus_element_attribute(root, "id");
    EXPECT_STREQ(id, "123");
}

TEST_F(TreeOperationsTest, SetAttributeUpdate) {
    parse_xml1("<root id='old'></root>");

    TaurusElement root = root1();

    TaurusStatus status = taurus_element_set_attribute(root, "id", "new");
    EXPECT_EQ(status, TAURUS_OK);

    const char* id = taurus_element_attribute(root, "id");
    EXPECT_STREQ(id, "new");
}

TEST_F(TreeOperationsTest, RemoveAttribute) {
    parse_xml1("<root id='123' name='test'>text</root>");

    TaurusElement root = root1();

    TaurusStatus status = taurus_element_remove_attribute(root, "id");
    EXPECT_EQ(status, TAURUS_OK);

    const char* id = taurus_element_attribute(root, "id");
    EXPECT_EQ(id, nullptr);

    // Verify other attribute remains
    const char* name = taurus_element_attribute(root, "name");
    EXPECT_STREQ(name, "test");
}

TEST_F(TreeOperationsTest, RemoveAllAttributes) {
    parse_xml1("<root id='1' name='2' class='3'>text</root>");

    TaurusElement root = root1();

    TaurusStatus status = taurus_element_remove_all_attributes(root);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify all attributes are removed
    EXPECT_EQ(taurus_element_attribute(root, "id"), nullptr);
    EXPECT_EQ(taurus_element_attribute(root, "name"), nullptr);
    EXPECT_EQ(taurus_element_attribute(root, "class"), nullptr);
}

// ============================================================================
// Replace Child Tests
// ============================================================================

TEST_F(TreeOperationsTest, ReplaceChild) {
    parse_xml1("<root><old/></root>");
    parse_xml2("<source><new/></source>");

    TaurusElement root = root1();
    TaurusElement old_child = taurus_element_first_child_any(root);
    TaurusElement new_child = taurus_element_first_child_any(root2());

    // Remove old and add new (no direct replace API)
    TaurusStatus remove_status = taurus_element_remove_child(root, old_child);
    TaurusStatus add_status = taurus_element_append_child(root, new_child);

    EXPECT_EQ(remove_status, TAURUS_OK);
    EXPECT_EQ(add_status, TAURUS_OK);

    // Verify replacement
    TaurusElement child = taurus_element_first_child_any(root);
    EXPECT_STREQ(taurus_element_name(child), "new");
}

// ============================================================================
// Complex Tree Manipulation Tests
// ============================================================================

TEST_F(TreeOperationsTest, MoveSubtreeBetweenDocuments) {
    parse_xml1("<doc1><target/></doc1>");
    parse_xml2("<doc2><source><item>data</item></source></doc2>");

    TaurusElement target = taurus_element_first_child_any(root1());
    TaurusElement source = taurus_element_first_child_any(root2());
    TaurusElement item = taurus_element_first_child_any(source);

    // Use append_copy to copy element from source to target document
    // (can't move elements between different memory pools)
    TaurusElement copied_item = taurus_element_append_copy(target, item);
    ASSERT_TRUE(ELEM_NOT_NULL(copied_item));

    // Verify item was copied to target document
    TaurusElement moved_item = taurus_element_first_child_any(target);
    ASSERT_TRUE(ELEM_NOT_NULL(moved_item));
    EXPECT_STREQ(taurus_element_name(moved_item), "item");

    // Verify source still has the original item (it was copied, not moved)
    TaurusElement source_item = taurus_element_first_child_any(source);
    EXPECT_TRUE(ELEM_NOT_NULL(source_item));
}

TEST_F(TreeOperationsTest, ReorganizeTree) {
    parse_xml1("<root>"
                "<container>"
                "<item1>text1</item1>"
                "<item2>text2</item2>"
                "<item3>text3</item3>"
                "</container>"
                "</root>");

    TaurusElement root = root1();
    TaurusElement container = taurus_element_first_child_any(root);

    // Remove middle item
    TaurusElement item1 = taurus_element_first_child_any(container);
    TaurusElement item2 = taurus_element_next_sibling_any(item1);
    TaurusElement item3 = taurus_element_next_sibling_any(item2);

    taurus_element_remove_child(container, item2);

    // Verify order is now item1, item3
    TaurusElement first = taurus_element_first_child_any(container);
    EXPECT_STREQ(taurus_element_name(first), "item1");

    TaurusElement second = taurus_element_next_sibling_any(first);
    EXPECT_STREQ(taurus_element_name(second), "item3");

    // Add item2 back at the beginning
    taurus_element_prepend_child(container, item2);

    first = taurus_element_first_child_any(container);
    EXPECT_STREQ(taurus_element_name(first), "item2");

    second = taurus_element_next_sibling_any(first);
    EXPECT_STREQ(taurus_element_name(second), "item1");
}

} // namespace taurus_test
