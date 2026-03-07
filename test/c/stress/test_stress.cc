/* test_stress.cc - Stress and limit tests for Taurus XML parser
 * Copyright (c) 2024, Ribose Inc.
 *
 * Based on libxml2/testlimits.c - Tests for parser limits and robustness
 * Tests:
 * - Deeply nested elements (stack depth limits)
 * - Many sibling elements (memory usage)
 * - Long attribute values
 * - Many attributes per element
 * - Deep nesting with many attributes
 * - Entity expansion limits
 * - Namespace handling under stress
 * - Large CDATA sections
 * - Complex mixed content
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <memory>
#include "../../src/include/taurus.h"

namespace taurus_test {

/**
 * Test class for stress and limit testing
 * These tests verify Taurus handles extreme XML structures robustly
 */
class StressTest : public ::testing::Test {
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

    // Parse XML string and return status
    TaurusStatus parse_xml(const std::string& xml) {
        xml_buffer = xml;
        TaurusStatus status;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &status);
        return status;
    }

    // Parse XML and verify success
    bool parse_valid(const std::string& xml) {
        return parse_xml(xml) == TAURUS_OK;
    }

    // Generate string of repeated character
    static std::string repeat_char(char c, size_t count) {
        return std::string(count, c);
    }

    // Generate nested elements
    static std::string nest_elements(const std::string& inner, size_t depth) {
        std::string result = inner;
        for (size_t i = 0; i < depth; i++) {
            result = "<l" + std::to_string(i) + ">" + result + "</l" + std::to_string(i) + ">";
        }
        return result;
    }
};

// ============================================================================
// Deep Nesting Tests
// ============================================================================

TEST_F(StressTest, DeepNesting50) {
    // 50 levels deep - should parse successfully
    std::string xml = "<root>";
    for (int i = 0; i < 50; i++) {
        xml += "<level" + std::to_string(i) + ">";
    }
    xml += "content";
    for (int i = 49; i >= 0; i--) {
        xml += "</level" + std::to_string(i) + ">";
    }
    xml += "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 50-level nested elements";
}

TEST_F(StressTest, DeepNesting100) {
    // 100 levels deep - should parse successfully
    std::string xml = nest_elements("content", 100);
    xml = "<root>" + xml + "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 100-level nested elements";
}

TEST_F(StressTest, DeepNesting500) {
    // 500 levels deep - tests stack depth
    std::string xml = nest_elements("x", 500);
    xml = "<root>" + xml + "</root>";

    TaurusStatus status = parse_xml(xml);
    // Should either succeed or fail gracefully (not crash)
    if (status == TAURUS_OK) {
        ASSERT_NE(doc, nullptr);
        TaurusElement root = taurus_document_root(doc);
        ASSERT_NE(root, nullptr);
    }
}

TEST_F(StressTest, DeepSelfClosingNesting) {
    // Deep nesting with self-closing elements
    std::string xml = "<root>";
    for (int i = 0; i < 100; i++) {
        xml += "<empty" + std::to_string(i) + "/>";
    }
    xml += "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse self-closing elements";
}

// ============================================================================
// Many Siblings Tests
// ============================================================================

TEST_F(StressTest, ManySiblingElements100) {
    // 100 sibling elements
    std::string xml = "<root>";
    for (int i = 0; i < 100; i++) {
        xml += "<sibling id='" + std::to_string(i) + "'/>";
    }
    xml += "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 100 sibling elements";

    if (status == TAURUS_OK) {
        TaurusElement root = taurus_document_root(doc);
        ASSERT_NE(root, nullptr);

        // Verify all children exist
        for (int i = 0; i < 100; i++) {
            TaurusElement child = taurus_element_child(root, i);
            ASSERT_NE(child, nullptr) << "Child " << i << " not found";
        }
    }
}

TEST_F(StressTest, ManySiblingElements1000) {
    // 1000 sibling elements
    std::string xml = "<root>";
    for (int i = 0; i < 1000; i++) {
        xml += "<item/>";
    }
    xml += "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 1000 sibling elements";
}

TEST_F(StressTest, ManySiblingTextNodes) {
    // Many text nodes as siblings
    std::string xml = "<root>";
    for (int i = 0; i < 500; i++) {
        xml += "text" + std::to_string(i) + " ";
    }
    xml += "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 500 text nodes";
}

