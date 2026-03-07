/* test_edge_cases_advanced.cpp - Advanced edge case tests from pugixml and libxml2
 * Copyright (c) 2024, Ribose Inc.
 *
 * Advanced edge case tests for XML parsing edge cases
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
 * Base class for edge case tests
 */
class EdgeCaseAdvancedTest : public ::testing::Test {
protected:
    TaurusDocument doc;
    TaurusElement root;
    std::string xml_buffer;

    void SetUp() override {
        doc = nullptr;
        root = ELEM_NULL();
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
        ASSERT_TRUE(ELEM_NOT_NULL(root));
    }

    std::string serialize() {
        char* xml = taurus_document_serialize(doc, NULL);
        std::string result(xml ? xml : "");
        if (xml) taurus_free_string(xml);
        return result;
    }
};

/* ============================================================================
 * Attribute Value Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseAdvancedTest, EmptyAttributeValue) {
    parse_xml("<node attr=''>text</node>");

    const char* value = taurus_element_attribute(root, "attr");
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "");
}

TEST_F(EdgeCaseAdvancedTest, AttributeWithSpecialChars) {
    parse_xml("<node attr='&lt;&gt;&amp;&apos;&quot;'>text</node>");

    const char* value = taurus_element_attribute(root, "attr");
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "<>&'\"");
}

TEST_F(EdgeCaseAdvancedTest, AttributeWithWhitespace) {
    parse_xml("<node attr='  test  '>text</node>");

    const char* value = taurus_element_attribute(root, "attr");
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "  test  ");
}

TEST_F(EdgeCaseAdvancedTest, AttributeWithNewlines) {
    parse_xml("<node attr='line1\nline2'>text</node>");

    const char* value = taurus_element_attribute(root, "attr");
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "line1\nline2");
}

TEST_F(EdgeCaseAdvancedTest, AttributeWithTabs) {
    parse_xml("<node attr='tab\there'>text</node>");

    const char* value = taurus_element_attribute(root, "attr");
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "tab\there");
}

/* ============================================================================
 * Text Content Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseAdvancedTest, TextWithAllWhitespace) {
    parse_xml("<node>   \t\n  </node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "   \t\n  ");
}

TEST_F(EdgeCaseAdvancedTest, EmptyText) {
    parse_xml("<node></node>");

    const char* text = taurus_element_text(root);
    // Empty element may return empty string or null
    if (text) {
        EXPECT_STREQ(text, "");
    }
}

TEST_F(EdgeCaseAdvancedTest, TextWithEntities) {
    parse_xml("<node>&lt;&gt;&amp;&apos;&quot;</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<>&'\"");
}

TEST_F(EdgeCaseAdvancedTest, TextWithNumericEntities) {
    parse_xml("<node>&#65;</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "A");
}

TEST_F(EdgeCaseAdvancedTest, TextWithHexEntities) {
    parse_xml("<node>&#x41;</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "A");
}

/* ============================================================================
 * CDATA Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseAdvancedTest, CDATAWithSpecialChars) {
    parse_xml("<node><![CDATA[<>&'\"]]></node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<>&'\"");
}

TEST_F(EdgeCaseAdvancedTest, CDATAWithNewlines) {
    parse_xml("<node><![CDATA[line1\nline2]]></node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "line1\nline2");
}

TEST_F(EdgeCaseAdvancedTest, EmptyCDATA) {
    parse_xml("<node><![CDATA[]]></node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "");
}

/* ============================================================================
 * Comment Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseAdvancedTest, CommentWithSpecialChars) {
    parse_xml("<node><!-- <>&'\" --></node>");

    // Comments should be preserved but not part of text content
    const char* text = taurus_element_text(root);
    if (text) {
        EXPECT_STREQ(text, "");
    }
}

TEST_F(EdgeCaseAdvancedTest, CommentWithNewlines) {
    parse_xml("<node><!-- line1\nline2 --></node>");

    const char* text = taurus_element_text(root);
    if (text) {
        EXPECT_STREQ(text, "");
    }
}

/* ============================================================================
 * Mixed Content Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseAdvancedTest, TextElementTextElement) {
    parse_xml("<node>text1<n1/>text2<n2/>text3</node>");

    // Taurus concatenates all text content
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "text1text2text3");
}

TEST_F(EdgeCaseAdvancedTest, CdataElementsMixed) {
    parse_xml("<node><![CDATA[cdata1]]><n1/><![CDATA[cdata2]]></node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "cdata1cdata2");
}

/* ============================================================================
 * Deep Nesting Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseAdvancedTest, DeepNesting) {
    parse_xml("<root><a><b><c><d><e>deep</e></d></c></b></a></root>");

    TaurusElement a = taurus_element_find_child(root, "a");
    ASSERT_TRUE(ELEM_NOT_NULL(a));

    TaurusElement b = taurus_element_find_child(a, "b");
    ASSERT_TRUE(ELEM_NOT_NULL(b));

    TaurusElement c = taurus_element_find_child(b, "c");
    ASSERT_TRUE(ELEM_NOT_NULL(c));

    TaurusElement d = taurus_element_find_child(c, "d");
    ASSERT_TRUE(ELEM_NOT_NULL(d));

    TaurusElement e = taurus_element_find_child(d, "e");
    ASSERT_TRUE(ELEM_NOT_NULL(e));

    const char* text = taurus_element_text(e);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "deep");
}

TEST_F(EdgeCaseAdvancedTest, DeepNestingWithAttributes) {
    parse_xml("<root id='1'><a id='2'><b id='3'><c id='4'><d id='5'><e id='6'>deep</e></d></c></b></a></root>");

    // Navigate through deep nesting
    TaurusElement e = taurus_element_root(root);
    e = taurus_element_find_child(e, "a");
    e = taurus_element_find_child(e, "b");
    e = taurus_element_find_child(e, "c");
    e = taurus_element_find_child(e, "d");
    e = taurus_element_find_child(e, "e");

    ASSERT_TRUE(ELEM_NOT_NULL(e));
    EXPECT_STREQ(taurus_element_text(e), "deep");

    const char* attr = taurus_element_attribute(e, "id");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "6");
}

/* ============================================================================
 * Many Siblings Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseAdvancedTest, ManySiblings) {
    // Create XML with many siblings
    std::string xml = "<node>";
    for (int i = 0; i < 100; i++) {
        xml += "<n" + std::to_string(i) + "/>";
    }
    xml += "</node>";

    parse_xml(xml);

    EXPECT_EQ(taurus_element_child_count(root), 100);
}

TEST_F(EdgeCaseAdvancedTest, ManyAttributes) {
    // Create XML with many attributes
    std::string xml = "<node";
    for (int i = 0; i < 50; i++) {
        xml += " attr" + std::to_string(i) + "='value" + std::to_string(i) + "'";
    }
    xml += ">text</node>";

    parse_xml(xml);

    // Check that we can retrieve attributes
    for (int i = 0; i < 50; i++) {
        std::string attr_name = "attr" + std::to_string(i);
        const char* value = taurus_element_attribute(root, attr_name.c_str());
        ASSERT_NE(value, nullptr) << "Failed to get attribute: " << attr_name;
    }
}

/* ============================================================================
 * Unicode Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseAdvancedTest, UTF8Japanese) {
    parse_xml("<node>テキスト</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "テキスト");
}

TEST_F(EdgeCaseAdvancedTest, UTF8Emoji) {
    parse_xml("<node>😀🎉🚀</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "😀🎉🚀");
}

TEST_F(EdgeCaseAdvancedTest, UTF8MixedScripts) {
    parse_xml("<node>Hello世界مرحبا</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "Hello世界مرحبا");
}

TEST_F(EdgeCaseAdvancedTest, UTF8InAttributes) {
    parse_xml("<node attr='テキスト'>text</node>");

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "テキスト");
}

/* ============================================================================
 * Serialization Round-Trip Tests
 * ============================================================================ */

