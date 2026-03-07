/* test_api_extensions.cpp - Tests for pugixml API compatibility extensions
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for new pugixml-compatible API functions:
 * - taurus_element_root()
 * - taurus_element_child_value()
 * - taurus_element_remove_children()
 * - taurus_element_hash_value()
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
 * Base class for API extension tests
 */
class ApiExtensionsTest : public ::testing::Test {
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
 * taurus_element_root() Tests
 * ============================================================================ */

TEST_F(ApiExtensionsTest, RootFromDocumentRoot) {
    parse_xml("<node><child>text</child></node>");

    TaurusElement root_elem = taurus_element_root(root);
    ASSERT_TRUE(ELEM_NOT_NULL(root_elem));
    // Root's root is itself - compare names
    EXPECT_STREQ(taurus_element_name(root_elem), taurus_element_name(root));
}

TEST_F(ApiExtensionsTest, RootFromNestedElement) {
    parse_xml("<root><level1><level2>deep</level2></level1></root>");

    TaurusElement level1 = taurus_element_find_child(root, "level1");
    ASSERT_TRUE(ELEM_NOT_NULL(level1));

    TaurusElement level2 = taurus_element_find_child(level1, "level2");
    ASSERT_TRUE(ELEM_NOT_NULL(level2));

    TaurusElement root_elem = taurus_element_root(level2);
    ASSERT_TRUE(ELEM_NOT_NULL(root_elem));
    EXPECT_STREQ(taurus_element_name(root_elem), "root");
}

TEST_F(ApiExtensionsTest, RootFromNullElement) {
    TaurusElement null_elem = ELEM_NULL();
    TaurusElement root_elem = taurus_element_root(null_elem);
    EXPECT_TRUE(ELEM_IS_NULL(root_elem));
}

/* ============================================================================
 * taurus_element_child_value() Tests
 * ============================================================================ */

TEST_F(ApiExtensionsTest, ChildValueWithText) {
    parse_xml("<node>text content</node>");

    const char* value = taurus_element_child_value(root);
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "text content");
}

TEST_F(ApiExtensionsTest, ChildValueWithElementChild) {
    parse_xml("<node><child>inner text</child></node>");

    const char* value = taurus_element_child_value(root);
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "inner text");
}

TEST_F(ApiExtensionsTest, ChildValueWithCDATA) {
    parse_xml("<node><![CDATA[cdata content]]></node>");

    const char* value = taurus_element_child_value(root);
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "cdata content");
}

TEST_F(ApiExtensionsTest, ChildValueFromEmptyElement) {
    parse_xml("<node/>");

    const char* value = taurus_element_child_value(root);
    EXPECT_EQ(value, nullptr);
}

TEST_F(ApiExtensionsTest, ChildValueFromNullElement) {
    TaurusElement null_elem = ELEM_NULL();
    const char* value = taurus_element_child_value(null_elem);
    EXPECT_EQ(value, nullptr);
}

/* ============================================================================
 * taurus_element_remove_children() Tests
 * ============================================================================ */

TEST_F(ApiExtensionsTest, RemoveChildrenAll) {
    parse_xml("<node><n1/><n2/><n3/></node>");

    EXPECT_EQ(taurus_element_remove_children(root), TAURUS_OK);
    EXPECT_EQ(taurus_element_child_count(root), 0);
    EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_element_first_child(root, nullptr)));
}

TEST_F(ApiExtensionsTest, RemoveChildrenWithText) {
    parse_xml("<node>text1<n1/>text2<n2/>text3</node>");

    EXPECT_EQ(taurus_element_remove_children(root), TAURUS_OK);
    EXPECT_EQ(taurus_element_child_count(root), 0);
    EXPECT_STREQ(taurus_element_text(root), "");
}

TEST_F(ApiExtensionsTest, RemoveChildrenEmptyElement) {
    parse_xml("<node/>");

    EXPECT_EQ(taurus_element_remove_children(root), TAURUS_OK);
    EXPECT_EQ(taurus_element_child_count(root), 0);
}