// ============================================================================
// Long Attribute Values Tests
// ============================================================================

TEST_F(StressTest, LongAttributeValue1K) {
    // 1KB attribute value
    std::string value = repeat_char('a', 1024);
    std::string xml = "<root attr='" + value + "'/>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 1KB attribute value";
}

TEST_F(StressTest, LongAttributeValue10K) {
    // 10KB attribute value
    std::string value = repeat_char('b', 10 * 1024);
    std::string xml = "<root attr='" + value + "'/>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 10KB attribute value";
}

TEST_F(StressTest, LongAttributeValue100K) {
    // 100KB attribute value - stress test
    std::string value = repeat_char('c', 100 * 1024);
    std::string xml = "<root attr='" + value + "'/>";

    TaurusStatus status = parse_xml(xml);
    // Should either succeed or fail gracefully
    if (status == TAURUS_OK) {
        TaurusElement root = taurus_document_root(doc);
        ASSERT_NE(root, nullptr);
        const char* attr = taurus_element_attribute(root, "attr");
        ASSERT_NE(attr, nullptr);
    }
}

TEST_F(StressTest, ManyLongAttributes) {
    // Many attributes with long values
    std::string xml = "<root ";
    for (int i = 0; i < 20; i++) {
        std::string value = repeat_char('x', 1000);
        xml += "attr" + std::to_string(i) + "='" + value + "' ";
    }
    xml += "/>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse many long attributes";
}

// ============================================================================
// Many Attributes Tests
// ============================================================================

TEST_F(StressTest, ManyAttributes10) {
    // 10 attributes
    std::string xml = "<root a1='v1' a2='v2' a3='v3' a4='v4' a5='v5' "
                      "a6='v6' a7='v7' a8='v8' a9='v9' a10='v10'/>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 10 attributes";
}

TEST_F(StressTest, ManyAttributes50) {
    // 50 attributes
    std::string xml = "<root ";
    for (int i = 0; i < 50; i++) {
        xml += "attr" + std::to_string(i) + "='value' ";
    }
    xml += "/>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 50 attributes";
}

TEST_F(StressTest, ManyAttributes100) {
    // 100 attributes
    std::string xml = "<root ";
    for (int i = 0; i < 100; i++) {
        xml += "attr" + std::to_string(i) + "='value" + std::to_string(i) + "' ";
    }
    xml += "/>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 100 attributes";
}

TEST_F(StressTest, ManyAttributesWithNamespaces) {
    // Attributes with namespace prefixes
    std::string xml = "<root "
                      "xmlns:a='urn:a' "
                      "xmlns:b='urn:b' "
                      "a:attr1='val1' a:attr2='val2' a:attr3='val3' "
                      "b:attr1='val1' b:attr2='val2' b:attr3='val3' "
                      "attr='plain'/>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse namespaced attributes";
}

// ============================================================================
// Long Element Names Tests
// ============================================================================

TEST_F(StressTest, LongElementName100) {
    // 100 character element name
    std::string name = "element_with_very_long_name_that_exceeds_normal_length_"
                       "for_element_names_in_xml_documents_" + repeat_char('x', 20);
    std::string xml = "<" + name + "/>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse long element name";
}

TEST_F(StressTest, LongElementName1000) {
    // 1000 character element name - stress test
    std::string name = repeat_char('n', 1000);
    std::string xml = "<" + name + "/>";

    TaurusStatus status = parse_xml(xml);
    // Should not crash
    if (status == TAURUS_OK) {
        TaurusElement root = taurus_document_root(doc);
        ASSERT_NE(root, nullptr);
        const char* elem_name = taurus_element_name(root);
        ASSERT_NE(elem_name, nullptr);
    }
}

// ============================================================================
// Long Text Content Tests
// ============================================================================

TEST_F(StressTest, LongTextContent1K) {
    // 1KB text content
    std::string content = repeat_char('t', 1024);
    std::string xml = "<root>" + content + "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 1KB text content";
}

TEST_F(StressTest, LongTextContent10K) {
    // 10KB text content
    std::string content = repeat_char('x', 10 * 1024);
    std::string xml = "<root>" + content + "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 10KB text content";
}

