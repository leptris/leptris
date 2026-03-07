/* test_simplified_api.cpp - Tests for simplified quick-start API
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for Phase 5 simplified API functions:
 * - taurus_parse()
 * - taurus_root()
 * - taurus_child()
 * - taurus_attr()
 * - taurus_text()
 * - taurus_free()
 */

#include <gtest/gtest.h>
#include <string>
#include <cstring>
#include "../../src/include/taurus.h"

/* TaurusElement handle API macros */
#define ELEM_IS_NULL(e) taurus_element_is_null((e))
#define ELEM_NOT_NULL(e) !taurus_element_is_null((e))
#define ELEM_NULL() taurus_element_handle_null()

namespace taurus_test {

// Helper macro to get length of string literal
#define PARSE(xml) taurus_parse(xml, strlen(xml))

/**
 * Base class for simplified API tests
 */
class SimplifiedApiTest : public ::testing::Test {
protected:
    TaurusDocument doc;
    std::string xml_buffer;

    void SetUp() override {
        doc = nullptr;
    }

    void TearDown() override {
        if (doc) {
            taurus_document_free(doc);
            doc = nullptr;
        }
        xml_buffer.clear();
    }
};

/* ============================================================================
 * taurus_parse() Tests - Simplified parsing
 * ============================================================================ */

TEST_F(SimplifiedApiTest, ParseSimpleXml) {
    const char* xml = "<root><child>text</child></root>";
    doc = PARSE(xml);

    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));
    EXPECT_STREQ(taurus_element_name(root), "root");
}

TEST_F(SimplifiedApiTest, ParseWithExplicitLength) {
    const char* xml = "<root><child>text</child></root>";
    size_t len = strlen(xml);
    doc = taurus_parse(xml, len);

    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));
    EXPECT_STREQ(taurus_element_name(root), "root");
}

TEST_F(SimplifiedApiTest, ParseInvalidXmlReturnsNull) {
    const char* xml = "<root><unclosed>";
    doc = PARSE(xml);

    // Invalid XML should return NULL
    EXPECT_EQ(doc, nullptr);
}

TEST_F(SimplifiedApiTest, ParseNullInput) {
    doc = taurus_parse(nullptr, 0);
    EXPECT_EQ(doc, nullptr);
}

TEST_F(SimplifiedApiTest, ParseEmptyString) {
    doc = taurus_parse("", 0);
    // Empty string is not valid XML
    EXPECT_EQ(doc, nullptr);
}

/* ============================================================================
 * taurus_root() Tests - Simplified root access
 * ============================================================================ */

