/* test_parse_edge_cases.cpp - Edge case parsing tests from pugixml
 * Copyright (c) 2024, Ribose Inc.
 *
 * Additional edge case tests adapted from pugixml test_parse.cpp
 * Focus on error cases and entity handling that don't require parse flags
 */

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "../../src/include/taurus.h"
#include "../../src/taurus/dom/element.h"

namespace taurus_test {

/**
 * Helper macros for TaurusElement assertions
 */
#define ASSERT_ELEM_NOT_NULL(elem) ASSERT_TRUE(!taurus_element_is_null((elem)))

/**
 * Base class for edge case parse tests
 */
class EdgeCaseParseTest : public ::testing::Test {
protected:
    TaurusDocument doc;
    TaurusElement root;
    std::string xml_buffer;

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
        xml_buffer.clear();
    }

    void parse_xml(const std::string& xml) {
        xml_buffer = xml;
        TaurusStatus status;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &status);
        if (doc && status == TAURUS_OK) {
            root = taurus_document_root(doc);
        }
    }

    void parse_xml_fail(const std::string& xml, TaurusStatus expected_status = TAURUS_ERROR_PARSE) {
        xml_buffer = xml;
        TaurusStatus status;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &status);
        // Should fail
        EXPECT_TRUE(!doc || status != TAURUS_OK);
    }
};

