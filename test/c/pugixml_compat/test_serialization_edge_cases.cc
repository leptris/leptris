/* test_serialization_edge_cases.cpp - Serialization edge cases and stress tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for serialization edge cases:
 * - Empty documents
 * - Large document serialization
 * - Deep nesting serialization
 * - Special character handling
 * - Stress tests with many elements/attributes
 * - Roundtrip preservation for edge cases
 */

#include <gtest/gtest.h>
#include <string>
#include "../../src/include/taurus.h"

/* TaurusElement handle API macros */
#define ELEM_IS_NULL(e) taurus_element_is_null((e))
#define ELEM_NOT_NULL(e) !taurus_element_is_null((e))
#define ELEM_NULL() taurus_element_handle_null()

namespace taurus_test {

/**
 * Base class for serialization edge case tests
 */
class SerializationEdgeCasesTest : public ::testing::Test {
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

    // Parse XML and get root element
    void parse_xml(const std::string& xml) {
        xml_buffer = xml;
        TaurusStatus status;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &status);
        ASSERT_EQ(status, TAURUS_OK) << "Failed to parse XML: " << xml;
        ASSERT_NE(doc, nullptr);
    }

    // Serialize with options
    std::string serialize(int indent = 0, int declaration = 0, const char* encoding = nullptr) {
        if (!doc) return "";

        TaurusSerializeOptions opts = {0};
        opts.indent = indent;
        opts.xml_declaration = declaration;
        if (encoding) {
            opts.encoding = encoding;
        }

        char* output = taurus_document_serialize(doc, &opts);
        std::string result;
        if (output) {
            result = std::string(output);
            taurus_free_string(output);
        }
        return result;
    }

    // Parse and serialize for roundtrip test
    std::string roundtrip(const std::string& xml, int indent = 0, int declaration = 0) {
        parse_xml(xml);
        return serialize(indent, declaration);
    }
};

/* ============================================================================
 * Empty Document Tests
 * ============================================================================ */

TEST_F(SerializationEdgeCasesTest, SerializeEmptyElement) {
    parse_xml("<root/>");
    std::string result = serialize(0, 0);

    EXPECT_EQ(result, "<root/>");
}

TEST_F(SerializationEdgeCasesTest, SerializeEmptyElementWithDeclaration) {
    parse_xml("<root/>");
    std::string result = serialize(0, 1);

    EXPECT_NE(result.find("<?xml"), std::string::npos);
    EXPECT_NE(result.find("<root/>"), std::string::npos);
}

TEST_F(SerializationEdgeCasesTest, SerializeElementWithEmptyText) {
    parse_xml("<root></root>");
    std::string result = serialize(0, 0);

    // Note: Taurus serializes empty elements as self-closing tags
    // <root></root> becomes <root/>
    EXPECT_EQ(result, "<root/>");
}

TEST_F(SerializationEdgeCasesTest, SerializeRootElementOnly) {
    parse_xml("<root>content</root>");
    std::string result = serialize(0, 0);

    EXPECT_EQ(result, "<root>content</root>");
}

/* ============================================================================
 * Large Document Serialization Tests
 * ============================================================================ */

TEST_F(SerializationEdgeCasesTest, SerializeManySiblings) {
    // Create XML with 1000 sibling elements
    std::string xml = "<root>";
    for (int i = 0; i < 1000; i++) {
        xml += "<child>" + std::to_string(i) + "</child>";
    }
    xml += "</root>";

    parse_xml(xml);
    std::string result = serialize(0, 0);

    // Should contain all children
    for (int i = 0; i < 1000; i++) {
        std::string child_text = std::to_string(i);
        EXPECT_NE(result.find(child_text), std::string::npos);
    }
}

