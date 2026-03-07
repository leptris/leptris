/* test_attribute_conversion.cpp - Tests for pugixml attribute type conversion compatibility
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for attribute type conversion functions:
 * - taurus_element_attribute_int()
 * - taurus_element_attribute_uint()
 * - taurus_element_attribute_double()
 * - taurus_element_attribute_float()
 * - taurus_element_attribute_bool()
 * - taurus_element_attribute_string()
 */

#include <gtest/gtest.h>
#include <string>
#include <limits.h>
#include <float.h>
#include "../../src/include/taurus.h"

/* TaurusElement handle API macros */
#define ELEM_IS_NULL(e) taurus_element_is_null((e))
#define ELEM_NOT_NULL(e) !taurus_element_is_null((e))
#define ELEM_NULL() taurus_element_handle_null()

namespace taurus_test {

/**
 * Base class for attribute conversion tests
 */
class AttributeConversionTest : public ::testing::Test {
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
        ASSERT_EQ(status, TAURUS_OK);
        ASSERT_NE(doc, nullptr);
        root = taurus_document_root(doc);
        ASSERT_TRUE(ELEM_NOT_NULL(root));
    }
};

/* ============================================================================
 * Integer Conversion Tests (as_int)
 * ============================================================================ */

TEST_F(AttributeConversionTest, IntAttributeValid) {
    parse_xml("<node value='42'/>");

    int result = taurus_element_attribute_int(root, "value", -1);
    EXPECT_EQ(result, 42);
}

TEST_F(AttributeConversionTest, IntAttributeNegative) {
    parse_xml("<node value='-123'/>");

    int result = taurus_element_attribute_int(root, "value", 0);
    EXPECT_EQ(result, -123);
}

TEST_F(AttributeConversionTest, IntAttributeZero) {
    parse_xml("<node value='0'/>");

    int result = taurus_element_attribute_int(root, "value", 999);
    EXPECT_EQ(result, 0);
}

TEST_F(AttributeConversionTest, IntAttributeMissing) {
    parse_xml("<node other='5'/>");

    int result = taurus_element_attribute_int(root, "value", 42);
    EXPECT_EQ(result, 42);  // Default returned
}

TEST_F(AttributeConversionTest, IntAttributeInvalid) {
    parse_xml("<node value='abc'/>");

    int result = taurus_element_attribute_int(root, "value", 99);
    EXPECT_EQ(result, 99);  // Default returned for invalid
}

TEST_F(AttributeConversionTest, IntAttributePartialValid) {
    parse_xml("<node value='42abc'/>");

    int result = taurus_element_attribute_int(root, "value", -1);
    EXPECT_EQ(result, -1);  // Partial parse should return default
}

TEST_F(AttributeConversionTest, IntAttributeWithSpaces) {
    parse_xml("<node value='  42  '/>");

    int result = taurus_element_attribute_int(root, "value", 0);
    EXPECT_EQ(result, 42);  // strtol handles leading spaces
}

TEST_F(AttributeConversionTest, IntAttributeLargeValue) {
    parse_xml("<node value='2147483647'/>");  // INT_MAX

    int result = taurus_element_attribute_int(root, "value", 0);
    EXPECT_EQ(result, 2147483647);
}

TEST_F(AttributeConversionTest, IntAttributeHex) {
    parse_xml("<node value='0xFF'/>");

    int result = taurus_element_attribute_int(root, "value", -1);
    // strtol with base 10 won't parse hex, so it returns default
    EXPECT_EQ(result, -1);
}

/* ============================================================================
 * Unsigned Integer Conversion Tests (as_uint)
 * ============================================================================ */

TEST_F(AttributeConversionTest, UintAttributeValid) {
    parse_xml("<node value='42'/>");

    unsigned int result = taurus_element_attribute_uint(root, "value", 0);
    EXPECT_EQ(result, 42U);
}

