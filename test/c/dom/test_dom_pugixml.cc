/**
 * Taurus DOM Modification Tests
 *
 * Tests adapted from pugixml DOM tests to verify Taurus DOM functionality
 * Covers: modification, traversal, attributes, serialization
 */

#include <gtest/gtest.h>
#include <string.h>
#include <stdlib.h>

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

// Test helper to serialize and compare
static std::string serialize_element(TaurusElement elem) {
    char* xml = taurus_element_serialize(elem, NULL);
    if (!xml) return "";
    std::string result(xml);
    free(xml);
    return result;
}

static std::string serialize_document(TaurusDocument doc) {
    char* xml = taurus_document_serialize(doc, NULL);
    if (!xml) return "";
    std::string result(xml);
    free(xml);
    return result;
}

// Test fixture
class TaurusDomTest : public ::testing::Test {
protected:
    TaurusDocument doc;

    void SetUp() override {
        TaurusStatus status;
        doc = taurus_parse_string_with_encoding("<root/>", 7, &status);
        if (!doc) {
            fprintf(stderr, "DEBUG: Failed to parse, status=%d\n", status);
        }
        ASSERT_NE(doc, nullptr);
    }

    void TearDown() override {
        if (doc) {
            taurus_document_free(doc);
        }
    }
};

// ============================================================================
// Attribute Tests
// ============================================================================

TEST_F(TaurusDomTest, SetAttribute) {
    TaurusElement root = taurus_document_root(doc);

    // Set single attribute
    TaurusStatus status = taurus_element_set_attribute(root, "attr1", "value1");
    EXPECT_EQ(status, TAURUS_OK);

    // Verify attribute value
    const char* value = taurus_element_attribute(root, "attr1");
    EXPECT_STREQ(value, "value1");

    // Update existing attribute
    status = taurus_element_set_attribute(root, "attr1", "value2");
    EXPECT_EQ(status, TAURUS_OK);
    value = taurus_element_attribute(root, "attr1");
    EXPECT_STREQ(value, "value2");

    // Set multiple attributes
    taurus_element_set_attribute(root, "attr2", "v2");
    taurus_element_set_attribute(root, "attr3", "v3");

    std::string xml = serialize_element(root);
    EXPECT_NE(xml.find("attr1=\"value2\""), std::string::npos);
    EXPECT_NE(xml.find("attr2=\"v2\""), std::string::npos);
    EXPECT_NE(xml.find("attr3=\"v3\""), std::string::npos);
}

TEST_F(TaurusDomTest, RemoveAttribute) {
    TaurusElement root = taurus_document_root(doc);

    // Set up attributes
    taurus_element_set_attribute(root, "attr1", "v1");
    taurus_element_set_attribute(root, "attr2", "v2");
    taurus_element_set_attribute(root, "attr3", "v3");

    // Remove one attribute
    TaurusStatus status = taurus_element_remove_attribute(root, "attr2");
    EXPECT_EQ(status, TAURUS_OK);

    // Verify removed
    const char* value = taurus_element_attribute(root, "attr2");
    EXPECT_EQ(value, nullptr);

    // Verify others still exist
    EXPECT_STREQ(taurus_element_attribute(root, "attr1"), "v1");
    EXPECT_STREQ(taurus_element_attribute(root, "attr3"), "v3");

    // Try removing non-existent attribute
    status = taurus_element_remove_attribute(root, "nonexistent");
    EXPECT_EQ(status, TAURUS_ERROR_NOT_FOUND);

    // Remove all attributes
    status = taurus_element_remove_all_attributes(root);
    EXPECT_EQ(status, TAURUS_OK);
    EXPECT_STREQ(taurus_element_attribute(root, "attr1"), nullptr);
    EXPECT_STREQ(taurus_element_attribute(root, "attr3"), nullptr);
}

// ============================================================================
// Element Creation Tests
// ============================================================================

