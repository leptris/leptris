/* test_document_level.cc - Document-level API tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for document-level operations based on pugixml/test_document.cpp
 * Tests document creation, reset, edge cases, and status codes.
 */

#include <gtest/gtest.h>
#include <string.h>
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
 * Test class for document-level operations
 */
class DocumentLevelTest : public ::testing::Test {
protected:
    TaurusDocument doc;
    std::string xml_buffer;

    void SetUp() override {
        doc = nullptr;
        xml_buffer.clear();
    }

    void TearDown() override {
        if (doc) {
            taurus_document_free(doc);
            doc = nullptr;
        }
        xml_buffer.clear();
    }

    // Helper to parse XML string
    void parse_string(const std::string& xml) {
        xml_buffer = xml;
        TaurusStatus status;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &status);
        ASSERT_EQ(status, TAURUS_OK) << "Failed to parse XML: " << xml;
        ASSERT_NE(doc, nullptr);
    }

    // Helper to parse with expected failure
    void parse_string_expect_error(const std::string& xml, TaurusStatus expected_status) {
        xml_buffer = xml;
        TaurusStatus status;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &status);
        EXPECT_EQ(status, expected_status) << "Expected status " << expected_status << " but got " << status;
    }
};

// ============================================================================
// Document Creation Tests
// ============================================================================

TEST_F(DocumentLevelTest, CreateEmptyDocument) {
    // Empty document should be null initially
    EXPECT_EQ(doc, nullptr);

    // Create document by parsing
    parse_string("<node/>");
    ASSERT_NE(doc, nullptr);

    // Should have root element
    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));
}

TEST_F(DocumentLevelTest, CreateDocumentWithContent) {
    parse_string("<root>text</root>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Verify element name
    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "root");
}

TEST_F(DocumentLevelTest, ResetDocument) {
    // Parse first document
    parse_string("<node1><child/></node1>");
    TaurusElement root1 = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root1));

    // Reset document
    taurus_document_free(doc);
    doc = nullptr;

    // Should be able to parse again
    parse_string("<node2/>");
    TaurusElement root2 = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root2));

    const char* name = taurus_element_name(root2);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "node2");
}

// ============================================================================
// Empty/Edge Case Buffer Handling
// ============================================================================

TEST_F(DocumentLevelTest, EmptyString) {
    parse_string_expect_error("", TAURUS_ERROR_PARSE);
    EXPECT_EQ(doc, nullptr);
}

TEST_F(DocumentLevelTest, WhitespaceOnly) {
    // Should fail - no document element
    parse_string_expect_error("   ", TAURUS_ERROR_PARSE);
    // Document may be created but with no root
    if (doc) {
        EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_document_root(doc)));
    }
}

TEST_F(DocumentLevelTest, NewlineOnly) {
    // Should fail - no document element
    parse_string_expect_error("\n", TAURUS_ERROR_PARSE);
    if (doc) {
        EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_document_root(doc)));
    }
}

TEST_F(DocumentLevelTest, CommentOnly) {
    // Comment only - no document element
    parse_string_expect_error("<!--comment-->", TAURUS_ERROR_PARSE);
    if (doc) {
        // Document may be created but root is null
        TaurusElement root = taurus_document_root(doc);
        // This is acceptable - comments don't create root element
    }
}

TEST_F(DocumentLevelTest, PITextOnly) {
    // PI only - no document element
    parse_string_expect_error("<?pi target?>", TAURUS_ERROR_PARSE);
    if (doc) {
        // Document may be created but root is null
        TaurusElement root = taurus_document_root(doc);
        // This is acceptable - PIs don't create root element
    }
}

TEST_F(DocumentLevelTest, DeclarationOnly) {
    // XML declaration only - no document element
    parse_string_expect_error("<?xml version=\"1.0\"?>", TAURUS_ERROR_PARSE);
    if (doc) {
        EXPECT_TRUE(ELEM_IS_NULL_TMP(taurus_document_root(doc)));
    }
}