TEST_F(EdgeCaseAdvancedTest, RoundTripSimple) {
    parse_xml("<node attr='value'>text content</node>");

    std::string serialized = serialize();
    EXPECT_NE(serialized.find("<node"), std::string::npos);
    EXPECT_NE(serialized.find("attr=\"value\""), std::string::npos);
    EXPECT_NE(serialized.find(">text content<"), std::string::npos);
}

TEST_F(EdgeCaseAdvancedTest, RoundTripCDATA) {
    parse_xml("<node><![CDATA[cdata content]]></node>");

    std::string serialized = serialize();
    EXPECT_NE(serialized.find("<![CDATA["), std::string::npos);
    EXPECT_NE(serialized.find("cdata content"), std::string::npos);
}

TEST_F(EdgeCaseAdvancedTest, RoundTripWithEntities) {
    parse_xml("<node>&lt;tag&gt;&amp;</node>");

    std::string serialized = serialize();
    EXPECT_NE(serialized.find("&lt;"), std::string::npos);
    EXPECT_NE(serialized.find("tag&gt;"), std::string::npos);
    EXPECT_NE(serialized.find("&amp;"), std::string::npos);
}

/* ============================================================================
 * Name Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseAdvancedTest, LongElementName) {
    std::string long_name(1000, 'a');
    std::string xml = "<" + long_name + ">text</" + long_name + ">";

    parse_xml(xml);

    const char* name = taurus_element_name(root);
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(std::string(name), long_name);
}

TEST_F(EdgeCaseAdvancedTest, LongAttributeName) {
    std::string long_attr(1000, 'b');
    std::string xml = "<node " + long_attr + "='value'>text</node>";

    parse_xml(xml);

    const char* value = taurus_element_attribute(root, long_attr.c_str());
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "value");
}

/* ============================================================================
 * Complex Document Structure
 * ============================================================================ */