/* ============================================================================
 * Processing Instruction Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseParseTest, ParsePIWithSpaces) {
    // PI with normalized whitespace
    parse_xml("<?pi  \r\n\t  value ?><root/>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);
    EXPECT_STREQ(taurus_element_name(root), "root");
}

TEST_F(EdgeCaseParseTest, ParsePIValueWithSpaces) {
    // PI value should preserve trailing space
    parse_xml("<?pi value ?><root/>");
    ASSERT_NE(doc, nullptr);
}

/* ============================================================================
 * Comment Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseParseTest, ParseCommentWithNewlines) {
    // Comments preserve line endings
    parse_xml("<!--\r\rval1\rval2\r\nval3\nval4\r\r--><root/>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);
}

TEST_F(EdgeCaseParseTest, ParseCommentWithDoubleDash) {
    // Invalid: comments should not contain -- except at end
    parse_xml_fail("<!-- <!-- --><!- -->");
}

TEST_F(EdgeCaseParseTest, ParseEmptyComment) {
    parse_xml("<!----><root/>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);
}

TEST_F(EdgeCaseParseTest, ParseCommentWithValue) {
    parse_xml("<!--value--><root/>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);
}

/* ============================================================================
 * CDATA Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseParseTest, ParseEmptyCDATA) {
    parse_xml("<root><![CDATA[]]></root>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "");
}

TEST_F(EdgeCaseParseTest, ParseCDATAWithNewlines) {
    parse_xml("<root><![CDATA[\r\rval1\rval2\r\nval3\nval4\r\r]]></root>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    // CDATA preserves content exactly
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(std::string(text), std::string("\r\rval1\rval2\r\nval3\nval4\r\r"));
}

/* ============================================================================
 * Entity and Escape Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseParseTest, ParseNumericEntityDecimal) {
    parse_xml("<root>&#65;</root>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "A");
}

TEST_F(EdgeCaseParseTest, ParseNumericEntityHex) {
    parse_xml("<root>&#x41;</root>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "A");
}

TEST_F(EdgeCaseParseTest, ParseNumericEntityUnicode) {
    // Unicode character: Greek letter gamma (U+03B3)
    parse_xml("<root>&#x03B3;</root>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    // UTF-8 encoding of U+03B3 is 0xCE 0xB3
    EXPECT_EQ(text[0], '\xce');
    EXPECT_EQ(text[1], '\xb3');
    EXPECT_EQ(text[2], '\0');
}

TEST_F(EdgeCaseParseTest, ParseNumericEntityHighSurrogate) {
    // High surrogate: U+24B62 (needs surrogate pair in UTF-16)
    parse_xml("<root>&#x24B62;</root>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    // UTF-8 encoding of U+24B62 is 0xF0 0xA4 0xAD 0xA2
    EXPECT_EQ((unsigned char)text[0], 0xf0);
    EXPECT_EQ((unsigned char)text[1], 0xa4);
    EXPECT_EQ((unsigned char)text[2], 0xad);
    EXPECT_EQ((unsigned char)text[3], 0xa2);
    EXPECT_EQ(text[4], '\0');
}

TEST_F(EdgeCaseParseTest, ParseInvalidHexEntity) {
    // Invalid hex digit - strict mode rejects the entire document
    parse_xml_fail("<root>&#x03g;</root>");
}

TEST_F(EdgeCaseParseTest, ParseIncompleteNumericEntity) {
    // Missing semicolon - may be preserved or cause error
    parse_xml_fail("<root>&#65</root>");
}

TEST_F(EdgeCaseParseTest, ParseEmptyNumericEntity) {
    // Empty entity reference - strict mode rejects the entire document
    parse_xml_fail("<root>&#;</root>");
}

TEST_F(EdgeCaseParseTest, ParseMissingEntityNumber) {
    // Missing number after hex indicator - strict mode rejects the entire document
    parse_xml_fail("<root>&#x;</root>");
}

TEST_F(EdgeCaseParseTest, ParseNegativeNumericEntity) {
    // Negative numbers are not valid in entities - strict mode rejects
    parse_xml_fail("<root>&#-1;</root>");
}

TEST_F(EdgeCaseParseTest, ParsePartialEntityEscape) {
    // Partial/unknown entity names - strict mode rejects
    parse_xml_fail("<root>&q &qu &quo &quot</root>");
}

TEST_F(EdgeCaseParseTest, ParseAllStandardEntities) {
    parse_xml("<root>&lt;&gt;&amp;&apos;&quot;</root>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<>&'\"");
}

TEST_F(EdgeCaseParseTest, ParseEntityInAttribute) {
    parse_xml("<root attr=\"&lt;&gt;&amp;&apos;&quot;\"/>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "<>&'\"");
}

TEST_F(EdgeCaseParseTest, ParseNumericEntityInAttribute) {
    parse_xml("<root attr=\"&#65;&#x41;\"/>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "AA");
}

TEST_F(EdgeCaseParseTest, ParseMixedEntitiesInAttribute) {
    parse_xml("<root attr=\"test &lt; &#65; &amp;\"/>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "test < A &");
}

/* ============================================================================
 * Whitespace Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseParseTest, ParseAllWhitespaceContent) {
    parse_xml("<root>   </root>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "   ");
}

TEST_F(EdgeCaseParseTest, ParseTabNewlineContent) {
    parse_xml("<root>\t\n\r \t</root>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "\t\n\r \t");
}

TEST_F(EdgeCaseParseTest, ParseZeroWidthSpace) {
    // Zero-width space (U+200B)
    parse_xml("<root>\xe2\x80\x8b</root>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "\xe2\x80\x8b");
}

/* ============================================================================
 * Tag Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseParseTest, ParseSelfClosingTag) {
    parse_xml("<root/>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);
    EXPECT_STREQ(taurus_element_name(root), "root");
}

TEST_F(EdgeCaseParseTest, ParseSelfClosingTagWithSpace) {
    parse_xml("<root />");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);
    EXPECT_STREQ(taurus_element_name(root), "root");
}

TEST_F(EdgeCaseParseTest, ParseSelfClosingTagWithSpaces) {
    parse_xml("<root   />");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);
    EXPECT_STREQ(taurus_element_name(root), "root");
}

TEST_F(EdgeCaseParseTest, ParseTagWithNamespacePrefix) {
    parse_xml("<ns:root xmlns:ns=\"http://example.com\"/>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);
    // Taurus returns local name only
    EXPECT_STREQ(taurus_element_name(root), "root");
}

/* ============================================================================
 * Attribute Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseParseTest, ParseAttributeWithSingleQuote) {
    parse_xml("<root attr='value'/>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "value");
}

TEST_F(EdgeCaseParseTest, ParseAttributeWithNoValue) {
    parse_xml_fail("<root attr/>");
}

TEST_F(EdgeCaseParseTest, ParseAttributeWithEmptyValue) {
    parse_xml("<root attr=\"\"/>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "");
}

TEST_F(EdgeCaseParseTest, ParseAttributeWithWhitespaceInValue) {
    parse_xml("<root attr=\"  test  \"/>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "  test  ");
}

TEST_F(EdgeCaseParseTest, ParseAttributeWithLineBreakInValue) {
    parse_xml("<root attr=\"line1\nline2\"/>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "line1\nline2");
}

TEST_F(EdgeCaseParseTest, ParseAttributeWithTabInValue) {
    parse_xml("<root attr=\"tab\tseparated\"/>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "tab\tseparated");
}

/* ============================================================================
 * Mixed Content Edge Cases
 * ============================================================================ */

TEST_F(EdgeCaseParseTest, ParseTextElementText) {
    parse_xml("<root>text1<child/>text2</root>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    // Taurus concatenates text content
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "text1text2");
}

TEST_F(EdgeCaseParseTest, ParseTextCommentText) {
    parse_xml("<root>text1<!--comment-->text2</root>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    // Comments don't contribute to text content
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "text1text2");
}

TEST_F(EdgeCaseParseTest, ParseMultipleElementsWithText) {
    parse_xml("<root>a<child1/>b<child2/>c</root>");
    ASSERT_NE(doc, nullptr);
    ASSERT_ELEM_NOT_NULL(root);

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "abc");
}

} // namespace taurus_test
