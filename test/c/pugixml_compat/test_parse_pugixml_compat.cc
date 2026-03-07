/* test_parse_pugixml_compat.cpp - Pugixml-compatible parse tests for Taurus
 * Copyright (c) 2024, Ribose Inc.
 *
 * Adapted from pugixml test_parse.cpp for Taurus compatibility
 * Tests XML parsing functionality including PIs, comments, CDATA, and error cases
 */

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "../../src/include/taurus.h"
#include "../../src/taurus/dom/element.h"  // For TaurusElementNode

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

/**
 * Base class for parse tests
 */
class ParseTestBase : public ::testing::Test {
protected:
    TaurusDocument doc;
    TaurusElement root;
    std::string xml_buffer;  // Keep XML buffer alive for StringViews

    void SetUp() override {
        doc = nullptr;
        root = taurus_element_handle_null();
        /* Enable strict mode for pugixml compatibility */
        taurus_set_strict_mode(1);
    }

    void TearDown() override {
        if (doc) {
            taurus_document_free(doc);
            doc = nullptr;
        }
        xml_buffer.clear();  // Clear buffer after document is freed
        /* Disable strict mode after tests */
        taurus_set_strict_mode(0);
    }

    /**
     * Parse XML string directly
     * NOTE: Stores the XML in xml_buffer to keep StringViews valid
     */
    void parse_xml(const std::string& xml) {
        xml_buffer = xml;  // Store to keep buffer alive
        TaurusStatus status;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &status);
        ASSERT_NE(doc, nullptr) << "Failed to parse XML";
        ASSERT_EQ(status, TAURUS_OK) << "Parse status not OK: " << status;

        root = taurus_document_root(doc);
    }

    /**
     * Parse XML string expecting failure
     * NOTE: Stores the XML in xml_buffer to keep StringViews valid
     */
    void parse_xml_fail(const std::string& xml, TaurusStatus expected_status = TAURUS_ERROR_PARSE) {
        xml_buffer = xml;  // Store to keep buffer alive
        TaurusStatus status;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &status);
        // Document might be NULL or have error status
        if (doc) {
            // Parser created a document but had an error
            EXPECT_NE(status, TAURUS_OK);
        } else {
            // Parser failed completely
            EXPECT_EQ(status, expected_status);
        }
    }

    /**
     * Get element by name from children
     */
    TaurusElement get_child_by_name(TaurusElement parent, const char* name) {
        if (element_is_null(parent)) return taurus_element_handle_null();

        TaurusElement child = taurus_element_first_child(parent, nullptr);
        while (!element_is_null(child)) {
            const char* child_name = taurus_element_name(child);
            if (child_name && strcmp(child_name, name) == 0) {
                return child;
            }
            child = taurus_element_next_sibling(child, nullptr);
        }
        return taurus_element_handle_null();
    }
};

/* ============================================================================
 * Basic Element Parsing Tests
 * ============================================================================ */

TEST_F(ParseTestBase, ParseEmptyDocument) {
    // Empty document should fail
    parse_xml_fail("");
}

TEST_F(ParseTestBase, ParseSimpleElement) {
    parse_xml("<root/>");
    ASSERT_ELEM_NOT_NULL(root);
    EXPECT_STREQ(taurus_element_name(root), "root");
}

TEST_F(ParseTestBase, ParseElementWithContent) {
    parse_xml("<root>content</root>");
    ASSERT_ELEM_NOT_NULL(root);
    EXPECT_STREQ(taurus_element_name(root), "root");

    // In Taurus, text is stored inline, not as a child node
    // Use taurus_element_text() to access text content
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "content");
}

TEST_F(ParseTestBase, ParseNestedElements) {
    parse_xml("<root><child1/><child2/></root>");
    ASSERT_ELEM_NOT_NULL(root);

    TaurusElement child1 = get_child_by_name(root, "child1");
    TaurusElement child2 = get_child_by_name(root, "child2");
    ASSERT_ELEM_NOT_NULL(child1);
    ASSERT_ELEM_NOT_NULL(child2);
}

/* ============================================================================
 * Attribute Parsing Tests
 * ============================================================================ */

TEST_F(ParseTestBase, ParseSingleAttribute) {
    parse_xml("<root id=\"test\"/>");
    ASSERT_ELEM_NOT_NULL(root);

    const char* id_attr = taurus_element_attribute(root, "id");
    ASSERT_NE(id_attr, nullptr);
    EXPECT_STREQ(id_attr, "test");
}

