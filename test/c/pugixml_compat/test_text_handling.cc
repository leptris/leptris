/* test_text_handling.cpp - Text handling tests from pugixml
 * Copyright (c) 2024, Ribose Inc.
 *
 * Text handling tests adapted from pugixml test_dom_text.cpp
 * Tests text content retrieval from elements, CDATA, mixed content, etc.
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
 * Base class for text handling tests
 */
class TextHandlingTest : public ::testing::Test {
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
};

/* ============================================================================
 * Basic Text Content Tests
 * ============================================================================ */

TEST_F(TextHandlingTest, GetTextFromElement) {
    parse_xml("<node>foo</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "foo");
}

TEST_F(TextHandlingTest, GetTextFromEmptyElement) {
    parse_xml("<node></node>");

    const char* text = taurus_element_text(root);
    // Empty element may return empty string or null depending on implementation
    if (text) {
        EXPECT_STREQ(text, "");
    }
}

TEST_F(TextHandlingTest, GetTextFromSelfClosingElement) {
    parse_xml("<node/>");

    const char* text = taurus_element_text(root);
    // Self-closing element may return empty string or null
    if (text) {
        EXPECT_STREQ(text, "");
    }
}

/* ============================================================================
 * CDATA Text Tests
 * ============================================================================ */

TEST_F(TextHandlingTest, GetTextFromCDATA) {
    parse_xml("<node><![CDATA[bar]]></node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "bar");
}

TEST_F(TextHandlingTest, GetTextFromElementWithCDATA) {
    parse_xml("<node><a><![CDATA[bar]]></a></node>");

    TaurusElement a = taurus_element_find_child(root, "a");
    ASSERT_TRUE(ELEM_NOT_NULL(a));

    const char* text = taurus_element_text(a);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "bar");
}

TEST_F(TextHandlingTest, GetEmptyCDATA) {
    parse_xml("<node><![CDATA[]]></node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "");
}

/* ============================================================================
 * Mixed Content Tests
 * ============================================================================ */

TEST_F(TextHandlingTest, GetTextFromMixedContent) {
    parse_xml("<node>text1<child/>text2</node>");

    // Taurus concatenates all text content
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "text1text2");
}

TEST_F(TextHandlingTest, GetTextFromNestedElement) {
    parse_xml("<node><a>foo</a></node>");

    TaurusElement a = taurus_element_find_child(root, "a");
    ASSERT_TRUE(ELEM_NOT_NULL(a));

    const char* text = taurus_element_text(a);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "foo");
}

TEST_F(TextHandlingTest, GetTextFromNestedElementWithCDATA) {
    parse_xml("<node><b><![CDATA[bar]]></b></node>");

    TaurusElement b = taurus_element_find_child(root, "b");
    ASSERT_TRUE(ELEM_NOT_NULL(b));

    const char* text = taurus_element_text(b);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "bar");
}

/* ============================================================================
 * Whitespace and Special Characters Tests
 * ============================================================================ */