TEST_F(AttributeConversionTest, UintAttributeLargeValue) {
    parse_xml("<node value='4294967295'/>");  // UINT_MAX

    unsigned int result = taurus_element_attribute_uint(root, "value", 0);
    EXPECT_EQ(result, 4294967295U);
}

TEST_F(AttributeConversionTest, UintAttributeZero) {
    parse_xml("<node value='0'/>");

    unsigned int result = taurus_element_attribute_uint(root, "value", 999);
    EXPECT_EQ(result, 0U);
}

TEST_F(AttributeConversionTest, UintAttributeMissing) {
    parse_xml("<node other='5'/>");

    unsigned int result = taurus_element_attribute_uint(root, "value", 100);
    EXPECT_EQ(result, 100U);
}

TEST_F(AttributeConversionTest, UintAttributeInvalid) {
    parse_xml("<node value='notanumber'/>");

    unsigned int result = taurus_element_attribute_uint(root, "value", 55);
    EXPECT_EQ(result, 55U);
}

/* ============================================================================
 * Double Conversion Tests (as_double)
 * ============================================================================ */

TEST_F(AttributeConversionTest, DoubleAttributeValid) {
    parse_xml("<node value='3.14'/>");

    double result = taurus_element_attribute_double(root, "value", 0.0);
    EXPECT_NEAR(result, 3.14, 0.001);
}

TEST_F(AttributeConversionTest, DoubleAttributeNegative) {
    parse_xml("<node value='-2.5'/>");

    double result = taurus_element_attribute_double(root, "value", 0.0);
    EXPECT_NEAR(result, -2.5, 0.001);
}

TEST_F(AttributeConversionTest, DoubleAttributeScientific) {
    parse_xml("<node value='1.5e2'/>");  // 150.0

    double result = taurus_element_attribute_double(root, "value", 0.0);
    EXPECT_NEAR(result, 150.0, 0.1);
}

TEST_F(AttributeConversionTest, DoubleAttributeInteger) {
    parse_xml("<node value='42'/>");

    double result = taurus_element_attribute_double(root, "value", 0.0);
    EXPECT_NEAR(result, 42.0, 0.001);
}

TEST_F(AttributeConversionTest, DoubleAttributeZero) {
    parse_xml("<node value='0.0'/>");

    double result = taurus_element_attribute_double(root, "value", 99.9);
    EXPECT_NEAR(result, 0.0, 0.001);
}

TEST_F(AttributeConversionTest, DoubleAttributeMissing) {
    parse_xml("<node other='1.5'/>");

    double result = taurus_element_attribute_double(root, "value", 2.5);
    EXPECT_NEAR(result, 2.5, 0.001);
}

TEST_F(AttributeConversionTest, DoubleAttributeInvalid) {
    parse_xml("<node value='abc'/>");

    double result = taurus_element_attribute_double(root, "value", 1.23);
    EXPECT_NEAR(result, 1.23, 0.001);
}

/* ============================================================================
 * Float Conversion Tests (as_float)
 * ============================================================================ */

TEST_F(AttributeConversionTest, FloatAttributeValid) {
    parse_xml("<node value='3.14'/>");

    float result = taurus_element_attribute_float(root, "value", 0.0f);
    EXPECT_NEAR(result, 3.14f, 0.01f);
}

TEST_F(AttributeConversionTest, FloatAttributeScientific) {
    parse_xml("<node value='1.5e2'/>");

    float result = taurus_element_attribute_float(root, "value", 0.0f);
    EXPECT_NEAR(result, 150.0f, 0.1f);
}

TEST_F(AttributeConversionTest, FloatAttributeMissing) {
    parse_xml("<node other='1.5'/>");

    float result = taurus_element_attribute_float(root, "value", 2.5f);
    EXPECT_NEAR(result, 2.5f, 0.01f);
}

/* ============================================================================
 * Boolean Conversion Tests (as_bool)
 * ============================================================================ */

TEST_F(AttributeConversionTest, BoolAttributeTrue) {
    parse_xml("<node value='true'/>");

    int result = taurus_element_attribute_bool(root, "value", 0);
    EXPECT_EQ(result, 1);
}