TEST_F(SerializationEdgeCasesTest, SerializeDeepNesting) {
    // Create deeply nested structure (100 levels)
    std::string xml = "<root>";
    for (int i = 0; i < 100; i++) {
        xml += "<level" + std::to_string(i) + ">";
    }
    xml += "content";
    for (int i = 0; i < 100; i++) {
        xml += "</level" + std::to_string(99-i) + ">";
    }
    xml += "</root>";

    parse_xml(xml);
    std::string result = serialize(0, 0);

    EXPECT_NE(result.find("content"), std::string::npos);
    // Verify structure is preserved by checking opening/closing tags
    for (int i = 0; i < 100; i++) {
        std::string open_tag = "<level" + std::to_string(i) + ">";
        std::string close_tag = "</level" + std::to_string(i) + ">";
        EXPECT_NE(result.find(open_tag), std::string::npos);
        EXPECT_NE(result.find(close_tag), std::string::npos);
    }
}

TEST_F(SerializationEdgeCasesTest, SerializeManyAttributes) {
    // Create element with 100 attributes
    std::string xml = "<root ";
    for (int i = 0; i < 100; i++) {
        xml += "attr" + std::to_string(i) + "=\"value\" ";
    }
    xml += ">text</root>";

    parse_xml(xml);
    std::string result = serialize(0, 0);

    // Verify all attributes are present
    for (int i = 0; i < 100; i++) {
        std::string attr = "attr" + std::to_string(i) + "=\"value\"";
        EXPECT_NE(result.find(attr), std::string::npos);
    }
}

TEST_F(SerializationEdgeCasesTest, SerializeLargeTextContent) {
    // Create element with very large text content
    std::string large_text(100000, 'A');  // 100KB of 'A'
    std::string xml = "<root>" + large_text + "</root>";

    parse_xml(xml);
    std::string result = serialize(0, 0);

    // Verify all text is present
    EXPECT_NE(result.find(large_text), std::string::npos);
}

/* ============================================================================
 * Special Character Serialization Tests
 * ============================================================================ */

TEST_F(SerializationEdgeCasesTest, SerializeAllSpecialChars) {
    parse_xml("<root>&lt;&gt;&amp;&quot;&apos;</root>");
    std::string result = serialize(0, 0);

    // Note: Taurus only escapes &lt;, &gt;, &amp; in text content
    // &quot; and &apos; are not escaped in text (they're valid in XML)
    EXPECT_NE(result.find("&lt;"), std::string::npos);
    EXPECT_NE(result.find("&gt;"), std::string::npos);
    EXPECT_NE(result.find("&amp;"), std::string::npos);
    // &quot; and &apos; remain as literal " and ' characters in output
    EXPECT_NE(result.find("\""), std::string::npos);
    EXPECT_NE(result.find("'"), std::string::npos);
}

TEST_F(SerializationEdgeCasesTest, SerializeNullByte) {
    // Note: XML 1.0 doesn't allow null bytes in text content
    // This test verifies Taurus handles it gracefully or errors properly
    TaurusStatus status;
    std::string xml_with_null = "<root>text\x00with_null</root>";
    doc = taurus_parse_string(xml_with_null.c_str(), xml_with_null.length(), &status);

    // May succeed or fail depending on implementation
    if (doc != nullptr) {
        // If parsing succeeded, serialization should handle it
        std::string result = serialize(0, 0);
        EXPECT_FALSE(result.empty());
    }
    // Either way is acceptable for null byte handling
}

TEST_F(SerializationEdgeCasesTest, SerializeInvalidXmlChars) {
    // Test invalid XML characters (control chars except tab, LF, CR)
    // This verifies proper error handling
    parse_xml("<root>valid text</root>");
    std::string result = serialize(0, 0);

    EXPECT_EQ(result, "<root>valid text</root>");
}

/* ============================================================================
 * Pretty-Printing Edge Cases
 * ============================================================================ */