TEST_F(StressTest, LongTextContent100K) {
    // 100KB text content - stress test
    std::string content = repeat_char('y', 100 * 1024);
    std::string xml = "<root>" + content + "</root>";

    TaurusStatus status = parse_xml(xml);
    if (status == TAURUS_OK) {
        TaurusElement root = taurus_document_root(doc);
        ASSERT_NE(root, nullptr);
        const char* text = taurus_element_text(root);
        ASSERT_NE(text, nullptr);
        // Verify text length
        EXPECT_EQ(strlen(text), 100 * 1024u) << "Text content length mismatch";
    }
}

// ============================================================================
// Long CDATA Section Tests
// ============================================================================

TEST_F(StressTest, LongCData1K) {
    // 1KB CDATA section
    std::string content = repeat_char('c', 1024);
    std::string xml = "<root><![CDATA[" + content + "]]></root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 1KB CDATA";
}

TEST_F(StressTest, LongCData10K) {
    // 10KB CDATA section
    std::string content = repeat_char('d', 10 * 1024);
    std::string xml = "<root><![CDATA[" + content + "]]></root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 10KB CDATA";
}

TEST_F(StressTest, LongCData100K) {
    // 100KB CDATA section - stress test
    std::string content = repeat_char('e', 100 * 1024);
    std::string xml = "<root><![CDATA[" + content + "]]></root>";

    TaurusStatus status = parse_xml(xml);
    if (status == TAURUS_OK) {
        TaurusElement root = taurus_document_root(doc);
        ASSERT_NE(root, nullptr);
        const char* text = taurus_element_text(root);
        ASSERT_NE(text, nullptr);
    }
}

// ============================================================================
// Long Comment Tests
// ============================================================================

TEST_F(StressTest, LongComment1K) {
    // 1KB comment
    std::string content = repeat_char('c', 1024);
    std::string xml = "<root><!--" + content + "--></root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 1KB comment";
}

TEST_F(StressTest, LongComment10K) {
    // 10KB comment
    std::string content = repeat_char('m', 10 * 1024);
    std::string xml = "<root><!--" + content + "--></root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 10KB comment";
}

// ============================================================================
// Long Processing Instruction Tests
// ============================================================================

TEST_F(StressTest, LongPI1K) {
    // 1KB processing instruction
    std::string content = repeat_char('p', 1024);
    std::string xml = "<?target " + content + "?><root/>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 1KB PI";
}

TEST_F(StressTest, LongPI10K) {
    // 10KB processing instruction
    std::string content = repeat_char('i', 10 * 1024);
    std::string xml = "<?target " + content + "?><root/>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse 10KB PI";
}

// ============================================================================
// Entity Expansion Tests
// ============================================================================

TEST_F(StressTest, ManyEntityReferences) {
    // Many entity references in content
    std::string xml = "<root>&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;"
                      "&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;"
                      "&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;"
                      "&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;"
                      "&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse many entity references";
}

TEST_F(StressTest, NestedEntityExpansion) {
    // Nested entity definitions
    std::string xml = R"(<?xml version="1.0"?>
<!DOCTYPE root [
  <!ENTITY a "1234567890">
  <!ENTITY b "&a;&a;&a;&a;&a;">
  <!ENTITY c "&b;&b;&b;&b;&b;">
  <!ENTITY d "&c;&c;&c;&c;&c;">
]>
<root>&d;&d;&d;</root>)";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse nested entities";
}

TEST_F(StressTest, LargeEntityExpansion) {
    // Large entity that expands to significant content
    std::string entity_content = repeat_char('x', 100);
    std::string xml = "<?xml version=\"1.0\"?>\n"
                      "<!DOCTYPE root [\n"
                      "  <!ENTITY large \"" + entity_content + "\">\n"
                      "]>\n"
                      "<root>&large;&large;&large;&large;&large;</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse large entity expansion";
}

// ============================================================================
// Namespace Stress Tests
// ============================================================================

TEST_F(StressTest, ManyNamespaces) {
    // Many namespace declarations
    std::string xml = "<root ";
    for (int i = 0; i < 20; i++) {
        xml += "xmlns:ns" + std::to_string(i) + "='urn:ns" + std::to_string(i) + "' ";
    }
    xml += "><ns0:child xmlns:ns0='urn:ns0'/></root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse many namespaces";
}