TEST_F(EdgeCaseAdvancedTest, ComplexHierarchy) {
    parse_xml(
        "<root>"
        "  <level1 attr1='1'>"
        "    <level2 attr2='2'>"
        "      <level3>text1</level3>"
        "      <level3>text2</level3>"
        "    </level2>"
        "    <level2>"
        "      <level4>deep</level4>"
        "    </level2>"
        "  </level1>"
        "  <level1>"
        "    <level5><level6>deepest</level6></level5>"
        "  </level1>"
        "</root>"
    );

    // Test navigation through the hierarchy
    TaurusElement level1_1 = taurus_element_first_child(root, "level1");
    ASSERT_TRUE(ELEM_NOT_NULL(level1_1));

    // level3 is a child of level2, not level1
    TaurusElement level2_1 = taurus_element_first_child(level1_1, "level2");
    ASSERT_TRUE(ELEM_NOT_NULL(level2_1));

    TaurusElement level3 = taurus_element_first_child(level2_1, "level3");
    ASSERT_TRUE(ELEM_NOT_NULL(level3));

    // Get second level3 (sibling of first)
    TaurusElement level3_2 = taurus_element_next_sibling(level3, "level3");
    ASSERT_TRUE(ELEM_NOT_NULL(level3_2));

    // Navigate to root from level3_2
    TaurusElement root_elem = taurus_element_root(level3_2);
    ASSERT_TRUE(ELEM_NOT_NULL(root_elem));

    // Find the second level1 element (which contains level5)
    TaurusElement level1_2 = taurus_element_next_sibling(level1_1, "level1");
    ASSERT_TRUE(ELEM_NOT_NULL(level1_2));

    TaurusElement level5 = taurus_element_find_child(level1_2, "level5");
    ASSERT_TRUE(ELEM_NOT_NULL(level5));

    TaurusElement level6 = taurus_element_find_child(level5, "level6");
    ASSERT_TRUE(ELEM_NOT_NULL(level6));

    EXPECT_STREQ(taurus_element_text(level6), "deepest");
}

} // namespace taurus_test