TEST_F(TaurusDomTest, CreateElement) {
    TaurusElement root = taurus_document_root(doc);

    // Create new element
    TaurusElement child1 = taurus_element_create(doc, "child1");
    ASSERT_ELEM_NOT_NULL(child1);

    // Verify element name
    const char* name = taurus_element_name(child1);
    EXPECT_STREQ(name, "child1");

    // Create another element
    TaurusElement child2 = taurus_element_create(doc, "child2");
    ASSERT_ELEM_NOT_NULL(child2);

    // Append to root
    TaurusStatus status = taurus_element_append_child(root, child1);
    EXPECT_EQ(status, TAURUS_OK);

    status = taurus_element_append_child(root, child2);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify child count
    size_t count = taurus_element_child_count(root);
    EXPECT_EQ(count, 2);

    std::string xml = serialize_element(root);
    EXPECT_NE(xml.find("<child1/>"), std::string::npos);
    EXPECT_NE(xml.find("<child2/>"), std::string::npos);
}

TEST_F(TaurusDomTest, SetElementName) {
    TaurusElement root = taurus_document_root(doc);
    const char* original_name = taurus_element_name(root);
    EXPECT_STREQ(original_name, "root");

    // Rename element
    TaurusStatus status = taurus_element_set_name(root, "newroot");
    EXPECT_EQ(status, TAURUS_OK);

    const char* new_name = taurus_element_name(root);
    EXPECT_STREQ(new_name, "newroot");

    // Verify in serialization
    std::string xml = serialize_element(root);
    EXPECT_NE(xml.find("<newroot/>"), std::string::npos);
}

// ============================================================================
// Child Manipulation Tests
// ============================================================================

TEST_F(TaurusDomTest, AppendChild) {
    TaurusElement root = taurus_document_root(doc);

    // Create and append children
    TaurusElement child1 = taurus_element_create(doc, "child1");
    TaurusElement child2 = taurus_element_create(doc, "child2");
    TaurusElement child3 = taurus_element_create(doc, "child3");

    taurus_element_append_child(root, child1);
    taurus_element_append_child(root, child2);
    taurus_element_append_child(root, child3);

    // Verify order
    EXPECT_EQ(taurus_element_child_count(root), 3);
    EXPECT_STREQ(taurus_element_name(taurus_element_child(root, 0)), "child1");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(root, 1)), "child2");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(root, 2)), "child3");
}

TEST_F(TaurusDomTest, PrependChild) {
    TaurusElement root = taurus_document_root(doc);

    // Create initial child
    TaurusElement child1 = taurus_element_create(doc, "child1");
    taurus_element_append_child(root, child1);

    // Prepend new child
    TaurusElement child0 = taurus_element_create(doc, "child0");
    TaurusStatus status = taurus_element_prepend_child(root, child0);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify order
    EXPECT_EQ(taurus_element_child_count(root), 2);
    EXPECT_STREQ(taurus_element_name(taurus_element_child(root, 0)), "child0");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(root, 1)), "child1");
}

TEST_F(TaurusDomTest, InsertBefore) {
    TaurusElement root = taurus_document_root(doc);

    // Create children
    TaurusElement child1 = taurus_element_create(doc, "child1");
    TaurusElement child2 = taurus_element_create(doc, "child2");
    TaurusElement child3 = taurus_element_create(doc, "child3");

    taurus_element_append_child(root, child1);
    taurus_element_append_child(root, child2);

    // Insert child3 before child2
    TaurusStatus status = taurus_element_insert_before(child2, child3);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify order
    EXPECT_EQ(taurus_element_child_count(root), 3);
    EXPECT_STREQ(taurus_element_name(taurus_element_child(root, 0)), "child1");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(root, 1)), "child3");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(root, 2)), "child2");
}

TEST_F(TaurusDomTest, InsertAfter) {
    TaurusElement root = taurus_document_root(doc);

    // Create children
    TaurusElement child1 = taurus_element_create(doc, "child1");
    TaurusElement child2 = taurus_element_create(doc, "child2");
    TaurusElement child3 = taurus_element_create(doc, "child3");

    taurus_element_append_child(root, child1);
    taurus_element_append_child(root, child2);

    // Insert child3 after child1
    TaurusStatus status = taurus_element_insert_after(child1, child3);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify order
    EXPECT_EQ(taurus_element_child_count(root), 3);
    EXPECT_STREQ(taurus_element_name(taurus_element_child(root, 0)), "child1");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(root, 1)), "child3");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(root, 2)), "child2");
}