TEST_F(StressTest, NestedNamespaces) {
    // Deep nesting with namespace declarations at each level
    std::string xml = "<root xmlns:a='urn:a'>";
    for (int i = 0; i < 50; i++) {
        xml += "<l" + std::to_string(i) + " xmlns:a='urn:a" + std::to_string(i) + "'>";
    }
    xml += "content";
    for (int i = 49; i >= 0; i--) {
        xml += "</l" + std::to_string(i) + ">";
    }
    xml += "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse nested namespaces";
}

TEST_F(StressTest, DefaultNamespaceStress) {
    // Many elements with default namespace
    std::string xml = "<root xmlns='urn:default'>";
    for (int i = 0; i < 100; i++) {
        xml += "<item" + std::to_string(i) + " attr='value" + std::to_string(i) + "'/>";
    }
    xml += "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse default namespace stress";
}

// ============================================================================
// Complex Mixed Content Tests
// ============================================================================

TEST_F(StressTest, ComplexMixedContent) {
    // Complex mixed content with elements, text, comments, PIs, CDATA
    std::string xml = "<?xml version='1.0'?>\n";
    xml += "<!-- comment before -->\n";
    xml += "<?pi before?>\n";
    xml += "<root xmlns:ns='urn:ns' ns:attr='value'>\n";
    xml += "  <!-- comment in content -->\n";
    xml += "  <?pi in content?>\n";
    xml += "  text1\n";
    xml += "  <child1 id='1'><![CDATA[cdata content]]></child1>\n";
    xml += "  text2\n";
    xml += "  <child2 id='2'><nested/></child2>\n";
    xml += "  text3\n";
    xml += "  <child3 id='3' ns:attr='nsvalue'/>\n";
    xml += "  text4\n";
    xml += "</root>\n";
    xml += "<!-- comment after -->\n";
    xml += "<?pi after?>\n";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse complex mixed content";
}

TEST_F(StressTest, AlternatingContent) {
    // Alternating text and elements
    std::string xml = "<root>";
    for (int i = 0; i < 100; i++) {
        xml += "text" + std::to_string(i) + "<item" + std::to_string(i) + "/>";
    }
    xml += "final_text</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse alternating content";
}

// ============================================================================
// DOCTYPE Stress Tests
// ============================================================================

TEST_F(StressTest, ComplexDOCTYPE) {
    // Complex DOCTYPE with many entities
    std::string xml = R"(<?xml version="1.0"?>
<!DOCTYPE root [
  <!ENTITY e1 "val1">
  <!ENTITY e2 "val2">
  <!ENTITY e3 "val3">
  <!ENTITY e4 "val4">
  <!ENTITY e5 "val5">
  <!ENTITY e6 "val6">
  <!ENTITY e7 "val7">
  <!ENTITY e8 "val8">
  <!ENTITY e9 "val9">
  <!ENTITY e10 "val10">
  <!ENTITY combined "&e1;&e2;&e3;&e4;&e5;">
]>
<root>&combined;&e6;&e7;&e8;&e9;&e10;</root>)";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse complex DOCTYPE";
}

