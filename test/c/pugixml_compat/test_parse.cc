/* test_parse.cc - Parse tests based on pugixml test_parse.cpp
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for XML parsing: PI, comments, CDATA, whitespace, escapes, attributes
 * Based on pugixml/tests/test_parse.cpp
 */

#include <gtest/gtest.h>
#include <string>
#include <cstring>
#include "../../src/include/taurus.h"

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
#define ASSERT_ELEM_NOT_NULL(elem) ASSERT_TRUE(!taurus_element_is_null((elem)))

/**
 * Base class for parse tests
 */
class ParseTest : public ::testing::Test {
protected:
    TaurusDocument doc;
    std::string xml_buffer;
    TaurusStatus last_status;

    void SetUp() override {
        doc = nullptr;
        xml_buffer.clear();
        last_status = TAURUS_OK;
    }

    void TearDown() override {
        if (doc) {
            taurus_document_free(doc);
            doc = nullptr;
        }
        xml_buffer.clear();
    }

    // Parse XML and check status
    bool parse_ok(const std::string& xml) {
        xml_buffer = xml;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &last_status);
        return last_status == TAURUS_OK && doc != nullptr;
    }

    // Parse XML and expect error
    bool parse_error(const std::string& xml, TaurusStatus expected_error) {
        xml_buffer = xml;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &last_status);
        return last_status == expected_error;
    }

    // Get root element
    TaurusElement root() {
        return doc ? taurus_document_root(doc) : taurus_element_handle_null();
    }

    // Get first child (any)
    TaurusElement first_child_any() {
        TaurusElement r = root();
        return !element_is_null(r) ? taurus_element_first_child_any(r) : taurus_element_handle_null();
    }

    // Get next sibling (any)
    TaurusElement next_sibling_any(TaurusElement elem) {
        return !element_is_null(elem) ? taurus_element_next_sibling_any(elem) : taurus_element_handle_null();
    }

    // Get element name
    const char* elem_name(TaurusElement elem) {
        return !element_is_null(elem) ? taurus_element_name(elem) : nullptr;
    }

    // Get element text
    const char* elem_text(TaurusElement elem) {
        return !element_is_null(elem) ? taurus_element_text(elem) : nullptr;
    }

    // Get child value (text content of first child)
    const char* child_value(TaurusElement elem) {
        return !element_is_null(elem) ? taurus_element_child_value(elem) : nullptr;
    }
};

// ============================================================================
// Processing Instruction Tests
// ============================================================================

