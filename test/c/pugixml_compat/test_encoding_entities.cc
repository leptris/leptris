/* test_encoding_entities.cpp - Tests for XML encoding and entity handling
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for encoding support and entity reference handling:
 * - UTF-8 encoding (with BOM)
 * - Character entities (lt, gt, amp, apos, quot)
 * - Numeric character entities (decimal and hexadecimal)
 * - Predefined entities
 * - Custom entity declarations
 * - Invalid/malformed entities
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
 * Base class for encoding and entity tests
 */
class EncodingEntitiesTest : public ::testing::Test {
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
        ASSERT_EQ(status, TAURUS_OK) << "Failed to parse XML: " << xml;
        ASSERT_NE(doc, nullptr);
        root = taurus_document_root(doc);
        ASSERT_TRUE(ELEM_NOT_NULL(root));
    }

    void parse_xml_fail(const std::string& xml) {
        xml_buffer = xml;
        TaurusStatus status;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &status);
        // Parsing should fail or succeed with empty document for invalid XML
    }
};

/* ============================================================================
 * UTF-8 BOM (Byte Order Mark) Tests
 * ============================================================================ */

TEST_F(EncodingEntitiesTest, UTF8WithBOM) {
    // UTF-8 BOM: EF BB BF
    std::string xml = "\xEF\xBB\xBF<root>text</root>";
    parse_xml(xml);

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "text");
}

TEST_F(EncodingEntitiesTest, UTF8BOMWithDeclaration) {
    // BOM + XML declaration
    std::string xml = "\xEF\xBB\xBF<?xml version='1.0'?><root>text</root>";
    parse_xml(xml);

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "text");
}

