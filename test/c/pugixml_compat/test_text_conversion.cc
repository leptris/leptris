/* test_text_conversion.cc - Text content type conversion tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for text content type conversion APIs (as_int, as_double, as_bool, etc.)
 * Based on pugixml/tests/test_dom_text.cpp
 */

#include <gtest/gtest.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <cmath>
#include "../../src/include/taurus.h"

/* TaurusElement handle API macros */
#define ELEM_NULL() taurus_element_handle_null()

namespace taurus_test {

/**
 * Test class for text content type conversion operations
 */
class TextConversionTest : public ::testing::Test {
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

    // Helper to get element by name
    TaurusElement get_element(const char* name) {
        TaurusElement root = taurus_document_root(doc);
        return taurus_element_first_child(root, name);
    }
};

// ============================================================================
// as_int Tests
// ============================================================================

TEST_F(TextConversionTest, AsIntBasic) {
    parse_string("<root><text1>1</text1><text2>-1</text2><text3>-2147483648</text3><text4>2147483647</text4><text5>0</text5></root>");

    EXPECT_EQ(taurus_element_text_int(get_element("text1"), 0), 1);
    EXPECT_EQ(taurus_element_text_int(get_element("text2"), 0), -1);
    EXPECT_EQ(taurus_element_text_int(get_element("text3"), 0), -2147483647 - 1);
    EXPECT_EQ(taurus_element_text_int(get_element("text4"), 0), 2147483647);
    EXPECT_EQ(taurus_element_text_int(get_element("text5"), 0), 0);
}

TEST_F(TextConversionTest, AsIntHex) {
    parse_string("<root><text1>0777</text1><text2>0x5ab</text2><text3>0XFf</text3><text4>-0x20</text4><text5>-0x80000000</text5><text6>0x</text6></root>");

    // No octal support - intentional (0777 should be parsed as decimal)
    EXPECT_EQ(taurus_element_text_int(get_element("text1"), 0), 777);
    EXPECT_EQ(taurus_element_text_int(get_element("text2"), 0), 1451);
    EXPECT_EQ(taurus_element_text_int(get_element("text3"), 0), 255);
    EXPECT_EQ(taurus_element_text_int(get_element("text4"), 0), -32);
    EXPECT_EQ(taurus_element_text_int(get_element("text5"), 0), -2147483647 - 1);
    EXPECT_EQ(taurus_element_text_int(get_element("text6"), 0), 0);
}

TEST_F(TextConversionTest, AsIntWhitespace) {
    parse_string("<root><text1> \t\n1234</text1><text2>\n\t 0x123</text2><text3>- 16</text3><text4>- 0x10</text4></root>");

    EXPECT_EQ(taurus_element_text_int(get_element("text1"), 0), 1234);
    EXPECT_EQ(taurus_element_text_int(get_element("text2"), 0), 291);
    EXPECT_EQ(taurus_element_text_int(get_element("text3"), 0), 0);  // "- 16" is invalid
    EXPECT_EQ(taurus_element_text_int(get_element("text4"), 0), 0);  // "- 0x10" is invalid
}

TEST_F(TextConversionTest, AsIntDefault) {
    parse_string("<root><text>abc</text></root>");

    EXPECT_EQ(taurus_element_text_int(get_element("text"), -999), -999);
    EXPECT_EQ(taurus_element_text_int(get_element("text"), 42), 42);
}

TEST_F(TextConversionTest, AsIntEmpty) {
    parse_string("<root><text></text></root>");

    EXPECT_EQ(taurus_element_text_int(get_element("text"), 99), 99);
}

TEST_F(TextConversionTest, AsIntNullElement) {
    EXPECT_EQ(taurus_element_text_int(ELEM_NULL(), 123), 123);
}

// ============================================================================
// as_uint Tests
// ============================================================================

TEST_F(TextConversionTest, AsUintBasic) {
    parse_string("<root><text1>0</text1><text2>1</text2><text3>2147483647</text3><text4>4294967295</text4><text5>0</text5></root>");

    EXPECT_EQ(taurus_element_text_uint(get_element("text1"), 0), 0);
    EXPECT_EQ(taurus_element_text_uint(get_element("text2"), 0), 1);
    EXPECT_EQ(taurus_element_text_uint(get_element("text3"), 0), 2147483647);
    EXPECT_EQ(taurus_element_text_uint(get_element("text4"), 0), 4294967295u);
    EXPECT_EQ(taurus_element_text_uint(get_element("text5"), 0), 0);
}

TEST_F(TextConversionTest, AsUintHex) {
    parse_string("<root><text1>0777</text1><text2>0x5ab</text2><text3>0XFf</text3><text4>0x20</text4><text5>0xFFFFFFFF</text5><text6>0x</text6></root>");

    // No octal support - intentional
    EXPECT_EQ(taurus_element_text_uint(get_element("text1"), 0), 777);
    EXPECT_EQ(taurus_element_text_uint(get_element("text2"), 0), 1451);
    EXPECT_EQ(taurus_element_text_uint(get_element("text3"), 0), 255);
    EXPECT_EQ(taurus_element_text_uint(get_element("text4"), 0), 32);
    EXPECT_EQ(taurus_element_text_uint(get_element("text5"), 0), 0xFFFFFFFFu);
    EXPECT_EQ(taurus_element_text_uint(get_element("text6"), 0), 0);
}

TEST_F(TextConversionTest, AsUintDefault) {
    parse_string("<root><text>abc</text></root>");

    EXPECT_EQ(taurus_element_text_uint(get_element("text"), 999), 999);
}

TEST_F(TextConversionTest, AsUintNullElement) {
    EXPECT_EQ(taurus_element_text_uint(ELEM_NULL(), 456), 456u);
}

// ============================================================================
// as_double Tests
// ============================================================================

TEST_F(TextConversionTest, AsDoubleBasic) {
    parse_string("<root><text1>0</text1><text2>1</text2><text3>0.12</text3><text4>-5.1</text4><text5>3e-4</text5><text6>3.14159265358979323846</text6></root>");

    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("text1"), 0.0), 0.0);
    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("text2"), 0.0), 1.0);
    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("text3"), 0.0), 0.12);
    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("text4"), 0.0), -5.1);
    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("text5"), 0.0), 3e-4);
    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("text6"), 0.0), 3.14159265358979323846);
}