TEST_F(AttributeConversionTest, BoolAttributeFalse) {
    parse_xml("<node value='false'/>");

    int result = taurus_element_attribute_bool(root, "value", 1);
    EXPECT_EQ(result, 0);
}

TEST_F(AttributeConversionTest, BoolAttributeOne) {
    parse_xml("<node value='1'/>");

    int result = taurus_element_attribute_bool(root, "value", 0);
    EXPECT_EQ(result, 1);
}

TEST_F(AttributeConversionTest, BoolAttributeZero) {
    parse_xml("<node value='0'/>");

    int result = taurus_element_attribute_bool(root, "value", 1);
    EXPECT_EQ(result, 0);
}

TEST_F(AttributeConversionTest, BoolAttributeYes) {
    parse_xml("<node value='yes'/>");

    int result = taurus_element_attribute_bool(root, "value", 0);
    EXPECT_EQ(result, 1);
}

TEST_F(AttributeConversionTest, BoolAttributeNo) {
    parse_xml("<node value='no'/>");

    int result = taurus_element_attribute_bool(root, "value", 1);
    EXPECT_EQ(result, 0);
}

TEST_F(AttributeConversionTest, BoolAttributeMixedCase) {
    parse_xml("<node value='TrUe'/>");

    int result = taurus_element_attribute_bool(root, "value", 0);
    EXPECT_EQ(result, 1);
}

TEST_F(AttributeConversionTest, BoolAttributeMissing) {
    parse_xml("<node other='true'/>");

    int result = taurus_element_attribute_bool(root, "value", 1);
    EXPECT_EQ(result, 1);  // Default returned
}

TEST_F(AttributeConversionTest, BoolAttributeInvalid) {
    parse_xml("<node value='maybe'/>");

    int result = taurus_element_attribute_bool(root, "value", 0);
    EXPECT_EQ(result, 0);  // Default returned for invalid
}

TEST_F(AttributeConversionTest, BoolAttributeEmpty) {
    parse_xml("<node value=''/>");

    int result = taurus_element_attribute_bool(root, "value", 1);
    EXPECT_EQ(result, 1);  // Default returned for empty
}

/* ============================================================================
 * String Conversion Tests (as_string)
 * ============================================================================ */

TEST_F(AttributeConversionTest, StringAttributeValid) {
    parse_xml("<node value='hello'/>");

    const char* result = taurus_element_attribute_string(root, "value", "default");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "hello");
}

TEST_F(AttributeConversionTest, StringAttributeEmpty) {
    parse_xml("<node value=''/>");

    const char* result = taurus_element_attribute_string(root, "value", "default");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "");
}

TEST_F(AttributeConversionTest, StringAttributeMissing) {
    parse_xml("<node other='test'/>");

    const char* result = taurus_element_attribute_string(root, "value", "default");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "default");
}

TEST_F(AttributeConversionTest, StringAttributeWithSpaces) {
    parse_xml("<node value='  test  '/>");

    const char* result = taurus_element_attribute_string(root, "value", "default");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "  test  ");
}

TEST_F(AttributeConversionTest, StringAttributeSpecialChars) {
    parse_xml("<node value='&lt;&gt;&amp;'/>");

    const char* result = taurus_element_attribute_string(root, "value", "default");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "<>&");
}

/* ============================================================================
 * Combined Tests
 * ============================================================================ */

TEST_F(AttributeConversionTest, MultipleAttributes) {
    parse_xml("<node int='42' uint='100' double='3.14' float='2.71' bool='true' str='test'/>");

    EXPECT_EQ(taurus_element_attribute_int(root, "int", -1), 42);
    EXPECT_EQ(taurus_element_attribute_uint(root, "uint", 0), 100U);
    EXPECT_NEAR(taurus_element_attribute_double(root, "double", 0.0), 3.14, 0.01);
    EXPECT_NEAR(taurus_element_attribute_float(root, "float", 0.0f), 2.71f, 0.01f);
    EXPECT_EQ(taurus_element_attribute_bool(root, "bool", 0), 1);
    EXPECT_STREQ(taurus_element_attribute_string(root, "str", "default"), "test");
}