TEST_F(ParseTestBase, ParseMultipleAttributes) {
    parse_xml("<root id=\"test\" class=\"main\" name=\"value\"/>");
    ASSERT_ELEM_NOT_NULL(root);

    const char* id_attr = taurus_element_attribute(root, "id");
    const char* class_attr = taurus_element_attribute(root, "class");
    const char* name_attr = taurus_element_attribute(root, "name");

    EXPECT_STREQ(id_attr, "test");
    EXPECT_STREQ(class_attr, "main");
    EXPECT_STREQ(name_attr, "value");
}

TEST_F(ParseTestBase, ParseAttributeWithSpecialChars) {
    parse_xml("<root attr=\"&lt;test&gt;\"/>");
    ASSERT_ELEM_NOT_NULL(root);

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "<test>");
}

TEST_F(ParseTestBase, ParseEmptyAttribute) {
    parse_xml("<root attr=\"\"/>");
    ASSERT_ELEM_NOT_NULL(root);

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "");
}

/* ============================================================================
 * Text Content Parsing Tests
 * ============================================================================ */

TEST_F(ParseTestBase, ParseSimpleText) {
    parse_xml("<root>Hello World</root>");
    ASSERT_ELEM_NOT_NULL(root);

    // In Taurus, text is stored inline, not as a child node
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "Hello World");
}

TEST_F(ParseTestBase, ParseTextWithEntities) {
    parse_xml("<root>&lt;test&gt;</root>");
    ASSERT_ELEM_NOT_NULL(root);

    // Entities are decoded during parsing
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<test>");
}

TEST_F(ParseTestBase, ParseTextWithWhitespace) {
    parse_xml("<root>  spaces  </root>");
    ASSERT_ELEM_NOT_NULL(root);

    // Taurus preserves whitespace in text content
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "  spaces  ");
}

/* ============================================================================
 * CDATA Section Tests
 * ============================================================================ */

TEST_F(ParseTestBase, ParseSimpleCDATA) {
    parse_xml("<root><![CDATA[<test>]]></root>");
    ASSERT_ELEM_NOT_NULL(root);

    // CDATA content is accessible via taurus_element_text
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<test>");
}

TEST_F(ParseTestBase, ParseCDATAWithSpecialChars) {
    parse_xml("<root><![CDATA[&lt;&gt;&amp;]]></root>");
    ASSERT_ELEM_NOT_NULL(root);

    // CDATA content is preserved literally
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    // CDATA preserves content literally, so entities are NOT decoded
    EXPECT_STREQ(text, "&lt;&gt;&amp;");
}

TEST_F(ParseTestBase, ParseEmptyCDATA) {
    parse_xml("<root><![CDATA[]]></root>");
    ASSERT_ELEM_NOT_NULL(root);

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "");
}

/* ============================================================================
 * Namespace Parsing Tests
 * ============================================================================ */

TEST_F(ParseTestBase, ParseDefaultNamespace) {
    parse_xml("<root xmlns=\"http://example.com\"/>");
    ASSERT_ELEM_NOT_NULL(root);
    EXPECT_STREQ(taurus_element_name(root), "root");
}

TEST_F(ParseTestBase, ParsePrefixedNamespace) {
    parse_xml("<root xmlns:ns=\"http://example.com\"/>");
    ASSERT_ELEM_NOT_NULL(root);
    EXPECT_STREQ(taurus_element_name(root), "root");
}

TEST_F(ParseTestBase, ParseElementWithNamespacePrefix) {
    parse_xml("<ns:root xmlns:ns=\"http://example.com\"/>");
    ASSERT_ELEM_NOT_NULL(root);
    // Element name returns local name only (libxml2 compatibility)
    // For qualified name (prefix:local), use taurus_element_get_qualified_name() when available
    EXPECT_STREQ(taurus_element_name(root), "root");
}

/* ============================================================================
 * Error Case Tests
 * ============================================================================ */

TEST_F(ParseTestBase, ParseUnclosedTag) {
    parse_xml_fail("<root>");
}

TEST_F(ParseTestBase, ParseMismatchedTags) {
    parse_xml_fail("<root></other>");
}

TEST_F(ParseTestBase, ParseInvalidAttributeName) {
    parse_xml_fail("<root 123attr=\"value\"/>");
}

TEST_F(ParseTestBase, ParseUnclosedAttribute) {
    parse_xml_fail("<root attr=\"value/>");
}

TEST_F(ParseTestBase, ParseNestedQuotes) {
    parse_xml_fail("<root attr=\"value\"extra\"/>");
}