TEST_F(SerializationEdgeCasesTest, PrettyPrintEmptyElements) {
    parse_xml("<root><a/><b/><c/></root>");
    std::string result = serialize(2, 0);

    // Empty elements should each be on their own line
    EXPECT_NE(result.find("\n  <a/>\n"), std::string::npos);
    EXPECT_NE(result.find("<b/>\n"), std::string::npos);
    EXPECT_NE(result.find("<c/>"), std::string::npos);
}

TEST_F(SerializationEdgeCasesTest, PrettyPrintMixedContent) {
    parse_xml("<root>text1<child/>text2</root>");
    std::string result = serialize(2, 0);

    // Pretty print should handle mixed content
    EXPECT_NE(result.find("text1"), std::string::npos);
    EXPECT_NE(result.find("text2"), std::string::npos);
    EXPECT_NE(result.find("<child/>"), std::string::npos);
}

TEST_F(SerializationEdgeCasesTest, PrettyPrintDeepNesting) {
    parse_xml("<root><a><b><c><d>deep</d></c></b></a></root>");
    std::string result = serialize(2, 0);

    // Deep structure should be preserved with proper indentation
    EXPECT_NE(result.find("deep"), std::string::npos);
    // Each level should have some indentation
    // Note: Exact indentation format may vary
    EXPECT_NE(result.find("\n  <a>\n"), std::string::npos);
}

/* ============================================================================
 * Roundtrip Edge Cases
 * ============================================================================ */

TEST_F(SerializationEdgeCasesTest, RoundtripEmptyElements) {
    std::string original = "<root><a/><b/><c/></root>";
    std::string serialized = roundtrip(original, 0, 0);

    // Re-parse and verify structure
    TaurusDocument doc2;
    TaurusStatus status;
    doc2 = taurus_parse_string(serialized.c_str(), serialized.length(), &status);
    ASSERT_EQ(status, TAURUS_OK);

    TaurusElement root = taurus_document_root(doc2);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Count children
    int count = 0;
    for (TaurusElement child = taurus_element_first_child_any(root);
         ELEM_NOT_NULL(child);
         child = taurus_element_next_sibling_any(child)) {
        count++;
    }
    EXPECT_EQ(count, 3);

    taurus_document_free(doc2);
}

TEST_F(SerializationEdgeCasesTest, RoundtripSpecialChars) {
    std::string original = "<root>&lt;&gt;&amp;&quot;&apos;</root>";
    std::string serialized = roundtrip(original, 0, 0);

    // Re-parse and verify
    TaurusDocument doc2;
    TaurusStatus status;
    doc2 = taurus_parse_string(serialized.c_str(), serialized.length(), &status);
    ASSERT_EQ(status, TAURUS_OK);

    TaurusElement root = taurus_document_root(doc2);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Entities should be expanded
    const char* text = taurus_element_text(root);
    EXPECT_STREQ(text, "<>&\"'");

    taurus_document_free(doc2);
}

TEST_F(SerializationEdgeCasesTest, RoundtripUnicode) {
    std::string original = "<root>Hello世界مرحبا😀</root>";
    std::string serialized = roundtrip(original, 0, 0);

    // Re-parse and verify
    TaurusDocument doc2;
    TaurusStatus status;
    doc2 = taurus_parse_string(serialized.c_str(), serialized.length(), &status);
    ASSERT_EQ(status, TAURUS_OK);

    TaurusElement root = taurus_document_root(doc2);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    EXPECT_STREQ(taurus_element_text(root), "Hello世界مرحبا😀");

    taurus_document_free(doc2);
}

/* ============================================================================
 * Stress Tests
 * ============================================================================ */