TEST_F(AttributeConversionTest, NullElementReturnsDefaults) {
    TaurusElement null_elem = ELEM_NULL();
    int int_result = taurus_element_attribute_int(null_elem, "value", 42);
    unsigned int uint_result = taurus_element_attribute_uint(null_elem, "value", 100);
    double double_result = taurus_element_attribute_double(null_elem, "value", 3.14);
    float float_result = taurus_element_attribute_float(null_elem, "value", 2.71f);
    int bool_result = taurus_element_attribute_bool(null_elem, "value", 1);
    const char* str_result = taurus_element_attribute_string(null_elem, "value", "default");

    EXPECT_EQ(int_result, 42);
    EXPECT_EQ(uint_result, 100U);
    EXPECT_NEAR(double_result, 3.14, 0.01);
    EXPECT_NEAR(float_result, 2.71f, 0.01f);
    EXPECT_EQ(bool_result, 1);
    EXPECT_STREQ(str_result, "default");
}

// ============================================================================
// Attribute Setter Tests (Type Conversion)
// ============================================================================

TEST_F(AttributeConversionTest, SetAttributeInt) {
    parse_xml("<node/>");

    EXPECT_EQ(taurus_element_set_attribute_int(root, "attr1", 42), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute_int(root, "attr1", 0), 42);

    EXPECT_EQ(taurus_element_set_attribute_int(root, "attr2", -100), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute_int(root, "attr2", 0), -100);

    EXPECT_EQ(taurus_element_set_attribute_int(root, "attr3", 0), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute_int(root, "attr3", -1), 0);

    EXPECT_EQ(taurus_element_set_attribute_int(root, "attr4", INT_MAX), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute_int(root, "attr4", 0), INT_MAX);

    EXPECT_EQ(taurus_element_set_attribute_int(root, "attr5", INT_MIN), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute_int(root, "attr5", 0), INT_MIN);
}

TEST_F(AttributeConversionTest, SetAttributeUint) {
    parse_xml("<node/>");

    EXPECT_EQ(taurus_element_set_attribute_uint(root, "attr1", 42U), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute_uint(root, "attr1", 0), 42U);

    EXPECT_EQ(taurus_element_set_attribute_uint(root, "attr2", 0U), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute_uint(root, "attr2", 999), 0U);

    EXPECT_EQ(taurus_element_set_attribute_uint(root, "attr3", UINT_MAX), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute_uint(root, "attr3", 0), UINT_MAX);
}

TEST_F(AttributeConversionTest, SetAttributeDouble) {
    parse_xml("<node/>");

    EXPECT_EQ(taurus_element_set_attribute_double(root, "attr1", 3.141592653589793), TAURUS_OK);
    EXPECT_NEAR(taurus_element_attribute_double(root, "attr1", 0.0), 3.141592653589793, 1e-10);

    EXPECT_EQ(taurus_element_set_attribute_double(root, "attr2", -2.71828), TAURUS_OK);
    EXPECT_NEAR(taurus_element_attribute_double(root, "attr2", 0.0), -2.71828, 1e-5);

    EXPECT_EQ(taurus_element_set_attribute_double(root, "attr3", 0.0), TAURUS_OK);
    EXPECT_NEAR(taurus_element_attribute_double(root, "attr3", 99.0), 0.0, 1e-10);

    EXPECT_EQ(taurus_element_set_attribute_double(root, "attr4", 1e10), TAURUS_OK);
    EXPECT_NEAR(taurus_element_attribute_double(root, "attr4", 0.0), 1e10, 1e3);
}