TEST_F(ApiExtensionsTest, RemoveChildrenNullElement) {
    TaurusElement null_elem = ELEM_NULL();
    TaurusStatus status = taurus_element_remove_children(null_elem);
    EXPECT_EQ(status, TAURUS_ERROR_NULL_ARG);
}

TEST_F(ApiExtensionsTest, RemoveChildrenAndAddNew) {
    parse_xml("<node><old1/><old2/></node>");

    EXPECT_EQ(taurus_element_remove_children(root), TAURUS_OK);
    EXPECT_EQ(taurus_element_child_count(root), 0);

    TaurusElement new_child = taurus_element_create(doc, "new");
    ASSERT_TRUE(ELEM_NOT_NULL(new_child));
    EXPECT_EQ(taurus_element_append_child(root, new_child), TAURUS_OK);
    EXPECT_EQ(taurus_element_child_count(root), 1);
    EXPECT_STREQ(taurus_element_name(taurus_element_first_child(root, nullptr)), "new");
}

/* ============================================================================
 * taurus_element_hash_value() Tests
 * ============================================================================ */

TEST_F(ApiExtensionsTest, HashValueNotNullElement) {
    parse_xml("<node/>");

    size_t hash = taurus_element_hash_value(root);
    EXPECT_NE(hash, 0);
}

TEST_F(ApiExtensionsTest, HashValueNullElement) {
    TaurusElement null_elem = ELEM_NULL();
    size_t hash = taurus_element_hash_value(null_elem);
    EXPECT_EQ(hash, 0);
}

TEST_F(ApiExtensionsTest, HashValueDifferentElements) {
    parse_xml("<node><n1/><n2/></node>");

    TaurusElement n1 = taurus_element_find_child(root, "n1");
    TaurusElement n2 = taurus_element_find_child(root, "n2");

    ASSERT_TRUE(ELEM_NOT_NULL(n1));
    ASSERT_TRUE(ELEM_NOT_NULL(n2));

    size_t hash1 = taurus_element_hash_value(n1);
    size_t hash2 = taurus_element_hash_value(n2);

    // Different elements should have different hash values
    EXPECT_NE(hash1, hash2);
}

TEST_F(ApiExtensionsTest, HashValueSameElementConsistent) {
    parse_xml("<node/>");

    size_t hash1 = taurus_element_hash_value(root);
    size_t hash2 = taurus_element_hash_value(root);

    // Same element should consistently return same hash
    EXPECT_EQ(hash1, hash2);
}

/* ============================================================================
 * Combined Tests
 * ============================================================================ */

TEST_F(ApiExtensionsTest, RemoveChildrenThenChildValue) {
    parse_xml("<node><child>text</child></node>");

    // First get the child value
    const char* value_before = taurus_element_child_value(root);
    ASSERT_NE(value_before, nullptr);
    EXPECT_STREQ(value_before, "text");

    // Remove all children
    EXPECT_EQ(taurus_element_remove_children(root), TAURUS_OK);

    // Now child_value should return NULL
    const char* value_after = taurus_element_child_value(root);
    EXPECT_EQ(value_after, nullptr);
}

TEST_F(ApiExtensionsTest, RootChildValueChain) {
    parse_xml("<root><parent><child>deep text</child></parent></root>");

    TaurusElement parent = taurus_element_find_child(root, "parent");
    ASSERT_TRUE(ELEM_NOT_NULL(parent));

    TaurusElement child = taurus_element_find_child(parent, "child");
    ASSERT_TRUE(ELEM_NOT_NULL(child));

    // Get root from child
    TaurusElement root_elem = taurus_element_root(child);
    ASSERT_TRUE(ELEM_NOT_NULL(root_elem));
    EXPECT_STREQ(taurus_element_name(root_elem), "root");

    // Get child_value from parent (should get child's text)
    const char* value = taurus_element_child_value(parent);
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "deep text");
}

} // namespace taurus_test