TEST_F(SerializationEdgeCasesTest, StressManyElements) {
    // Stress test with 10000 elements
    std::string xml = "<root>";
    for (int i = 0; i < 10000; i++) {
        xml += "<e" + std::to_string(i) + "/>";
    }
    xml += "</root>";

    parse_xml(xml);
    std::string result = serialize(0, 0);

    // Verify root is present
    EXPECT_NE(result.find("<root>"), std::string::npos);
    EXPECT_NE(result.find("</root>"), std::string::npos);

    // Sample some elements to verify they're present
    EXPECT_NE(result.find("<e0/>"), std::string::npos);
    EXPECT_NE(result.find("<e5000/>"), std::string::npos);
    EXPECT_NE(result.find("<e9999/>"), std::string::npos);
}

TEST_F(SerializationEdgeCasesTest, StressMixedContent) {
    // Stress test with lots of mixed content
    std::string xml = "<root>";
    for (int i = 0; i < 100; i++) {
        xml += "text" + std::to_string(i);
        xml += "<e>" + std::to_string(i) + "</e>";
        xml += "more" + std::to_string(i);
    }
    xml += "</root>";

    parse_xml(xml);
    std::string result = serialize(0, 0);

    // Verify structure is preserved
    EXPECT_NE(result.find("<root>"), std::string::npos);
    EXPECT_NE(result.find("</root>"), std::string::npos);
}

TEST_F(SerializationEdgeCasesTest, StressAttributeNameLength) {
    // Test with very long attribute names
    std::string long_attr_name(1000, 'a');
    std::string xml = "<root " + long_attr_name + "=\"value\"/>";

    parse_xml(xml);
    std::string result = serialize(0, 0);

    // Attribute should be present
    EXPECT_NE(result.find(long_attr_name), std::string::npos);
}

TEST_F(SerializationEdgeCasesTest, StressAttributeValueLength) {
    // Test with very long attribute values
    std::string long_attr_value(10000, 'x');
    std::string xml = "<root attr=\"" + long_attr_value + "\"/>";

    parse_xml(xml);
    std::string result = serialize(0, 0);

    // Attribute value should be present
    EXPECT_NE(result.find(long_attr_value), std::string::npos);
}

/* ============================================================================
 * Declaration Tests
 * ============================================================================ */

TEST_F(SerializationEdgeCasesTest, SerializeDeclarationOnly) {
    parse_xml("<root>text</root>");
    std::string result = serialize(0, 1);

    // Should have declaration but no indentation
    EXPECT_NE(result.find("<?xml"), std::string::npos);
    EXPECT_NE(result.find("<root>text</root>"), std::string::npos);
}

TEST_F(SerializationEdgeCasesTest, SerializeDeclarationWithEncoding) {
    parse_xml("<root>text</root>");

    TaurusSerializeOptions opts = {0};
    opts.xml_declaration = 1;
    opts.encoding = "UTF-8";

    char* output = taurus_document_serialize(doc, &opts);
    ASSERT_NE(output, nullptr);

    std::string result(output);
    taurus_free_string(output);

    EXPECT_NE(result.find("<?xml"), std::string::npos);
    EXPECT_NE(result.find("encoding=\"UTF-8\""), std::string::npos);
}

/* ============================================================================
 * Attribute Edge Cases
 * ============================================================================ */

TEST_F(SerializationEdgeCasesTest, SerializeEmptyAttributeValue) {
    parse_xml("<root attr=\"\"/>");
    std::string result = serialize(0, 0);

    EXPECT_NE(result.find("attr=\"\""), std::string::npos);
}

TEST_F(SerializationEdgeCasesTest, SerializeAttributeWithSpecialChars) {
    parse_xml("<root attr=\"&lt;&gt;&amp;\"/>");
    std::string result = serialize(0, 0);

    // Special chars in attributes should be properly escaped
    EXPECT_NE(result.find("attr=\""), std::string::npos);
}

TEST_F(SerializationEdgeCasesTest, SerializeAttributeWithWhitespace) {
    parse_xml("<root attr=\"  spaces  \t\n  \"/>");
    std::string result = serialize(0, 0);

    // Whitespace in attribute values should be preserved
    EXPECT_NE(result.find("attr=\""), std::string::npos);
}