TEST_F(AttributeConversionTest, SetAttributeFloat) {
    parse_xml("<node/>");

    EXPECT_EQ(taurus_element_set_attribute_float(root, "attr1", 3.14159f), TAURUS_OK);
    EXPECT_NEAR(taurus_element_attribute_float(root, "attr1", 0.0f), 3.14159f, 1e-5f);

    EXPECT_EQ(taurus_element_set_attribute_float(root, "attr2", -2.718f), TAURUS_OK);
    EXPECT_NEAR(taurus_element_attribute_float(root, "attr2", 0.0f), -2.718f, 1e-5f);
}

TEST_F(AttributeConversionTest, SetAttributeBool) {
    parse_xml("<node/>");

    EXPECT_EQ(taurus_element_set_attribute_bool(root, "attr1", 1), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute_bool(root, "attr1", 0), 1);
    EXPECT_STREQ(taurus_element_attribute(root, "attr1"), "true");

    EXPECT_EQ(taurus_element_set_attribute_bool(root, "attr2", 0), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute_bool(root, "attr2", 1), 0);
    EXPECT_STREQ(taurus_element_attribute(root, "attr2"), "false");

    EXPECT_EQ(taurus_element_set_attribute_bool(root, "attr3", 100), TAURUS_OK);  /* non-zero is true */
    EXPECT_EQ(taurus_element_attribute_bool(root, "attr3", 0), 1);
}

TEST_F(AttributeConversionTest, SetAttributeUpdatesExisting) {
    parse_xml("<node attr='old'/>");

    EXPECT_EQ(taurus_element_set_attribute_int(root, "attr", 42), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute_int(root, "attr", 0), 42);

    EXPECT_EQ(taurus_element_set_attribute_double(root, "attr", 3.14), TAURUS_OK);
    EXPECT_NEAR(taurus_element_attribute_double(root, "attr", 0.0), 3.14, 0.01);

    EXPECT_EQ(taurus_element_set_attribute_bool(root, "attr", true), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute_bool(root, "attr", 0), 1);
}

TEST_F(AttributeConversionTest, SetAttributeNullElement) {
    TaurusElement null_elem = ELEM_NULL();
    EXPECT_EQ(taurus_element_set_attribute_int(null_elem, "attr", 42), TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_element_set_attribute_uint(null_elem, "attr", 42U), TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_element_set_attribute_double(null_elem, "attr", 3.14), TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_element_set_attribute_float(null_elem, "attr", 3.14f), TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_element_set_attribute_bool(null_elem, "attr", 1), TAURUS_ERROR_NULL_ARG);
}

TEST_F(AttributeConversionTest, SetAttributeNullName) {
    parse_xml("<node/>");

    EXPECT_EQ(taurus_element_set_attribute_int(root, nullptr, 42), TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_element_set_attribute_uint(root, nullptr, 42U), TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_element_set_attribute_double(root, nullptr, 3.14), TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_element_set_attribute_float(root, nullptr, 3.14f), TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_element_set_attribute_bool(root, nullptr, 1), TAURUS_ERROR_NULL_ARG);
}

TEST_F(AttributeConversionTest, MixedSetterGetter) {
    parse_xml("<node/>");

    /* Set with setter, get with getter */
    taurus_element_set_attribute_int(root, "num", 42);
    EXPECT_EQ(taurus_element_attribute_int(root, "num", 0), 42);

    taurus_element_set_attribute_uint(root, "count", 100U);
    EXPECT_EQ(taurus_element_attribute_uint(root, "count", 0), 100U);

    taurus_element_set_attribute_double(root, "price", 19.99);
    EXPECT_NEAR(taurus_element_attribute_double(root, "price", 0.0), 19.99, 0.001);

    taurus_element_set_attribute_float(root, "rate", 2.5f);
    EXPECT_NEAR(taurus_element_attribute_float(root, "rate", 0.0f), 2.5f, 0.001f);

    taurus_element_set_attribute_bool(root, "enabled", 1);
    EXPECT_EQ(taurus_element_attribute_bool(root, "enabled", 0), 1);
}

} // namespace taurus_test