TEST_F(DocumentLevelTest, MinimalElement) {
    parse_string("<n/>");
    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));
    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "n");
}

// ============================================================================
// Malformed XML Tests
// ============================================================================

TEST_F(DocumentLevelTest, UnclosedTag) {
    parse_string_expect_error("<foo><bar/>", TAURUS_ERROR_PARSE);
    // Document may be partially constructed
}

TEST_F(DocumentLevelTest, MismatchedTags) {
    parse_string_expect_error("<foo></bar>", TAURUS_ERROR_PARSE);
}

TEST_F(DocumentLevelTest, InvalidAttributeName) {
    // Invalid attribute name (starts with digit)
    parse_string_expect_error("<foo 1attr='value'/>", TAURUS_ERROR_PARSE);
}

// ============================================================================
// Document Element Access
// ============================================================================

TEST_F(DocumentLevelTest, DocumentElementWithDeclaration) {
    parse_string("<?xml version='1.0'?><node><child/></node>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "node");
}

TEST_F(DocumentLevelTest, DocumentElementWithCommentBefore) {
    parse_string("<!--comment--><node><child/></node>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "node");
}

TEST_F(DocumentLevelTest, DocumentElementWithPIBefore) {
    parse_string("<?pi target?><node><child/></node>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "node");
}

TEST_F(DocumentLevelTest, DocumentElementWithMixedBefore) {
    parse_string("<?xml version='1.0'?><!--comment--><?pi?><node><child/></node>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "node");
}

// ============================================================================
// Multiple Elements Tests
// ============================================================================

TEST_F(DocumentLevelTest, MultipleRootElements) {
    // Only first element should be root (pugixml-compatible lenient parsing)
    parse_string("<node1/><node2/><node3/>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "node1");
}

TEST_F(DocumentLevelTest, ElementWithChildren) {
    parse_string("<root><child1/><child2><grandchild/></child2><child3/></root>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "root");

    // Check children
    TaurusElement child = taurus_element_child(root, 0);
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    name = taurus_element_name(child);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "child1");
}

// ============================================================================
// Buffer Edge Cases
// ============================================================================

TEST_F(DocumentLevelTest, VeryLongElementName) {
    // Create element with very long name
    std::string xml = "<";
    xml.append(1000, 'a');  // 1000 'a' characters
    xml.append("/>");

    parse_string(xml);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Name should be preserved
    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(strlen(name), 1000);
}

TEST_F(DocumentLevelTest, ManyAttributes) {
    // Element with many attributes
    std::string xml = "<root ";
    for (int i = 0; i < 50; i++) {
        xml += "attr";
        xml += std::to_string(i);
        xml += "='value' ";
    }
    xml += "/>";

    parse_string(xml);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Verify we can access attributes
    for (int i = 0; i < 50; i++) {
        std::string attr_name = "attr" + std::to_string(i);
        const char* value = taurus_element_attribute(root, attr_name.c_str());
        ASSERT_NE(value, nullptr) << "Attribute " << attr_name << " not found";
        EXPECT_STREQ(value, "value");
    }
}

TEST_F(DocumentLevelTest, DeeplyNestedElements) {
    // Create deeply nested structure
    std::string xml = "<root>";
    for (int i = 0; i < 100; i++) {
        xml += "<level" + std::to_string(i) + ">";
    }
    xml += "text";
    for (int i = 0; i < 100; i++) {
        xml += "</level" + std::to_string(99 - i) + ">";
    }
    xml += "</root>";

    parse_string(xml);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "root");
}

// ============================================================================
// Special Characters Tests
// ============================================================================

TEST_F(DocumentLevelTest, ElementNameWithUnderscore) {
    parse_string("<my_element/>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "my_element");
}

TEST_F(DocumentLevelTest, ElementNameWithHyphen) {
    parse_string("<my-element/>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "my-element");
}

TEST_F(DocumentLevelTest, ElementNameWithPeriod) {
    parse_string("<my.element/>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "my.element");
}

TEST_F(DocumentLevelTest, ElementNameWithNumbers) {
    parse_string("<elem123/>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "elem123");
}

// ============================================================================
// Text Content Tests
// ============================================================================

TEST_F(DocumentLevelTest, ElementWithText) {
    parse_string("<root>text content</root>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Get text content
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "text content");
}

TEST_F(DocumentLevelTest, ElementWithEmptyText) {
    parse_string("<root></root>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* text = taurus_element_text(root);
    // Empty text is acceptable
    if (text) {
        EXPECT_STREQ(text, "");
    }
}

TEST_F(DocumentLevelTest, ElementWithWhitespaceText) {
    parse_string("<root>   </root>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "   ");
}

TEST_F(DocumentLevelTest, ElementWithMixedContent) {
    parse_string("<root>text1<child/>text2</root>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Text content should concatenate all text nodes
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "text1text2");
}

// ============================================================================
// Self-Closing Element Tests
// ============================================================================

TEST_F(DocumentLevelTest, SelfClosingElement) {
    parse_string("<node/>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Should have no children
    TaurusElement child = taurus_element_child(root, 0);
    EXPECT_TRUE(ELEM_IS_NULL(child));
}

TEST_F(DocumentLevelTest, SelfClosingElementWithSpace) {
    parse_string("<node />");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    TaurusElement child = taurus_element_child(root, 0);
    EXPECT_TRUE(ELEM_IS_NULL(child));
}

TEST_F(DocumentLevelTest, SelfClosingElementWithAttributes) {
    parse_string("<node attr1='value1' attr2='value2'/>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* attr1 = taurus_element_attribute(root, "attr1");
    ASSERT_NE(attr1, nullptr);
    EXPECT_STREQ(attr1, "value1");

    const char* attr2 = taurus_element_attribute(root, "attr2");
    ASSERT_NE(attr2, nullptr);
    EXPECT_STREQ(attr2, "value2");
}

// ============================================================================
// Unicode Tests
// ============================================================================

#ifdef TAURUS_HAS_UTF8PROC
TEST_F(DocumentLevelTest, UTF8CharactersInElementName) {
    // Unicode characters in element name (Cyrillic)
    // NOTE: This test requires utf8proc to be enabled for proper UTF-8 validation
    parse_string("<\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82/>");  // "привет"

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));
}
#else
TEST_F(DocumentLevelTest, UTF8CharactersInElementName_Disabled) {
    // UTF-8 element name validation requires utf8proc library
    // This test is disabled when TAURUS_ENABLE_UTF8PROC=OFF
    GTEST_SKIP() << "UTF-8 element names require utf8proc (TAURUS_ENABLE_UTF8PROC=OFF)";
}
#endif

TEST_F(DocumentLevelTest, UTF8CharactersInText) {
    // Unicode text content (Cyrillic)
    parse_string("<root>\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82</root>");  // "привет"

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
}

TEST_F(DocumentLevelTest, UTF8CharactersInAttribute) {
    // Unicode in attribute value
    parse_string("<root attr='\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82'/>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
}

// ============================================================================
// Memory Management Tests
// ============================================================================

TEST_F(DocumentLevelTest, MultipleDocumentLifecycle) {
    // Create and destroy multiple documents
    for (int i = 0; i < 100; i++) {
        TaurusDocument doc2;
        TaurusStatus status;
        std::string xml = "<node" + std::to_string(i) + "/>";
        doc2 = taurus_parse_string(xml.c_str(), xml.length(), &status);
        ASSERT_EQ(status, TAURUS_OK);
        ASSERT_NE(doc2, nullptr);

        TaurusElement root = taurus_document_root(doc2);
        ASSERT_TRUE(ELEM_NOT_NULL(root));

        taurus_document_free(doc2);
    }
}

TEST_F(DocumentLevelTest, LargeDocument) {
    // Create document with many elements
    std::string xml = "<root>";
    for (int i = 0; i < 1000; i++) {
        xml += "<child" + std::to_string(i) + " attr='" + std::to_string(i) + "'/>";
    }
    xml += "</root>";

    parse_string(xml);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));
}

} // namespace taurus_test