TEST_F(TextHandlingTest, GetTextWithWhitespace) {
    parse_xml("<node>  text  </node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "  text  ");
}

TEST_F(TextHandlingTest, GetTextWithNewlines) {
    parse_xml("<node>line1\nline2</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "line1\nline2");
}

TEST_F(TextHandlingTest, GetTextWithTabs) {
    parse_xml("<node>tab\there</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "tab\there");
}

TEST_F(TextHandlingTest, GetTextWithSpecialChars) {
    parse_xml("<node>&lt;&gt;&amp;&apos;&quot;</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<>&'\"");
}

TEST_F(TextHandlingTest, GetTextWithNumericEntity) {
    parse_xml("<node>&#65;</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "A");
}

TEST_F(TextHandlingTest, GetTextWithHexEntity) {
    parse_xml("<node>&#x41;</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "A");
}

/* ============================================================================
 * Unicode Tests
 * ============================================================================ */

TEST_F(TextHandlingTest, GetTextWithUnicode) {
    parse_xml("<node>テキスト</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "テキスト");
}

TEST_F(TextHandlingTest, GetTextWithEmoji) {
    parse_xml("<node>😀🎉</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "😀🎉");
}

TEST_F(TextHandlingTest, GetTextWithMixedScripts) {
    parse_xml("<node>Hello世界مرحبا</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "Hello世界مرحبا");
}

/* ============================================================================
 * Complex Document Structure Tests
 * ============================================================================ */

TEST_F(TextHandlingTest, GetTextFromDeepNesting) {
    parse_xml("<root><a><b><c>deep text</c></b></a></root>");

    TaurusElement a = taurus_element_find_child(root, "a");
    ASSERT_TRUE(ELEM_NOT_NULL(a));

    TaurusElement b = taurus_element_find_child(a, "b");
    ASSERT_TRUE(ELEM_NOT_NULL(b));

    TaurusElement c = taurus_element_find_child(b, "c");
    ASSERT_TRUE(ELEM_NOT_NULL(c));

    const char* text = taurus_element_text(c);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "deep text");
}

TEST_F(TextHandlingTest, GetTextFromSiblingElements) {
    parse_xml("<root><a>text1</a><b>text2</b><c>text3</c></root>");

    TaurusElement a = taurus_element_find_child(root, "a");
    ASSERT_TRUE(ELEM_NOT_NULL(a));

    TaurusElement b = taurus_element_find_child(root, "b");
    ASSERT_TRUE(ELEM_NOT_NULL(b));

    TaurusElement c = taurus_element_find_child(root, "c");
    ASSERT_TRUE(ELEM_NOT_NULL(c));

    const char* text_a = taurus_element_text(a);
    const char* text_b = taurus_element_text(b);
    const char* text_c = taurus_element_text(c);

    ASSERT_NE(text_a, nullptr);
    ASSERT_NE(text_b, nullptr);
    ASSERT_NE(text_c, nullptr);

    EXPECT_STREQ(text_a, "text1");
    EXPECT_STREQ(text_b, "text2");
    EXPECT_STREQ(text_c, "text3");
}

TEST_F(TextHandlingTest, GetTextFromElementWithAttributes) {
    parse_xml("<node attr='value'>text content</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "text content");

    // Verify attribute doesn't affect text content
    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "value");
}

/* ============================================================================
 * Edge Cases Tests
 * ============================================================================ */

TEST_F(TextHandlingTest, GetVeryLongText) {
    std::string long_text(10000, 'x');
    std::string xml = "<node>" + long_text + "</node>";
    parse_xml(xml);

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(std::string(text), long_text);
}

TEST_F(TextHandlingTest, GetTextWithAllWhitespace) {
    parse_xml("<node>   \t\n  </node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "   \t\n  ");
}

TEST_F(TextHandlingTest, GetTextFromElementWithOnlyComment) {
    parse_xml("<node><!--comment--></node>");

    // Comments don't contribute to text content
    const char* text = taurus_element_text(root);
    // Should be empty or null
    if (text) {
        EXPECT_STREQ(text, "");
    }
}

TEST_F(TextHandlingTest, GetTextFromElementWithPI) {
    parse_xml("<?pi value?><node/>");

    // PI is before root, shouldn't affect root's text
    const char* text = taurus_element_text(root);
    // Should be empty or null
    if (text) {
        EXPECT_STREQ(text, "");
    }
}

/* ============================================================================
 * Attribute Text Tests
 * ============================================================================ */

TEST_F(TextHandlingTest, GetAttributeTextWithEntities) {
    parse_xml("<node attr='&lt;&gt;&amp;'/>");

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "<>&");
}

TEST_F(TextHandlingTest, GetAttributeTextWithWhitespace) {
    parse_xml("<node attr='  test  '/>");

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "  test  ");
}

TEST_F(TextHandlingTest, GetAttributeTextWithNewline) {
    parse_xml("<node attr='line1\nline2'/>");

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "line1\nline2");
}

TEST_F(TextHandlingTest, GetAttributeEmpty) {
    parse_xml("<node attr=''/>");

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "");
}

TEST_F(TextHandlingTest, GetAttributeMissing) {
    parse_xml("<node/>");

    const char* attr = taurus_element_attribute(root, "missing");
    EXPECT_EQ(attr, nullptr);
}

/* ============================================================================
 * Round-trip Tests (parse -> get text -> serialize)
 * ============================================================================ */

TEST_F(TextHandlingTest, RoundtripText) {
    parse_xml("<node>text content</node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "text content");

    // Serialize and verify
    char* serialized = taurus_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    std::string result(serialized);
    taurus_free_string(serialized);

    EXPECT_NE(result.find("text content"), std::string::npos);
}

TEST_F(TextHandlingTest, RoundtripCDATA) {
    parse_xml("<node><![CDATA[CDATA content]]></node>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "CDATA content");

    // Serialize and verify CDATA is preserved
    char* serialized = taurus_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    std::string result(serialized);
    taurus_free_string(serialized);

    EXPECT_NE(result.find("<![CDATA[CDATA content]]>"), std::string::npos);
}

} // namespace taurus_test