TEST_F(TaurusDomTest, RemoveChild) {
    TaurusElement root = taurus_document_root(doc);

    // Create children
    TaurusElement child1 = taurus_element_create(doc, "child1");
    TaurusElement child2 = taurus_element_create(doc, "child2");
    TaurusElement child3 = taurus_element_create(doc, "child3");

    taurus_element_append_child(root, child1);
    taurus_element_append_child(root, child2);
    taurus_element_append_child(root, child3);

    // Remove middle child
    TaurusStatus status = taurus_element_remove_child(root, child2);
    EXPECT_EQ(status, TAURUS_OK);

    // Verify
    EXPECT_EQ(taurus_element_child_count(root), 2);
    EXPECT_STREQ(taurus_element_name(taurus_element_child(root, 0)), "child1");
    EXPECT_STREQ(taurus_element_name(taurus_element_child(root, 1)), "child3");

    // Verify parent of removed child is NULL
    EXPECT_ELEM_RETURN_NULL(taurus_element_parent(child2));
}

// ============================================================================
// Text Content Tests
// ============================================================================

TEST_F(TaurusDomTest, SetText) {
    TaurusElement root = taurus_document_root(doc);

    // Set text content
    TaurusStatus status = taurus_element_set_text(root, "Hello World");
    EXPECT_EQ(status, TAURUS_OK);

    // Get text content
    const char* text = taurus_element_text(root);
    EXPECT_STREQ(text, "Hello World");

    // Verify in serialization
    std::string xml = serialize_element(root);
    EXPECT_NE(xml.find("Hello World"), std::string::npos);
}

TEST_F(TaurusDomTest, TextWithChildren) {
    TaurusElement root = taurus_document_root(doc);

    // Set text and add children
    taurus_element_set_text(root, "parent text");

    TaurusElement child = taurus_element_create(doc, "child");
    taurus_element_set_text(child, "child text");
    taurus_element_append_child(root, child);

    // Parent text includes all descendant text (XML spec behavior)
    const char* parent_text = taurus_element_text(root);
    EXPECT_STREQ(parent_text, "parent textchild text");

    const char* child_text = taurus_element_text(child);
    EXPECT_STREQ(child_text, "child text");
}

// ============================================================================
// Parent Tests
// ============================================================================

TEST_F(TaurusDomTest, ParentNavigation) {
    TaurusElement root = taurus_document_root(doc);

    // Root should have no parent
    EXPECT_ELEM_RETURN_NULL(taurus_element_parent(root));

    // Create child
    TaurusElement child = taurus_element_create(doc, "child");
    taurus_element_append_child(root, child);

    // Child's parent should be root
    TaurusElement parent = taurus_element_parent(child);
    EXPECT_ELEM_EQ(parent, root);

    // Create grandchild
    TaurusElement grandchild = taurus_element_create(doc, "grandchild");
    taurus_element_append_child(child, grandchild);

    // Grandchild's parent should be child
    parent = taurus_element_parent(grandchild);
    EXPECT_ELEM_EQ(parent, child);
}

// ============================================================================
// Serialization Tests
// ============================================================================

TEST_F(TaurusDomTest, SerializeElement) {
    TaurusElement root = taurus_document_root(doc);

    taurus_element_set_attribute(root, "attr1", "value1");
    taurus_element_set_text(root, "text content");

    TaurusElement child = taurus_element_create(doc, "child");
    taurus_element_set_attribute(child, "attr2", "value2");
    taurus_element_append_child(root, child);

    std::string xml = serialize_element(root);

    // Verify content
    EXPECT_NE(xml.find("<root"), std::string::npos);
    EXPECT_NE(xml.find("attr1=\"value1\""), std::string::npos);
    EXPECT_NE(xml.find("text content"), std::string::npos);
    EXPECT_NE(xml.find("<child"), std::string::npos);
    EXPECT_NE(xml.find("attr2=\"value2\""), std::string::npos);
}