TEST_F(ParseTestBase, ParseInvalidEntity) {
    parse_xml_fail("<root>&invalid;</root>");
}

TEST_F(ParseTestBase, ParseUnclosedEntity) {
    parse_xml_fail("<root>&lt</root>");
}

/* ============================================================================
 * Whitespace Handling Tests
 * ============================================================================ */

TEST_F(ParseTestBase, ParsePreserveWhitespace) {
    parse_xml("<root>  text  </root>");
    ASSERT_ELEM_NOT_NULL(root);

    // In Taurus, text is stored inline, not as a child node
    const char* text = taurus_element_text(root);
    EXPECT_STREQ(text, "  text  ");
}

TEST_F(ParseTestBase, ParseMultipleTextNodes) {
    parse_xml("<root>text1<!--comment-->text2</root>");
    ASSERT_ELEM_NOT_NULL(root);

    // In Taurus, text is stored inline and comments don't create separate nodes
    // The text will be concatenated (comments are filtered out)
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    // Text should contain both parts (comment stripped)
    EXPECT_STREQ(text, "text1text2");
}

/* ============================================================================
 * Complex Document Tests
 * ============================================================================ */

TEST_F(ParseTestBase, ParseDeepNesting) {
    parse_xml("<root><l1><l2><l3><l4><l5>deep</l5></l4></l3></l2></l1></root>");
    ASSERT_ELEM_NOT_NULL(root);

    TaurusElement l1 = get_child_by_name(root, "l1");
    ASSERT_ELEM_NOT_NULL(l1);

    TaurusElement l2 = get_child_by_name(l1, "l2");
    ASSERT_ELEM_NOT_NULL(l2);

    TaurusElement l3 = get_child_by_name(l2, "l3");
    ASSERT_ELEM_NOT_NULL(l3);

    TaurusElement l4 = get_child_by_name(l3, "l4");
    ASSERT_ELEM_NOT_NULL(l4);

    TaurusElement l5 = get_child_by_name(l4, "l5");
    ASSERT_ELEM_NOT_NULL(l5);

    // In Taurus, text is stored inline in the element
    const char* text = taurus_element_text(l5);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "deep");
}

TEST_F(ParseTestBase, ParseManySiblings) {
    std::string xml = "<root>";
    for (int i = 0; i < 100; i++) {
        xml += "<child/>";
    }
    xml += "</root>";
    parse_xml(xml);

    ASSERT_ELEM_NOT_NULL(root);
    int child_count = 0;
    TaurusElement child = taurus_element_first_child(root, nullptr);
    while (!element_is_null(child)) {
        child_count++;
        child = taurus_element_next_sibling(child, nullptr);
    }
    EXPECT_EQ(child_count, 100);
}

/* ============================================================================
 * XML Declaration Tests
 * ============================================================================ */

TEST_F(ParseTestBase, ParseWithXMLDeclaration) {
    parse_xml("<?xml version=\"1.0\"?><root/>");
    ASSERT_ELEM_NOT_NULL(root);
    EXPECT_STREQ(taurus_element_name(root), "root");
}

TEST_F(ParseTestBase, ParseWithXMLDeclarationEncoding) {
    parse_xml("<?xml version=\"1.0\" encoding=\"UTF-8\"?><root/>");
    ASSERT_ELEM_NOT_NULL(root);
    EXPECT_STREQ(taurus_element_name(root), "root");
}

TEST_F(ParseTestBase, ParseWithXMLDeclarationStandalone) {
    parse_xml("<?xml version=\"1.0\" standalone=\"yes\"?><root/>");
    ASSERT_ELEM_NOT_NULL(root);
    EXPECT_STREQ(taurus_element_name(root), "root");
}

/* ============================================================================
 * DOCTYPE Tests
 * ============================================================================ */

TEST_F(ParseTestBase, ParseWithInternalDTD) {
    parse_xml("<!DOCTYPE root [ <!ELEMENT root (#PCDATA)> ]><root>test</root>");
    ASSERT_ELEM_NOT_NULL(root);
    EXPECT_STREQ(taurus_element_name(root), "root");
}

TEST_F(ParseTestBase, ParseWithExternalDTD) {
    parse_xml("<!DOCTYPE root SYSTEM \"test.dtd\"><root>test</root>");
    ASSERT_ELEM_NOT_NULL(root);
    EXPECT_STREQ(taurus_element_name(root), "root");
}

} // namespace taurus_test