TEST_F(StressTest, DOCTYPEWithElementDeclarations) {
    // DOCTYPE with element declarations
    std::string xml = R"(<?xml version="1.0"?>
<!DOCTYPE root [
  <!ELEMENT root (child+)>
  <!ELEMENT child (#PCDATA|emph)*>
  <!ELEMENT emph (#PCDATA)>
]>
<root><child>text<emph>emphasized</emph>more</child></root>)";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse DOCTYPE with element declarations";
}

// ============================================================================
// Recursive Structure Tests
// ============================================================================

TEST_F(StressTest, RecursiveStructure) {
    // Recursively nested structure (similar to linked list)
    std::string xml = "<level0>";
    for (int i = 1; i < 100; i++) {
        xml += "<level" + std::to_string(i) + ">";
    }
    xml += "deep_content";
    for (int i = 99; i >= 0; i--) {
        xml += "</level" + std::to_string(i) + ">";
    }
    xml += "</level0>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse recursive structure";
}

TEST_F(StressTest, WideAndDeepStructure) {
    // Wide at top, deep at bottom
    std::string xml = "<root>";
    for (int i = 0; i < 20; i++) {
        xml += "<sibling" + std::to_string(i) + ">";
        // Deep nesting within each sibling
        for (int j = 0; j < 10; j++) {
            xml += "<nested" + std::to_string(j) + ">";
        }
        xml += "content";
        for (int j = 9; j >= 0; j--) {
            xml += "</nested" + std::to_string(j) + ">";
        }
        xml += "</sibling" + std::to_string(i) + ">";
    }
    xml += "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse wide and deep structure";
}

// ============================================================================
// Special Character Stress Tests
// ============================================================================

TEST_F(StressTest, ManySpecialCharsInContent) {
    // Content with many special XML characters
    std::string content;
    for (int i = 0; i < 1000; i++) {
        content += "<tag attr='value'>text &amp; more</tag>";
    }
    std::string xml = "<root>" + content + "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse many special chars";
}

TEST_F(StressTest, MixedQuotes) {
    // Attributes with mixed quote styles
    std::string xml = "<root ";
    for (int i = 0; i < 50; i++) {
        if (i % 2 == 0) {
            xml += "attr" + std::to_string(i) + "='value" + std::to_string(i) + "' ";
        } else {
            xml += "attr" + std::to_string(i) + "=\"value" + std::to_string(i) + "\" ";
        }
    }
    xml += "/>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse mixed quote styles";
}

// ============================================================================
// Empty/Minimal Structure Tests
// ============================================================================

TEST_F(StressTest, EmptyElementsChain) {
    // Chain of empty elements
    std::string xml = "<root>";
    for (int i = 0; i < 500; i++) {
        xml += "<empty" + std::to_string(i) + "/>";
    }
    xml += "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse empty element chain";
}

TEST_F(StressTest, SelfClosingWithEverything) {
    // Self-closing element with namespace and attributes
    std::string xml = "<root>";
    for (int i = 0; i < 100; i++) {
        xml += "<item" + std::to_string(i)
               + " xmlns:ns='urn:ns" + std::to_string(i) + "'"
               + " ns:attr" + std::to_string(i) + "='value" + std::to_string(i) + "'"
               + " id='" + std::to_string(i) + "'"
               + "/>";
    }
    xml += "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse self-closing with namespace";
}

// ============================================================================
// Memory/Performance Stress Tests
// ============================================================================

TEST_F(StressTest, LargeBalancedDocument) {
    // Large balanced document for memory testing
    std::string xml = "<root>";
    for (int i = 0; i < 100; i++) {
        xml += "<group id='" + std::to_string(i) + "'>";
        for (int j = 0; j < 10; j++) {
            xml += "<item id='" + std::to_string(i) + "-" + std::to_string(j)
                   + "' value='val" + std::to_string(i) + std::to_string(j) + "'>";
            xml += "content for item " + std::to_string(i) + "-" + std::to_string(j);
            xml += "</item>";
        }
        xml += "</group>";
    }
    xml += "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse large balanced document";
}

TEST_F(StressTest, DocumentWithAllNodeTypes) {
    // Document containing all node types in sequence
    std::string xml = "<?xml version='1.0'?>\n";
    xml += "<!-- comment 1 -->\n";
    xml += "<?pi1 data?>\n";
    xml += "<root>\n";
    xml += "  <!-- comment 2 -->\n";
    xml += "  <?pi2 data?>\n";
    xml += "  text content\n";
    xml += "  <![CDATA[cdata section]]>\n";
    xml += "  <child1/>\n";
    xml += "  more text\n";
    xml += "  <child2 attr='value'/>\n";
    xml += "  final text\n";
    xml += "</root>\n";
    xml += "<!-- comment 3 -->\n";
    xml += "<?pi3 data?>\n";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse document with all node types";
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(StressTest, WhitespaceOnlyContent) {
    // Large whitespace-only content
    std::string content = repeat_char(' ', 10000);
    std::string xml = "<root>" + content + "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse whitespace-only content";
}

TEST_F(StressTest, NewlineOnlyContent) {
    // Large newline-only content
    std::string content = repeat_char('\n', 10000);
    std::string xml = "<root>" + content + "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse newline-only content";
}

TEST_F(StressTest, MixedWhitespaceContent) {
    // Mixed whitespace content
    std::string content;
    for (int i = 0; i < 1000; i++) {
        content += " \t\n\r  ";
    }
    std::string xml = "<root>" + content + "</root>";

    TaurusStatus status = parse_xml(xml);
    EXPECT_EQ(status, TAURUS_OK) << "Failed to parse mixed whitespace content";
}

} // namespace taurus_test