TEST_F(TaurusDomTest, SerializeDocument) {
    TaurusElement root = taurus_document_root(doc);

    taurus_element_set_attribute(root, "version", "1.0");

    TaurusElement child = taurus_element_create(doc, "item");
    taurus_element_set_text(child, "data");
    taurus_element_append_child(root, child);

    std::string xml = serialize_document(doc);

    // Verify content (note: taurus_document_serialize may not include XML declaration)
    EXPECT_NE(xml.find("<root"), std::string::npos);
    EXPECT_NE(xml.find("version=\"1.0\""), std::string::npos);
    EXPECT_NE(xml.find("<item>data</item>"), std::string::npos);
}

// ============================================================================
// Complex Document Tests
// ============================================================================

TEST(TaurusDomComplex, BuildComplexDocument) {
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string_with_encoding("<root/>", 7, &status);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);

    // Build complex structure
    TaurusElement section1 = taurus_element_create(doc, "section");
    taurus_element_set_attribute(section1, "id", "1");
    taurus_element_set_attribute(section1, "title", "First Section");

    TaurusElement para1 = taurus_element_create(doc, "para");
    taurus_element_set_text(para1, "This is paragraph 1");
    taurus_element_append_child(section1, para1);

    TaurusElement para2 = taurus_element_create(doc, "para");
    taurus_element_set_text(para2, "This is paragraph 2");
    taurus_element_append_child(section1, para2);

    taurus_element_append_child(root, section1);

    TaurusElement section2 = taurus_element_create(doc, "section");
    taurus_element_set_attribute(section2, "id", "2");
    taurus_element_set_attribute(section2, "title", "Second Section");

    TaurusElement para3 = taurus_element_create(doc, "para");
    taurus_element_set_text(para3, "This is paragraph 3");
    taurus_element_append_child(section2, para3);

    taurus_element_append_child(root, section2);

    // Verify structure
    EXPECT_EQ(taurus_element_child_count(root), 2);

    TaurusElement s1 = taurus_element_child(root, 0);
    EXPECT_STREQ(taurus_element_name(s1), "section");
    EXPECT_STREQ(taurus_element_attribute(s1, "id"), "1");
    EXPECT_EQ(taurus_element_child_count(s1), 2);

    TaurusElement s2 = taurus_element_child(root, 1);
    EXPECT_STREQ(taurus_element_name(s2), "section");
    EXPECT_STREQ(taurus_element_attribute(s2, "id"), "2");
    EXPECT_EQ(taurus_element_child_count(s2), 1);

    // Serialize and verify
    char* xml = taurus_document_serialize(doc, NULL);
    ASSERT_NE(xml, nullptr);

    std::string xml_str(xml);
    EXPECT_NE(xml_str.find("<section"), std::string::npos);
    EXPECT_NE(xml_str.find("id=\"1\""), std::string::npos);
    EXPECT_NE(xml_str.find("This is paragraph 1"), std::string::npos);
    EXPECT_NE(xml_str.find("This is paragraph 3"), std::string::npos);

    free(xml);
    taurus_document_free(doc);
}

// ============================================================================
// Performance Tests (Basic)
// ============================================================================

TEST(TaurusDomPerformance, CreateManyElements) {
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string_with_encoding("<root/>", 7, &status);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);

    // Create 1000 elements
    for (int i = 0; i < 1000; i++) {
        char name[32];
        snprintf(name, sizeof(name), "element%d", i);
        TaurusElement elem = taurus_element_create(doc, name);
        ASSERT_ELEM_NOT_NULL(elem);
        taurus_element_append_child(root, elem);
    }

    EXPECT_EQ(taurus_element_child_count(root), 1000);

    taurus_document_free(doc);
}

TEST(TaurusDomPerformance, CreateManyAttributes) {
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string_with_encoding("<root/>", 7, &status);
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

    // Verify some attributes
    EXPECT_STREQ(taurus_element_attribute(root, "attr0"), "value0");
    EXPECT_STREQ(taurus_element_attribute(root, "attr50"), "value50");
    EXPECT_STREQ(taurus_element_attribute(root, "attr99"), "value99");

    taurus_document_free(doc);
}