TEST_F(SimplifiedApiTest, RootFromDocument) {
    doc = PARSE("<root><child/></root>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));
    EXPECT_STREQ(taurus_element_name(root), "root");
}

TEST_F(SimplifiedApiTest, RootFromNullDocument) {
    TaurusElement root = taurus_root(nullptr);
    EXPECT_TRUE(ELEM_IS_NULL(root));
}

TEST_F(SimplifiedApiTest, RootFromEmptyDocument) {
    // Create a document without parsing
    // Note: This might not be possible through the simplified API
    // but we test the null case anyway
    doc = PARSE("<root/>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));
}

/* ============================================================================
 * taurus_child() Tests - Simplified child finding
 * ============================================================================ */

TEST_F(SimplifiedApiTest, ChildFindFirst) {
    doc = PARSE("<root><child1/><child2/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    TaurusElement child = taurus_child(root, "child1");
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "child1");
}

TEST_F(SimplifiedApiTest, ChildFindSecond) {
    doc = PARSE("<root><child1/><child2/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    TaurusElement child = taurus_child(root, "child2");
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "child2");
}

TEST_F(SimplifiedApiTest, ChildNotFound) {
    doc = PARSE("<root><child1/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    TaurusElement child = taurus_child(root, "nonexistent");
    EXPECT_TRUE(ELEM_IS_NULL(child));
}

TEST_F(SimplifiedApiTest, ChildFromNullElement) {
    TaurusElement null_elem = ELEM_NULL();
    TaurusElement child = taurus_child(null_elem, "child");
    EXPECT_TRUE(ELEM_IS_NULL(child));
}

TEST_F(SimplifiedApiTest, ChildWithNullName) {
    doc = PARSE("<root><child/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    TaurusElement child = taurus_child(root, nullptr);
    // With null name, should return first child or null
    // Implementation dependent - just verify it doesn't crash
}

TEST_F(SimplifiedApiTest, ChildNestedElements) {
    doc = PARSE("<root><parent><child>deep</child></parent></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    TaurusElement parent = taurus_child(root, "parent");
    ASSERT_TRUE(ELEM_NOT_NULL(parent));

    TaurusElement child = taurus_child(parent, "child");
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_name(child), "child");
}

/* ============================================================================
 * taurus_attr() Tests - Simplified attribute access
 * ============================================================================ */

TEST_F(SimplifiedApiTest, AttrGetExisting) {
    doc = PARSE("<root attr1=\"value1\" attr2=\"value2\"/>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* value = taurus_attr(root, "attr1");
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "value1");
}

TEST_F(SimplifiedApiTest, AttrGetSecond) {
    doc = PARSE("<root attr1=\"value1\" attr2=\"value2\"/>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* value = taurus_attr(root, "attr2");
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "value2");
}

TEST_F(SimplifiedApiTest, AttrNotFound) {
    doc = PARSE("<root attr1=\"value1\"/>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* value = taurus_attr(root, "nonexistent");
    EXPECT_EQ(value, nullptr);
}

TEST_F(SimplifiedApiTest, AttrFromNullElement) {
    TaurusElement null_elem = ELEM_NULL();
    const char* value = taurus_attr(null_elem, "attr");
    EXPECT_EQ(value, nullptr);
}

TEST_F(SimplifiedApiTest, AttrWithNullName) {
    doc = PARSE("<root attr=\"value\"/>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* value = taurus_attr(root, nullptr);
    // With null name, should return null
    EXPECT_EQ(value, nullptr);
}

TEST_F(SimplifiedApiTest, AttrOnChildElement) {
    doc = PARSE("<root><child id=\"123\"/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    TaurusElement child = taurus_child(root, "child");
    ASSERT_TRUE(ELEM_NOT_NULL(child));

    const char* value = taurus_attr(child, "id");
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "123");
}

/* ============================================================================
 * taurus_text() Tests - Simplified text content access
 * ============================================================================ */

TEST_F(SimplifiedApiTest, TextGetContent) {
    doc = PARSE("<root>Hello World</root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* text = taurus_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "Hello World");
}

TEST_F(SimplifiedApiTest, TextGetEmpty) {
    doc = PARSE("<root/>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* text = taurus_text(root);
    // Empty element should return empty string or null
    // Implementation dependent
    if (text != nullptr) {
        EXPECT_STREQ(text, "");
    }
}

TEST_F(SimplifiedApiTest, TextFromNestedChild) {
    doc = PARSE("<root><child>Nested Text</child></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    TaurusElement child = taurus_child(root, "child");
    ASSERT_TRUE(ELEM_NOT_NULL(child));

    const char* text = taurus_text(child);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "Nested Text");
}

TEST_F(SimplifiedApiTest, TextFromNullElement) {
    TaurusElement null_elem = ELEM_NULL();
    const char* text = taurus_text(null_elem);
    // Implementation may return nullptr or empty string - both are acceptable
    // for a null element
    if (text != nullptr) {
        EXPECT_STREQ(text, "");
    }
    // If we reach here without crashing, test passes
}

TEST_F(SimplifiedApiTest, TextWithMixedContent) {
    doc = PARSE("<root>Hello <b>World</b>!</root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* text = taurus_text(root);
    ASSERT_NE(text, nullptr);
    // Should concatenate all text: "Hello World!"
    EXPECT_STREQ(text, "Hello World!");
}

/* ============================================================================
 * taurus_free() Tests - Simplified document cleanup
 * ============================================================================ */

TEST_F(SimplifiedApiTest, FreeDocument) {
    doc = PARSE("<root/>");
    ASSERT_NE(doc, nullptr);

    // Free using simplified API
    taurus_free(doc);
    doc = nullptr;  // Prevent double-free in TearDown

    // Document should be freed without crash
    SUCCEED();
}

TEST_F(SimplifiedApiTest, FreeNullDocument) {
    // Should not crash
    taurus_free(nullptr);
    SUCCEED();
}

TEST_F(SimplifiedApiTest, FreeAfterAccess) {
    doc = PARSE("<root attr=\"value\">text</root>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* attr = taurus_attr(root, "attr");
    ASSERT_NE(attr, nullptr);

    const char* text = taurus_text(root);
    ASSERT_NE(text, nullptr);

    // Free after all accesses
    taurus_free(doc);
    doc = nullptr;

    SUCCEED();
}

/* ============================================================================
 * Integration Tests - Complete workflow
 * ============================================================================ */

TEST_F(SimplifiedApiTest, FullWorkflow) {
    // Parse
    const char* xml = "<config>"
                      "  <server host=\"localhost\" port=\"8080\"/>"
                      "  <database url=\"localhost:5432\" name=\"mydb\"/>"
                      "</config>";

    doc = PARSE(xml);
    ASSERT_NE(doc, nullptr);

    // Get root
    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));
    EXPECT_STREQ(taurus_element_name(root), "config");

    // Find server element
    TaurusElement server = taurus_child(root, "server");
    ASSERT_TRUE(ELEM_NOT_NULL(server));

    // Get attributes
    const char* host = taurus_attr(server, "host");
    ASSERT_NE(host, nullptr);
    EXPECT_STREQ(host, "localhost");

    const char* port = taurus_attr(server, "port");
    ASSERT_NE(port, nullptr);
    EXPECT_STREQ(port, "8080");

    // Find database element
    TaurusElement db = taurus_child(root, "database");
    ASSERT_TRUE(ELEM_NOT_NULL(db));

    const char* url = taurus_attr(db, "url");
    ASSERT_NE(url, nullptr);
    EXPECT_STREQ(url, "localhost:5432");

    // Free
    taurus_free(doc);
    doc = nullptr;
}

TEST_F(SimplifiedApiTest, MultipleParsesSequential) {
    // First document
    doc = PARSE("<doc1><item/></doc1>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root1 = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root1));
    EXPECT_STREQ(taurus_element_name(root1), "doc1");

    taurus_free(doc);
    doc = nullptr;

    // Second document
    doc = PARSE("<doc2><item/></doc2>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root2 = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root2));
    EXPECT_STREQ(taurus_element_name(root2), "doc2");

    taurus_free(doc);
    doc = nullptr;
}

TEST_F(SimplifiedApiTest, LargeDocument) {
    // Build a large document
    std::string xml = "<root>";
    for (int i = 0; i < 100; i++) {
        xml += "<item id=\"" + std::to_string(i) + "\">Item " + std::to_string(i) + "</item>";
    }
    xml += "</root>";

    doc = taurus_parse(xml.c_str(), xml.length());
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Verify we can access various items
    TaurusElement item50 = taurus_child(root, "item");
    // First child should be first item
    ASSERT_TRUE(ELEM_NOT_NULL(item50));
    EXPECT_STREQ(taurus_element_name(item50), "item");

    // Check first item has id="0"
    const char* id = taurus_attr(item50, "id");
    ASSERT_NE(id, nullptr);
    EXPECT_STREQ(id, "0");

    taurus_free(doc);
    doc = nullptr;
}

} // namespace taurus_test