TEST_F(TextConversionTest, AsDoubleScientific) {
    parse_string("<root><text1>1e10</text1><text2>1.5E10</text2><text3>-2.5e-5</text3><text4>1E+5</text4></root>");

    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("text1"), 0.0), 1e10);
    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("text2"), 0.0), 1.5E10);
    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("text3"), 0.0), -2.5e-5);
    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("text4"), 0.0), 1e5);
}

TEST_F(TextConversionTest, AsDoubleDefault) {
    parse_string("<root><text>abc</text></root>");

    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("text"), 3.14), 3.14);
}

TEST_F(TextConversionTest, AsDoubleNullElement) {
    EXPECT_DOUBLE_EQ(taurus_element_text_double(ELEM_NULL(), 2.71), 2.71);
}

// ============================================================================
// as_float Tests
// ============================================================================

TEST_F(TextConversionTest, AsFloatBasic) {
    parse_string("<root><text1>0</text1><text2>1</text2><text3>0.12</text3><text4>-5.1</text4><text5>3e-4</text5><text6>3.14159</text6></root>");

    EXPECT_FLOAT_EQ(taurus_element_text_float(get_element("text1"), 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(taurus_element_text_float(get_element("text2"), 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(taurus_element_text_float(get_element("text3"), 0.0f), 0.12f);
    EXPECT_FLOAT_EQ(taurus_element_text_float(get_element("text4"), 0.0f), -5.1f);
    EXPECT_FLOAT_EQ(taurus_element_text_float(get_element("text5"), 0.0f), 3e-4f);
    EXPECT_FLOAT_EQ(taurus_element_text_float(get_element("text6"), 0.0f), 3.14159f);
}

TEST_F(TextConversionTest, AsFloatDefault) {
    parse_string("<root><text>abc</text></root>");

    EXPECT_FLOAT_EQ(taurus_element_text_float(get_element("text"), 1.5f), 1.5f);
}

TEST_F(TextConversionTest, AsFloatNullElement) {
    EXPECT_FLOAT_EQ(taurus_element_text_float(ELEM_NULL(), 9.99f), 9.99f);
}

// ============================================================================
// as_bool Tests
// ============================================================================

TEST_F(TextConversionTest, AsBoolBasic) {
    parse_string("<root><text1>0</text1><text2>1</text2><text3>true</text3><text4>True</text4><text5>Yes</text5><text6>yes</text6><text7>false</text7></root>");

    EXPECT_EQ(taurus_element_text_bool(get_element("text1"), 0), 0);
    EXPECT_EQ(taurus_element_text_bool(get_element("text2"), 0), 1);
    EXPECT_EQ(taurus_element_text_bool(get_element("text3"), 0), 1);
    EXPECT_EQ(taurus_element_text_bool(get_element("text4"), 0), 1);
    EXPECT_EQ(taurus_element_text_bool(get_element("text5"), 0), 1);
    EXPECT_EQ(taurus_element_text_bool(get_element("text6"), 0), 1);
    EXPECT_EQ(taurus_element_text_bool(get_element("text7"), 1), 0);
}

TEST_F(TextConversionTest, AsBoolOtherTrueValues) {
    parse_string("<root><t1>TRUE</t1><t2>YES</t2><t3>on</t3><t4>ON</t4><t5>anything</t5></root>");

    EXPECT_EQ(taurus_element_text_bool(get_element("t1"), 0), 1);
    EXPECT_EQ(taurus_element_text_bool(get_element("t2"), 0), 1);
    EXPECT_EQ(taurus_element_text_bool(get_element("t3"), 0), 1);
    EXPECT_EQ(taurus_element_text_bool(get_element("t4"), 0), 1);
    EXPECT_EQ(taurus_element_text_bool(get_element("t5"), 0), 1);
}

TEST_F(TextConversionTest, AsBoolOtherFalseValues) {
    parse_string("<root><t1>FALSE</t1><t2>NO</t2><t3>off</t3><t4>OFF</t4><t5></t5></root>");

    EXPECT_EQ(taurus_element_text_bool(get_element("t1"), 1), 0);
    EXPECT_EQ(taurus_element_text_bool(get_element("t2"), 1), 0);
    EXPECT_EQ(taurus_element_text_bool(get_element("t3"), 1), 0);
    EXPECT_EQ(taurus_element_text_bool(get_element("t4"), 1), 0);
    EXPECT_EQ(taurus_element_text_bool(get_element("t5"), 1), 0);
}

TEST_F(TextConversionTest, AsBoolDefault) {
    parse_string("<root><text>invalid</text></root>");

    // Unrecognized non-empty string returns 1 (true), not the default
    EXPECT_EQ(taurus_element_text_bool(get_element("text"), 1), 1);
    EXPECT_EQ(taurus_element_text_bool(get_element("text"), 0), 1);
}

TEST_F(TextConversionTest, AsBoolNullElement) {
    // Null element returns 0 (false), not the default
    EXPECT_EQ(taurus_element_text_bool(ELEM_NULL(), 1), 0);
    EXPECT_EQ(taurus_element_text_bool(ELEM_NULL(), 0), 0);
}

// ============================================================================
// Edge Cases and Special Values
// ============================================================================

TEST_F(TextConversionTest, AsIntBoundaryValues) {
    parse_string("<root><min>-2147483648</min><max>2147483647</max></root>");

    EXPECT_EQ(taurus_element_text_int(get_element("min"), 0), -2147483647 - 1);
    EXPECT_EQ(taurus_element_text_int(get_element("max"), 0), 2147483647);
}

TEST_F(TextConversionTest, AsDoubleBoundaryValues) {
    parse_string("<root><min>-1e308</min><max>1e308</max><tiny>1e-308</tiny></root>");

    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("min"), 0.0), -1e308);
    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("max"), 0.0), 1e308);
    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("tiny"), 0.0), 1e-308);
}