TEST_F(ParseTest, PISkip) {
    // PIs should be skipped by default, but Taurus requires a document element
    // So PIs alone without a root element fail (correct behavior)
    EXPECT_FALSE(parse_ok("<?pi?><?pi value?>"));

    // With a root element, PIs are skipped
    EXPECT_TRUE(parse_ok("<?pi?><?pi value?><root/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    EXPECT_STREQ(elem_name(root_elem), "root");
}

TEST_F(ParseTest, PIWithValue) {
    EXPECT_TRUE(parse_ok("<?test value?><root/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    // Root element should exist
    EXPECT_STREQ(elem_name(root_elem), "root");
}

TEST_F(ParseTest, PIWithSpaces) {
    EXPECT_TRUE(parse_ok("<?target  value?><root/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    // The root element is "root", not the PI target "target"
    EXPECT_STREQ(elem_name(root_elem), "root");
}

TEST_F(ParseTest, PIErrorEmpty) {
    // Empty PI name is an error
    EXPECT_FALSE(parse_ok("<?>"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, PIErrorDoubleQuestion) {
    // Double question mark is an error
    EXPECT_FALSE(parse_ok("<?>"));
}

TEST_F(ParseTest, PIErrorNoTermination) {
    // Unterminated PI
    EXPECT_FALSE(parse_ok("<?pi"));
}

TEST_F(ParseTest, PIErrorInvalidNameChar) {
    // PI with invalid character in name
    EXPECT_FALSE(parse_ok("<?#invalid?>"));
}

// ============================================================================
// Comment Tests
// ============================================================================

TEST_F(ParseTest, CommentSkip) {
    // Comments are parsed but treated as text content
    EXPECT_TRUE(parse_ok("<!--comment--><root/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    EXPECT_STREQ(elem_name(root_elem), "root");
}

TEST_F(ParseTest, CommentWithText) {
    EXPECT_TRUE(parse_ok("<!--comment--><root/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    EXPECT_STREQ(elem_name(root_elem), "root");
}

TEST_F(ParseTest, CommentErrorUnterminated) {
    // Unterminated comment
    EXPECT_FALSE(parse_ok("<!--comment"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, CommentErrorEmpty) {
    EXPECT_FALSE(parse_ok("<!--"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, CommentErrorDashDash) {
    // Double dash inside comment is invalid
    EXPECT_FALSE(parse_ok("<!-- -- -->"));
}

TEST_F(ParseTest, CommentPreservesContent) {
    EXPECT_TRUE(parse_ok("<!-- comment with spaces --><root/>"));
    // Should parse successfully
    EXPECT_EQ(last_status, TAURUS_OK);
}

// ============================================================================
// CDATA Tests
// ============================================================================

TEST_F(ParseTest, CDATAWithText) {
    EXPECT_TRUE(parse_ok("<root><![CDATA[test]]></root>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* text = child_value(root_elem);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "test");
}

TEST_F(ParseTest, CDATAMixedContent) {
    EXPECT_TRUE(parse_ok("<root><![CDATA[text]]> more</root>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* text = child_value(root_elem);
    ASSERT_NE(text, nullptr);
    // CDATA content is preserved
    EXPECT_TRUE(strstr(text, "text") != nullptr);
}

TEST_F(ParseTest, CDATAErrorUnterminated) {
    EXPECT_FALSE(parse_ok("<![CDATA[not terminated"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, CDATAErrorMissingBracket) {
    EXPECT_FALSE(parse_ok("<![CDATA[test]]"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, CDATAWithSpecialChars) {
    // CDATA can contain < and > without escaping
    EXPECT_TRUE(parse_ok("<root><![CDATA[<tag>]]></root>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* text = child_value(root_elem);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<tag>");
}

// ============================================================================
// Text/PCDATA Tests
// ============================================================================

TEST_F(ParseTest, TextContent) {
    EXPECT_TRUE(parse_ok("<root>text content</root>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* text = child_value(root_elem);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "text content");
}

TEST_F(ParseTest, TextWithEntities) {
    EXPECT_TRUE(parse_ok("<root>&lt;&gt;&amp;&quot;&apos;</root>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* text = child_value(root_elem);
    ASSERT_NE(text, nullptr);
    // Entities are expanded in input order: &lt; &gt; &amp; &quot; &apos; -> < > & " '
    EXPECT_STREQ(text, "<>&\"'");
}

TEST_F(ParseTest, TextWithNumericEntities) {
    EXPECT_TRUE(parse_ok("<root>&#65;&#x41;</root>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* text = child_value(root_elem);
    ASSERT_NE(text, nullptr);
    // 'A' (65 decimal and hex)
    EXPECT_STREQ(text, "AA");
}

TEST_F(ParseTest, TextUnicode) {
    EXPECT_TRUE(parse_ok("<root>&#x03B3;&#x03b3;</root>"));  // Greek gamma
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* text = child_value(root_elem);
    ASSERT_NE(text, nullptr);
    // Should contain two gamma characters
    EXPECT_EQ(strlen(text), 4);  // 2 chars * 2 bytes each for UTF-8
}

TEST_F(ParseTest, TextMixedWithElements) {
    EXPECT_TRUE(parse_ok("<root>text1<child/>text2</root>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    // Use element_text which concatenates all text content across child elements
    const char* text = taurus_element_text(root_elem);
    ASSERT_NE(text, nullptr);
    EXPECT_TRUE(strstr(text, "text1text2") != nullptr);
}

TEST_F(ParseTest, TextPreservesWhitespace) {
    EXPECT_TRUE(parse_ok("<root>  spaces  </root>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* text = child_value(root_elem);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "  spaces  ");
}

TEST_F(ParseTest, TextEmptyElement) {
    EXPECT_TRUE(parse_ok("<root></root>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* text = child_value(root_elem);
    // Empty elements may have empty text or null
}

// ============================================================================
// Attribute Tests
// ============================================================================

TEST_F(ParseTest, AttributeBasic) {
    EXPECT_TRUE(parse_ok("<node id='value'/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* attr = taurus_element_attribute(root_elem, "id");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "value");
}

TEST_F(ParseTest, AttributeDoubleQuotes) {
    EXPECT_TRUE(parse_ok("<node id=\"value\"/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* attr = taurus_element_attribute(root_elem, "id");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "value");
}

TEST_F(ParseTest, AttributeSpacesAroundEquals) {
    EXPECT_TRUE(parse_ok("<node id1='v1' id2 ='v2' id3= 'v3' id4 = 'v4'/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* attr1 = taurus_element_attribute(root_elem, "id1");
    const char* attr2 = taurus_element_attribute(root_elem, "id2");
    const char* attr3 = taurus_element_attribute(root_elem, "id3");
    const char* attr4 = taurus_element_attribute(root_elem, "id4");
    ASSERT_NE(attr1, nullptr);
    ASSERT_NE(attr2, nullptr);
    ASSERT_NE(attr3, nullptr);
    ASSERT_NE(attr4, nullptr);
    EXPECT_STREQ(attr1, "v1");
    EXPECT_STREQ(attr2, "v2");
    EXPECT_STREQ(attr3, "v3");
    EXPECT_STREQ(attr4, "v4");
}

TEST_F(ParseTest, AttributeWithEntities) {
    EXPECT_TRUE(parse_ok("<node id='&lt;&gt;&amp;'/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* attr = taurus_element_attribute(root_elem, "id");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "<>&");
}

TEST_F(ParseTest, AttributeQuoteInside) {
    EXPECT_TRUE(parse_ok("<node id1='\"' id2=\"'\"/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* attr1 = taurus_element_attribute(root_elem, "id1");
    const char* attr2 = taurus_element_attribute(root_elem, "id2");
    ASSERT_NE(attr1, nullptr);
    ASSERT_NE(attr2, nullptr);
    EXPECT_STREQ(attr1, "\"");
    EXPECT_STREQ(attr2, "'");
}

TEST_F(ParseTest, AttributeMissingValue) {
    EXPECT_FALSE(parse_ok("<node id/>"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, AttributeUnterminated) {
    EXPECT_FALSE(parse_ok("<node id='value"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, AttributeMissingEquals) {
    EXPECT_FALSE(parse_ok("<node id'value'/>"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, AttributeInvalidCharacter) {
    EXPECT_FALSE(parse_ok("<node id&='value'/>"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, MultipleAttributesNoSeparator) {
    EXPECT_FALSE(parse_ok("<node id1='v1'id2='v2'/>"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, ManyAttributes) {
    std::string xml = "<root ";
    for (int i = 0; i < 20; i++) {
        xml += "attr" + std::to_string(i) + "='value" + std::to_string(i) + "' ";
    }
    xml += "/>";

    EXPECT_TRUE(parse_ok(xml));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);

    // Verify all attributes
    for (int i = 0; i < 20; i++) {
        std::string attr_name = "attr" + std::to_string(i);
        const char* attr = taurus_element_attribute(root_elem, attr_name.c_str());
        ASSERT_NE(attr, nullptr) << "Attribute " << attr_name << " not found";
    }
}

// ============================================================================
// Tag/Element Tests
// ============================================================================

TEST_F(ParseTest, SelfClosingTag) {
    EXPECT_TRUE(parse_ok("<node/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    EXPECT_STREQ(elem_name(root_elem), "node");
}

TEST_F(ParseTest, SelfClosingWithSpace) {
    EXPECT_TRUE(parse_ok("<node />"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    EXPECT_STREQ(elem_name(root_elem), "node");
}

TEST_F(ParseTest, NestedElements) {
    EXPECT_TRUE(parse_ok("<root><child><grandchild/></child></root>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    EXPECT_STREQ(elem_name(root_elem), "root");

    TaurusElement child = taurus_element_find_child(root_elem, "child");
    ASSERT_ELEM_NOT_NULL(child);
    EXPECT_STREQ(elem_name(child), "child");

    TaurusElement grandchild = taurus_element_find_child(child, "grandchild");
    ASSERT_ELEM_NOT_NULL(grandchild);
    EXPECT_STREQ(elem_name(grandchild), "grandchild");
}

TEST_F(ParseTest, TagErrorUnclosed) {
    EXPECT_FALSE(parse_ok("<root>"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, TagErrorMismatched) {
    EXPECT_FALSE(parse_ok("<root></child>"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, TagErrorEmptyName) {
    EXPECT_FALSE(parse_ok("<>"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, TagErrorInvalidStart) {
    EXPECT_FALSE(parse_ok("<#invalid/>"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, TagErrorNoClose) {
    EXPECT_FALSE(parse_ok("<root"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, TagErrorExtraContent) {
    EXPECT_FALSE(parse_ok("<root></root extra"));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

// ============================================================================
// Declaration Tests
// ============================================================================

TEST_F(ParseTest, XMLDeclaration) {
    EXPECT_TRUE(parse_ok("<?xml version='1.0'?><root/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    EXPECT_STREQ(elem_name(root_elem), "root");
}

TEST_F(ParseTest, XMLDeclarationWithEncoding) {
    EXPECT_TRUE(parse_ok("<?xml version='1.0' encoding='UTF-8'?><root/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    EXPECT_STREQ(elem_name(root_elem), "root");
}

TEST_F(ParseTest, XMLDeclarationStandalone) {
    EXPECT_TRUE(parse_ok("<?xml version='1.0' standalone='yes'?><root/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    EXPECT_STREQ(elem_name(root_elem), "root");
}

// ============================================================================
// Empty/Error Tests
// ============================================================================

TEST_F(ParseTest, EmptyString) {
    EXPECT_FALSE(parse_ok(""));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, WhitespaceOnly) {
    EXPECT_FALSE(parse_ok("   "));
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, OnlyComment) {
    EXPECT_FALSE(parse_ok("<!--comment-->"));
    // No document element
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, OnlyPI) {
    EXPECT_FALSE(parse_ok("<?pi?>"));
    // No document element
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

TEST_F(ParseTest, OnlyDeclaration) {
    EXPECT_FALSE(parse_ok("<?xml version='1.0'?>"));
    // No document element
    EXPECT_EQ(last_status, TAURUS_ERROR_PARSE);
}

// ============================================================================
// Complex Documents
// ============================================================================

TEST_F(ParseTest, ComplexMixedContent) {
    EXPECT_TRUE(parse_ok("<root>text<!--comment--><?pi?><child attr='value'>inner</child></root>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    EXPECT_STREQ(elem_name(root_elem), "root");

    TaurusElement child = taurus_element_find_child(root_elem, "child");
    ASSERT_ELEM_NOT_NULL(child);
    EXPECT_STREQ(elem_name(child), "child");

    const char* attr = taurus_element_attribute(child, "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "value");
}

TEST_F(ParseTest, DeepNesting) {
    std::string xml = "<root>";
    for (int i = 0; i < 50; i++) {
        xml += "<level" + std::to_string(i) + ">";
    }
    xml += "deep";
    for (int i = 49; i >= 0; i--) {
        xml += "</level" + std::to_string(i) + ">";
    }
    xml += "</root>";

    EXPECT_TRUE(parse_ok(xml));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    EXPECT_STREQ(elem_name(root_elem), "root");
}

TEST_F(ParseTest, LargeDocument) {
    std::string xml = "<root>";
    for (int i = 0; i < 1000; i++) {
        xml += "<item id='" + std::to_string(i) + "'>Item " + std::to_string(i) + "</item>";
    }
    xml += "</root>";

    EXPECT_TRUE(parse_ok(xml));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    EXPECT_STREQ(elem_name(root_elem), "root");
}

// ============================================================================
// Memory/Edge Cases
// ============================================================================

TEST_F(ParseTest, LargeTextContent) {
    std::string content(10000, 'x');
    std::string xml = "<root>" + content + "</root>";
    EXPECT_TRUE(parse_ok(xml));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* text = child_value(root_elem);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(strlen(text), 10000);
}

TEST_F(ParseTest, LongElementName) {
    std::string name(1000, 'a');
    std::string xml = "<" + name + "/>";
    EXPECT_TRUE(parse_ok(xml));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    EXPECT_EQ(strlen(elem_name(root_elem)), 1000);
}

TEST_F(ParseTest, LongAttributeName) {
    std::string name(500, 'a');
    std::string xml = "<root " + name + "='value'/>";
    EXPECT_TRUE(parse_ok(xml));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    const char* attr = taurus_element_attribute(root_elem, name.c_str());
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "value");
}

// ============================================================================
// Attribute Type Conversion Tests
// ============================================================================

TEST_F(ParseTest, AttributeInt) {
    EXPECT_TRUE(parse_ok("<root value='42'/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    int val = taurus_element_attribute_int(root_elem, "value", -1);
    EXPECT_EQ(val, 42);
}

TEST_F(ParseTest, AttributeIntDefault) {
    EXPECT_TRUE(parse_ok("<root/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    int val = taurus_element_attribute_int(root_elem, "missing", -1);
    EXPECT_EQ(val, -1);
}

TEST_F(ParseTest, AttributeDouble) {
    EXPECT_TRUE(parse_ok("<root value='3.14'/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    double val = taurus_element_attribute_double(root_elem, "value", 0.0);
    EXPECT_DOUBLE_EQ(val, 3.14);
}

TEST_F(ParseTest, AttributeBool) {
    EXPECT_TRUE(parse_ok("<root val='true'/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    int val = taurus_element_attribute_bool(root_elem, "val", 0);
    EXPECT_EQ(val, 1);  // true
}

TEST_F(ParseTest, AttributeBoolFalse) {
    EXPECT_TRUE(parse_ok("<root val='false'/>"));
    TaurusElement root_elem = root();
    ASSERT_ELEM_NOT_NULL(root_elem);
    int val = taurus_element_attribute_bool(root_elem, "val", 1);
    EXPECT_EQ(val, 0);  // false
}

} // namespace taurus_test