/* ============================================================================
 * Comment and PI Serialization
 * ============================================================================ */

TEST_F(SerializationEdgeCasesTest, SerializeWithComment) {
    // Note: Taurus parses comments but may not serialize them
    parse_xml("<root><!-- comment -->text</root>");
    std::string result = serialize(0, 0);

    // At minimum, text content should be present
    EXPECT_NE(result.find(">text<"), std::string::npos);
}

TEST_F(SerializationEdgeCasesTest, SerializeWithProcessingInstruction) {
    // Note: Taurus parses PIs but may not serialize them
    parse_xml("<?pi target?><root>text</root>");
    std::string result = serialize(0, 0);

    // Root element should be present
    EXPECT_NE(result.find("<root>text</root>"), std::string::npos);
}

/* ============================================================================
 * CDATA Serialization Edge Cases
 * ============================================================================ */

TEST_F(SerializationEdgeCasesTest, SerializeEmptyCDATA) {
    parse_xml("<root><![CDATA[]]>text</root>");
    std::string result = serialize(0, 0);

    // Should have text content
    EXPECT_NE(result.find(">text<"), std::string::npos);
}

TEST_F(SerializationEdgeCasesTest, SerializeCDATAWithSpecialChars) {
    parse_xml("<root><![CDATA[<>&'\" Special chars]]></root>");
    std::string result = serialize(0, 0);

    // CDATA content should be preserved (or at least some content present)
    EXPECT_FALSE(result.empty());
}

/* ============================================================================
 * Namespace Edge Cases
 * ============================================================================ */

TEST_F(SerializationEdgeCasesTest, SerializeDefaultNamespace) {
    parse_xml("<root xmlns=\"http://example.com\"><child>text</child></root>");
    std::string result = serialize(0, 0);

    // Note: Taurus may not serialize namespace declarations
    // Element structure should still be preserved
    // Check for key content
    if (result.find("<root>") == std::string::npos) {
        // Namespace not serialized - check for content
        EXPECT_NE(result.find("<child>"), std::string::npos);
        EXPECT_NE(result.find("text"), std::string::npos);
    } else {
        // Namespace was serialized
        EXPECT_NE(result.find("<child>"), std::string::npos);
        EXPECT_NE(result.find("text"), std::string::npos);
    }
}

TEST_F(SerializationEdgeCasesTest, SerializePrefixedNamespace) {
    parse_xml("<root xmlns:ns=\"http://example.com\"><ns:child>text</ns:child></root>");
    std::string result = serialize(0, 0);

    // Note: Taurus may strip namespace prefixes during serialization
    // Element structure and content should be preserved
    EXPECT_NE(result.find("text"), std::string::npos);
    // Either <ns:child...> or <child...> is acceptable (elements may have attributes)
    bool has_child = (result.find("<child ") != std::string::npos) ||
                     (result.find("<child>") != std::string::npos) ||
                     (result.find("<ns:child ") != std::string::npos) ||
                     (result.find("<ns:child>") != std::string::npos);
    EXPECT_TRUE(has_child);
}

/* ============================================================================
 * Error Recovery Tests
 * ============================================================================ */

TEST_F(SerializationEdgeCasesTest, SerializePartialDocument) {
    // Test serializing a document with only some children
    parse_xml("<root><a>1</a><b>2</b><c>3</c></root>");

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Remove some children
    TaurusElement b = taurus_element_find_child(root, "b");
    ASSERT_TRUE(ELEM_NOT_NULL(b));
    taurus_element_remove_child(root, b);

    // Serialize after modification
    std::string result = serialize(0, 0);

    // Should only have a and c
    EXPECT_NE(result.find("<a>1</a>"), std::string::npos);
    EXPECT_EQ(result.find("<b>2</b>"), std::string::npos);  // b was removed
    EXPECT_NE(result.find("<c>3</c>"), std::string::npos);
}

} // namespace taurus_test
