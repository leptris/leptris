/**
 * Taurus DOM Tests - Adapted from pugixml test_dom_modify.cpp
 *
 * This file imports critical DOM modification tests from pugixml to verify
 * Taurus DOM functionality is complete and correct.
 *
 * pugixml test_dom_modify.cpp covers:
 * - Attribute assignment (string, int, long, double, bool)
 * - Attribute name/value modification
 * - Element name changes
 * - Child append/prepend/insert/remove
 * - Copy operations
 *
 * Status: IN PROGRESS - Importing 50 critical tests
 */

#include <gtest/gtest.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern "C" {
#include "taurus.h"
}

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

// Helper: Serialize element and return as std::string
static std::string serialize_elem(TaurusElement elem) {
    char* xml = taurus_element_serialize(elem, NULL);
    if (!xml) return "";
    std::string result(xml);
    free(xml);
    return result;
}

// Helper: Serialize document and return as std::string
static std::string serialize_doc(TaurusDocument doc) {
    char* xml = taurus_document_serialize(doc, NULL);
    if (!xml) return "";
    std::string result(xml);
    free(xml);
    return result;
}

// Helper: Create document from XML string
static TaurusDocument create_doc(const char* xml) {
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string_with_encoding(xml, strlen(xml), &status);
    return doc;
}

// ============================================================================
// Attribute Assignment Tests
// These tests verify attribute assignment with various data types
// ============================================================================