TEST_F(TextConversionTest, AsIntTrailingGarbage) {
    parse_string("<root><text>123abc</text></root>");

    EXPECT_EQ(taurus_element_text_int(get_element("text"), 0), 0);  // Should fail on trailing chars
}

TEST_F(TextConversionTest, AsDoubleTrailingGarbage) {
    parse_string("<root><text>123.45abc</text></root>");

    EXPECT_EQ(taurus_element_text_double(get_element("text"), 0.0), 0.0);  // Should fail on trailing chars
}

TEST_F(TextConversionTest, AsIntLeadingZeros) {
    parse_string("<root><text>007</text></root>");

    EXPECT_EQ(taurus_element_text_int(get_element("text"), 0), 7);  // Leading zeros, no octal
}

TEST_F(TextConversionTest, AsDoubleLeadingWhitespace) {
    parse_string("<root><text>   42.5</text></root>");

    EXPECT_DOUBLE_EQ(taurus_element_text_double(get_element("text"), 0.0), 42.5);
}

TEST_F(TextConversionTest, MixedContentText) {
    // When element has mixed content (text + child elements), text() concatenates all text
    parse_string("<root>text1<child/>text2</root>");

    TaurusElement root = taurus_document_root(doc);
    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "text1text2");
}

} // namespace taurus_test