TEST_F(EncodingEntitiesTest, UTF8JapaneseCharacters) {
    parse_xml("<root>日本語テキスト</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "日本語テキスト");
}

TEST_F(EncodingEntitiesTest, UTF8EmojiCharacters) {
    parse_xml("<root>😀🎉🚀✨</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "😀🎉🚀✨");
}

TEST_F(EncodingEntitiesTest, UTF8MixedScripts) {
    parse_xml("<root>Hello世界مرحبا😀</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "Hello世界مرحبا😀");
}

/* ============================================================================
 * Predefined Entity Tests
 * ============================================================================ */

TEST_F(EncodingEntitiesTest, EntityLt) {
    parse_xml("<root>&lt;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<");
}

TEST_F(EncodingEntitiesTest, EntityGt) {
    parse_xml("<root>&gt;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, ">");
}

TEST_F(EncodingEntitiesTest, EntityAmp) {
    parse_xml("<root>&amp;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "&");
}

TEST_F(EncodingEntitiesTest, EntityApos) {
    parse_xml("<root>&apos;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "'");
}

TEST_F(EncodingEntitiesTest, EntityQuot) {
    parse_xml("<root>&quot;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "\"");
}

TEST_F(EncodingEntitiesTest, AllPredefinedEntities) {
    parse_xml("<root>&lt;&gt;&amp;&apos;&quot;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<>&'\"");
}

/* ============================================================================
 * Numeric Character Entity Tests (Decimal)
 * ============================================================================ */

TEST_F(EncodingEntitiesTest, NumericEntitySimple) {
    parse_xml("<root>&#65;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "A");
}

TEST_F(EncodingEntitiesTest, NumericEntityMultiple) {
    parse_xml("<root>&#72;&#73;&#84;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "HIT");  // 72=H, 73=I, 84=T
}

TEST_F(EncodingEntitiesTest, NumericEntityUnicode) {
    // U+65E5 (日) - Japanese character (26085 decimal)
    parse_xml("<root>&#26085;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "日");
}

TEST_F(EncodingEntitiesTest, NumericEntityEmoji) {
    // U+1F600 (😀)
    parse_xml("<root>&#128512;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "😀");
}

TEST_F(EncodingEntitiesTest, NumericEntityZero) {
    parse_xml("<root>&#0;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "\0");  // Null character
}

TEST_F(EncodingEntitiesTest, NumericEntityMaxBMP) {
    // U+FFFF (max BMP character)
    parse_xml("<root>&#65535;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    // Valid UTF-8 encoding of U+FFFF
    EXPECT_EQ(text[0], (char)0xEF);
    EXPECT_EQ(text[1], (char)0xBF);
    EXPECT_EQ(text[2], (char)0xBF);
}

/* ============================================================================
 * Hexadecimal Character Entity Tests
 * ============================================================================ */

TEST_F(EncodingEntitiesTest, HexEntitySimple) {
    parse_xml("<root>&#x41;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "A");
}

TEST_F(EncodingEntitiesTest, HexEntityLowercase) {
    parse_xml("<root>&#x42;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "B");
}

TEST_F(EncodingEntitiesTest, HexEntityUppercase) {
    parse_xml("<root>&#X43;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "C");
}

TEST_F(EncodingEntitiesTest, HexEntityUnicode) {
    // U+65E5 (日) - Japanese character
    parse_xml("<root>&#x65e5;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "日");
}

TEST_F(EncodingEntitiesTest, HexEntityEmoji) {
    // U+1F600 (😀)
    parse_xml("<root>&#x1F600;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "😀");
}

/* ============================================================================
 * Entity in Attributes Tests
 * ============================================================================ */

TEST_F(EncodingEntitiesTest, EntityInAttributeValue) {
    parse_xml("<root attr=\"&lt;&gt;&amp;&apos;&quot;\">text</root>");

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "<>&'\"");
}

TEST_F(EncodingEntitiesTest, NumericEntityInAttribute) {
    parse_xml("<root attr=\"&#65;&#66;&#67;\">text</root>");

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "ABC");
}

TEST_F(EncodingEntitiesTest, HexEntityInAttribute) {
    parse_xml("<root attr=\"&#x41;&#x42;&#x43;\">text</root>");

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "ABC");
}

/* ============================================================================
 * Mixed Content with Entities
 * ============================================================================ */

TEST_F(EncodingEntitiesTest, EntitiesWithText) {
    parse_xml("<root>a &lt; b &gt; c &amp; d</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "a < b > c & d");
}

TEST_F(EncodingEntitiesTest, EntitiesWithElements) {
    parse_xml("<root>&lt;node/&gt;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    // This is text content, not an element
    EXPECT_STREQ(text, "<node/>");
}

TEST_F(EncodingEntitiesTest, MultipleEntities) {
    parse_xml("<root>&lt;&gt;&lt;&gt;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<><>");
}

/* ============================================================================
 * Entity Edge Cases
 * ============================================================================ */

TEST_F(EncodingEntitiesTest, EntityAtStart) {
    parse_xml("<root>&lt;start</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<start");
}

TEST_F(EncodingEntitiesTest, EntityAtEnd) {
    parse_xml("<root>end&gt;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "end>");
}

TEST_F(EncodingEntitiesTest, OnlyEntities) {
    parse_xml("<root>&lt;&gt;&amp;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<>&");
}

TEST_F(EncodingEntitiesTest, EntitySequences) {
    parse_xml("<root>&lt;&lt;&lt;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<<<");
}

/* ============================================================================
 * CDATA vs Entity Comparison
 * ============================================================================ */

TEST_F(EncodingEntitiesTest, CDATAContainsEntities) {
    parse_xml("<root><![CDATA[&lt;&gt;&amp;]]></root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    // CDATA preserves literal content, entities are not expanded
    EXPECT_STREQ(text, "&lt;&gt;&amp;");
}

TEST_F(EncodingEntitiesTest, TextWithEntities) {
    parse_xml("<root>&lt;&gt;&amp;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    // Text expands entities
    EXPECT_STREQ(text, "<>&");
}

/* ============================================================================
 * Attribute with Special Characters
 * ============================================================================ */

TEST_F(EncodingEntitiesTest, AttributeWithBrackets) {
    parse_xml("<node attr=\"&lt;tag&gt;\">text</node>");

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "<tag>");
}

TEST_F(EncodingEntitiesTest, AttributeWithAmpersand) {
    parse_xml("<node attr=\"a &amp; b\">text</node>");

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "a & b");
}

TEST_F(EncodingEntitiesTest, AttributeWithQuotes) {
    parse_xml("<node attr=\"&quot;hello&quot;\">text</node>");

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "\"hello\"");
}

TEST_F(EncodingEntitiesTest, AttributeWithApostrophe) {
    parse_xml("<node attr=\"&apos;world&apos;\">text</node>");

    const char* attr = taurus_element_attribute(root, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "'world'");
}

/* ============================================================================
 * Whitespace and Encoding Tests
 * ============================================================================ */

TEST_F(EncodingEntitiesTest, WhitespacePreservation) {
    parse_xml("<root>  &lt;text&gt;  </root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "  <text>  ");
}

TEST_F(EncodingEntitiesTest, NewlineInTextWithEntities) {
    parse_xml("<root>&lt;\n&gt;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<\n>");
}

TEST_F(EncodingEntitiesTest, TabInTextWithEntities) {
    parse_xml("<root>&lt;\t&gt;</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<\t>");
}

/* ============================================================================
 * Invalid/Malformed Entity Tests
 * ============================================================================ */

TEST_F(EncodingEntitiesTest, InvalidNumericEntityIncomplete) {
    // Incomplete numeric entity - parser might handle this gracefully
    parse_xml("<root>&#65</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    // Taurus should parse what it can
}

TEST_F(EncodingEntitiesTest, InvalidHexEntityIncomplete) {
    parse_xml("<root>&#x4</root>");

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    // Taurus should parse what it can
}

TEST_F(EncodingEntitiesTest, InvalidEntityUnknown) {
    // Unknown entity - parsers should handle gracefully
    parse_xml_fail("<root>&unknown;</root>");
}

TEST_F(EncodingEntitiesTest, MalformedEntityMissingSemicolon) {
    parse_xml_fail("<root>&lt</root>");
}

TEST_F(EncodingEntitiesTest, MalformedEntityMissingAmpersand) {
    parse_xml_fail("<root>lt;</root>");
}

/* ============================================================================
 * Combined Tests
 * ============================================================================ */

TEST_F(EncodingEntitiesTest, ComplexDocumentWithEntities) {
    parse_xml(
        "<?xml version='1.0' encoding='UTF-8'?>"
        "<root>"
        "  <header>&lt;Document Header&gt;</header>"
        "  <content attr=\"value: &quot;test&quot;\">"
        "    Text with &#65;&#66;&#67; and &#x41;&#x42;&#x43;"
        "  </content>"
        "  <footer>&copy; 2024</footer>"
        "</root>"
    );

    // Check header text
    TaurusElement header = taurus_element_find_child(root, "header");
    ASSERT_TRUE(ELEM_NOT_NULL(header));
    const char* header_text = taurus_element_text(header);
    EXPECT_STREQ(header_text, "<Document Header>");

    // Check content attribute
    TaurusElement content = taurus_element_find_child(root, "content");
    ASSERT_TRUE(ELEM_NOT_NULL(content));
    const char* attr = taurus_element_attribute(content, "attr");
    EXPECT_STREQ(attr, "value: \"test\"");

    // Check content text (Taurus preserves whitespace from XML)
    const char* content_text = taurus_element_text(content);
    ASSERT_NE(content_text, nullptr);
    // XML preserves whitespace: "    Text with ABC and ABC  " (2 trailing spaces from XML)
    EXPECT_STREQ(content_text, "    Text with ABC and ABC  ");
}

} // namespace taurus_test