TEST(TaurusDomModify, AttrAssign_String) {
    TaurusDocument doc = create_doc("<node/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Set attributes with string values
    taurus_element_set_attribute(root, "attr1", "v1");
    taurus_element_set_attribute(root, "attr2", "value2");
    taurus_element_set_attribute(root, "attr9", "v2");
    
    std::string xml = serialize_elem(root);
    EXPECT_NE(xml.find("attr1=\"v1\""), std::string::npos);
    EXPECT_NE(xml.find("attr2=\"value2\""), std::string::npos);
    EXPECT_NE(xml.find("attr9=\"v2\""), std::string::npos);
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, AttrAssign_Integer) {
    TaurusDocument doc = create_doc("<node/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Set attributes with integer values
    taurus_element_set_attribute(root, "attr1", "-2147483647");
    taurus_element_set_attribute(root, "attr2", "-2147483648");
    taurus_element_set_attribute(root, "attr4", "4294967295");
    taurus_element_set_attribute(root, "attr5", "4294967294");
    
    std::string xml = serialize_elem(root);
    EXPECT_NE(xml.find("attr1=\"-2147483647\""), std::string::npos);
    EXPECT_NE(xml.find("attr2=\"-2147483648\""), std::string::npos);
    EXPECT_NE(xml.find("attr4=\"4294967295\""), std::string::npos);
    EXPECT_NE(xml.find("attr5=\"4294967294\""), std::string::npos);
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, AttrAssign_Float) {
    TaurusDocument doc = create_doc("<node/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Set attributes with floating-point values
    taurus_element_set_attribute(root, "attr1", "0.5");
    taurus_element_set_attribute(root, "attr2", "0.25");
    
    std::string xml = serialize_elem(root);
    EXPECT_NE(xml.find("attr1=\"0.5\""), std::string::npos);
    EXPECT_NE(xml.find("attr2=\"0.25\""), std::string::npos);
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, AttrAssign_Boolean) {
    TaurusDocument doc = create_doc("<node/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Set attribute with boolean value
    taurus_element_set_attribute(root, "attr1", "true");
    
    std::string xml = serialize_elem(root);
    EXPECT_NE(xml.find("attr1=\"true\""), std::string::npos);
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, AttrSetValue) {
    TaurusDocument doc = create_doc("<node/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Create and modify attributes
    taurus_element_set_attribute(root, "attr1", "initial");
    taurus_element_set_attribute(root, "attr1", "updated");
    
    // Verify only the updated value exists
    std::string xml = serialize_elem(root);
    EXPECT_NE(xml.find("attr1=\"updated\""), std::string::npos);
    // Should not have both values
    EXPECT_EQ(xml.find("initial"), std::string::npos);
    
    taurus_document_free(doc);
}

// ============================================================================
// Attribute Removal Tests
// ============================================================================

TEST(TaurusDomModify, AttrRemove) {
    TaurusDocument doc = create_doc("<node attr1='v1' attr2='v2' attr3='v3'/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Remove middle attribute
    TaurusStatus status = taurus_element_remove_attribute(root, "attr2");
    EXPECT_EQ(status, TAURUS_OK);
    
    // Verify removed
    EXPECT_EQ(taurus_element_attribute(root, "attr2"), nullptr);
    
    // Verify others still exist
    EXPECT_STREQ(taurus_element_attribute(root, "attr1"), "v1");
    EXPECT_STREQ(taurus_element_attribute(root, "attr3"), "v3");
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, AttrRemove_NotFound) {
    TaurusDocument doc = create_doc("<node/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Try to remove non-existent attribute
    TaurusStatus status = taurus_element_remove_attribute(root, "nonexistent");
    EXPECT_EQ(status, TAURUS_ERROR_NOT_FOUND);
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, AttrRemoveAll) {
    TaurusDocument doc = create_doc("<node attr1='v1' attr2='v2' attr3='v3'/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Remove all attributes
    TaurusStatus status = taurus_element_remove_all_attributes(root);
    EXPECT_EQ(status, TAURUS_OK);
    
    // Verify all removed
    EXPECT_EQ(taurus_element_attribute(root, "attr1"), nullptr);
    EXPECT_EQ(taurus_element_attribute(root, "attr2"), nullptr);
    EXPECT_EQ(taurus_element_attribute(root, "attr3"), nullptr);
    
    taurus_document_free(doc);
}

// ============================================================================
// Element Name Tests
// ============================================================================

TEST(TaurusDomModify, SetElementName) {
    TaurusDocument doc = create_doc("<root/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Rename element
    TaurusStatus status = taurus_element_set_name(root, "newnode");
    EXPECT_EQ(status, TAURUS_OK);
    
    // Verify name changed
    EXPECT_STREQ(taurus_element_name(root), "newnode");
    
    // Verify in serialization
    std::string xml = serialize_elem(root);
    EXPECT_NE(xml.find("<newnode"), std::string::npos);
    EXPECT_EQ(xml.find("<root"), std::string::npos);
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, SetElementNameWithAttribute) {
    TaurusDocument doc = create_doc("<node attr='value'/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Rename element with attribute
    TaurusStatus status = taurus_element_set_name(root, "newnode");
    EXPECT_EQ(status, TAURUS_OK);
    
    // Verify name changed, attribute preserved
    std::string xml = serialize_elem(root);
    EXPECT_NE(xml.find("<newnode"), std::string::npos);
    EXPECT_NE(xml.find("attr=\"value\""), std::string::npos);
    
    taurus_document_free(doc);
}

// ============================================================================
// Child Append Tests
// ============================================================================

TEST(TaurusDomModify, AppendChild) {
    TaurusDocument doc = create_doc("<parent/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement parent = taurus_document_root(doc);
    
    // Create and append children
    TaurusElement child1 = taurus_element_create(doc, "child1");
    TaurusElement child2 = taurus_element_create(doc, "child2");
    
    TaurusStatus status = taurus_element_append_child(parent, child1);
    EXPECT_EQ(status, TAURUS_OK);
    
    status = taurus_element_append_child(parent, child2);
    EXPECT_EQ(status, TAURUS_OK);
    
    // Verify children
    EXPECT_EQ(taurus_element_child_count(parent), 2);
    EXPECT_STREQ(taurus_element_name(taurus_element_child(parent, 0)), "child1");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(parent, 1)), "child2");
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, PrependChild) {
    TaurusDocument doc = create_doc("<parent><child1/></parent>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement parent = taurus_document_root(doc);
    TaurusElement child1 = taurus_element_child(parent, 0);
    
    // Prepend new child
    TaurusElement child0 = taurus_element_create(doc, "child0");
    TaurusStatus status = taurus_element_prepend_child(parent, child0);
    EXPECT_EQ(status, TAURUS_OK);
    
    // Verify order: child0 should be first
    EXPECT_EQ(taurus_element_child_count(parent), 2);
    EXPECT_STREQ(taurus_element_name(taurus_element_child(parent, 0)), "child0");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(parent, 1)), "child1");
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, InsertBefore) {
    TaurusDocument doc = create_doc("<parent><child1/><child2/></parent>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement parent = taurus_document_root(doc);
    TaurusElement child2 = taurus_element_child(parent, 1);
    
    // Insert new child before child2
    TaurusElement child1_5 = taurus_element_create(doc, "child1.5");
    TaurusStatus status = taurus_element_insert_before(child2, child1_5);
    EXPECT_EQ(status, TAURUS_OK);
    
    // Verify order
    EXPECT_EQ(taurus_element_child_count(parent), 3);
    EXPECT_STREQ(taurus_element_name(taurus_element_child(parent, 0)), "child1");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(parent, 1)), "child1.5");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(parent, 2)), "child2");
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, InsertAfter) {
    TaurusDocument doc = create_doc("<parent><child1/><child2/></parent>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement parent = taurus_document_root(doc);
    TaurusElement child1 = taurus_element_child(parent, 0);
    
    // Insert new child after child1
    TaurusElement child1_5 = taurus_element_create(doc, "child1.5");
    TaurusStatus status = taurus_element_insert_after(child1, child1_5);
    EXPECT_EQ(status, TAURUS_OK);
    
    // Verify order
    EXPECT_EQ(taurus_element_child_count(parent), 3);
    EXPECT_STREQ(taurus_element_name(taurus_element_child(parent, 0)), "child1");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(parent, 1)), "child1.5");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(parent, 2)), "child2");
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, RemoveChild) {
    TaurusDocument doc = create_doc("<parent><child1/><child2/><child3/></parent>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement parent = taurus_document_root(doc);
    TaurusElement child2 = taurus_element_child(parent, 1);
    
    // Remove middle child
    TaurusStatus status = taurus_element_remove_child(parent, child2);
    EXPECT_EQ(status, TAURUS_OK);
    
    // Verify
    EXPECT_EQ(taurus_element_child_count(parent), 2);
    EXPECT_STREQ(taurus_element_name(taurus_element_child(parent, 0)), "child1");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(parent, 1)), "child3");
    
    // Verify removed child has no parent
    EXPECT_ELEM_RETURN_NULL(taurus_element_parent(child2));
    
    taurus_document_free(doc);
}

// ============================================================================
// Tree Navigation Tests
// ============================================================================

TEST(TaurusDomModify, ParentNavigation) {
    TaurusDocument doc = create_doc("<root><child><grandchild/></child></root>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_child(root, 0);
    TaurusElement grandchild = taurus_element_child(child, 0);
    
    // Verify parent relationships
    EXPECT_ELEM_RETURN_NULL(taurus_element_parent(root));
    EXPECT_ELEM_EQ(taurus_element_parent(child), root);
    EXPECT_ELEM_EQ(taurus_element_parent(grandchild), child);
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, SiblingNavigation) {
    TaurusDocument doc = create_doc("<parent><child1/><child2/><child3/></parent>");
    ASSERT_NE(doc, nullptr);

    TaurusElement parent = taurus_document_root(doc);
    TaurusElement child1 = taurus_element_child(parent, 0);
    TaurusElement child2 = taurus_element_child(parent, 1);
    TaurusElement child3 = taurus_element_child(parent, 2);

    // Verify next sibling
    EXPECT_ELEM_EQ(taurus_element_next_sibling(child1, NULL), child2);
    EXPECT_ELEM_EQ(taurus_element_next_sibling(child2, NULL), child3);
    EXPECT_ELEM_RETURN_NULL(taurus_element_next_sibling(child3, NULL));

    // Verify previous sibling
    EXPECT_ELEM_EQ(taurus_element_previous_sibling(child2, NULL), child1);
    EXPECT_ELEM_EQ(taurus_element_previous_sibling(child3, NULL), child2);
    EXPECT_ELEM_RETURN_NULL(taurus_element_previous_sibling(child1, NULL));

    taurus_document_free(doc);
}

TEST(TaurusDomModify, FindChildByName) {
    TaurusDocument doc = create_doc("<parent><a/><b/><a/></parent>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement parent = taurus_document_root(doc);
    
    // Find first child with name "a"
    TaurusElement child_a = taurus_element_find_child(parent, "a");
    ASSERT_ELEM_NOT_NULL(child_a);
    EXPECT_STREQ(taurus_element_name(child_a), "a");
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, FirstLastChild) {
    TaurusDocument doc = create_doc("<parent><first/><middle/><last/></parent>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement parent = taurus_document_root(doc);
    
    // First child
    TaurusElement first = taurus_element_first_child(parent, NULL);
    ASSERT_ELEM_NOT_NULL(first);
    EXPECT_STREQ(taurus_element_name(first), "first");
    
    // Last child
    TaurusElement last = taurus_element_last_child(parent, NULL);
    ASSERT_ELEM_NOT_NULL(last);
    EXPECT_STREQ(taurus_element_name(last), "last");
    
    taurus_document_free(doc);
}

// ============================================================================
// Text Content Tests
// ============================================================================

TEST(TaurusDomModify, SetText) {
    TaurusDocument doc = create_doc("<node/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Set text content
    TaurusStatus status = taurus_element_set_text(root, "Hello World");
    EXPECT_EQ(status, TAURUS_OK);
    
    // Verify
    EXPECT_STREQ(taurus_element_text(root), "Hello World");
    
    std::string xml = serialize_elem(root);
    EXPECT_NE(xml.find("Hello World"), std::string::npos);
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, SetTextOnElementWithChildren) {
    TaurusDocument doc = create_doc("<node><child/></node>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_child(root, 0);
    
    // Set text on both
    taurus_element_set_text(root, "parent");
    taurus_element_set_text(child, "child");
    
    // Verify
    EXPECT_STREQ(taurus_element_text(root), "parent");
    EXPECT_STREQ(taurus_element_text(child), "child");
    
    taurus_document_free(doc);
}

// ============================================================================
// Complex Document Building Tests
// ============================================================================

TEST(TaurusDomModify, BuildComplexDocument) {
    TaurusDocument doc = create_doc("<root/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Build: <root><section id="1"><para>text1</para><para>text2</para></section></root>
    TaurusElement section = taurus_element_create(doc, "section");
    taurus_element_set_attribute(section, "id", "1");
    taurus_element_append_child(root, section);
    
    TaurusElement para1 = taurus_element_create(doc, "para");
    taurus_element_set_text(para1, "text1");
    taurus_element_append_child(section, para1);
    
    TaurusElement para2 = taurus_element_create(doc, "para");
    taurus_element_set_text(para2, "text2");
    taurus_element_append_child(section, para2);
    
    // Verify structure
    EXPECT_EQ(taurus_element_child_count(root), 1);
    EXPECT_EQ(taurus_element_child_count(section), 2);
    
    EXPECT_STREQ(taurus_element_attribute(section, "id"), "1");
    EXPECT_STREQ(taurus_element_text(para1), "text1");
    EXPECT_STREQ(taurus_element_text(para2), "text2");
    
    std::string xml = serialize_doc(doc);
    EXPECT_NE(xml.find("<section"), std::string::npos);
    EXPECT_NE(xml.find("id=\"1\""), std::string::npos);
    EXPECT_NE(xml.find("text1"), std::string::npos);
    EXPECT_NE(xml.find("text2"), std::string::npos);
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, ModifyExistingDocument) {
    TaurusDocument doc = create_doc("<root><child attr='value'/></root>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_child(root, 0);
    
    // Modify existing element
    TaurusStatus status = taurus_element_set_name(child, "modified");
    EXPECT_EQ(status, TAURUS_OK);
    
    taurus_element_set_attribute(child, "newattr", "newvalue");
    
    // Verify
    std::string xml = serialize_elem(root);
    EXPECT_NE(xml.find("<modified"), std::string::npos);
    EXPECT_NE(xml.find("newattr=\"newvalue\""), std::string::npos);
    EXPECT_NE(xml.find("attr=\"value\""), std::string::npos); // Original attr still there
    
    taurus_document_free(doc);
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST(TaurusDomModify, CreateNullName) {
    TaurusDocument doc = create_doc("<root/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Try to create element with NULL name - should handle gracefully
    TaurusElement elem = taurus_element_create(doc, "");
    // Empty name might be allowed or might fail depending on implementation
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, ModifyNullElement) {
    TaurusStatus status;
    TaurusElement null_elem = taurus_element_handle_null();

    // Try operations on NULL element - should return error
    status = taurus_element_set_name(null_elem, "test");
    EXPECT_NE(status, TAURUS_OK);

    status = taurus_element_set_attribute(null_elem, "attr", "value");
    EXPECT_NE(status, TAURUS_OK);

    status = taurus_element_set_text(null_elem, "text");
    EXPECT_NE(status, TAURUS_OK);
}

TEST(TaurusDomModify, RemoveNonExistentChild) {
    TaurusDocument doc = create_doc("<root><child/></root>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Create a child that's not in the document
    TaurusElement orphan = taurus_element_create(doc, "orphan");
    
    // Try to remove it - should fail
    TaurusStatus status = taurus_element_remove_child(root, orphan);
    EXPECT_NE(status, TAURUS_OK);
    
    taurus_document_free(doc);
}

// ============================================================================
// Performance Tests (Basic)
// ============================================================================

TEST(TaurusDomModify, CreateManyChildren) {
    TaurusDocument doc = create_doc("<root/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Create 100 children
    for (int i = 0; i < 100; i++) {
        char name[32];
        snprintf(name, sizeof(name), "child%d", i);
        TaurusElement child = taurus_element_create(doc, name);
        ASSERT_ELEM_NOT_NULL(child);
        taurus_element_append_child(root, child);
    }
    
    EXPECT_EQ(taurus_element_child_count(root), 100);
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, CreateManyAttributes) {
    TaurusDocument doc = create_doc("<node/>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    // Create 100 attributes
    for (int i = 0; i < 100; i++) {
        char name[32], value[32];
        snprintf(name, sizeof(name), "attr%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        TaurusStatus status = taurus_element_set_attribute(root, name, value);
        EXPECT_EQ(status, TAURUS_OK);
    }
    
    // Verify some
    EXPECT_STREQ(taurus_element_attribute(root, "attr0"), "value0");
    EXPECT_STREQ(taurus_element_attribute(root, "attr50"), "value50");
    EXPECT_STREQ(taurus_element_attribute(root, "attr99"), "value99");
    
    taurus_document_free(doc);
}

// ============================================================================
// Serialization Tests
// ============================================================================

TEST(TaurusDomModify, SerializeElement) {
    TaurusDocument doc = create_doc("<root attr='value'><child>text</child></root>");
    ASSERT_NE(doc, nullptr);
    
    TaurusElement root = taurus_document_root(doc);
    
    std::string xml = serialize_elem(root);
    
    // Verify key elements
    EXPECT_NE(xml.find("<root"), std::string::npos);
    EXPECT_NE(xml.find("attr=\"value\""), std::string::npos);
    EXPECT_NE(xml.find("<child"), std::string::npos);
    EXPECT_NE(xml.find("text"), std::string::npos);
    
    taurus_document_free(doc);
}

TEST(TaurusDomModify, SerializeDocument) {
    TaurusDocument doc = create_doc("<root><child/></root>");
    ASSERT_NE(doc, nullptr);
    
    std::string xml = serialize_doc(doc);
    
    // Document serialization should include XML structure
    EXPECT_NE(xml.find("<root"), std::string::npos);
    EXPECT_NE(xml.find("<child"), std::string::npos);
    
    taurus_document_free(doc);
}
