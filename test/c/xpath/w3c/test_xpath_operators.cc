/* test_xpath_operators.cc - XPath Operators W3C Tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * W3C XPath 1.0 conformance tests for all 15 operators
 */

#include "xpath_test_utils.h"

using namespace taurus_test;

class XPathOperatorsTest : public XPathTestBase {
protected:
    void SetUp() override {
        XPathTestBase::SetUp();
        load_fixture("w3c/xpath/operators/operator_tests.xml");
    }
};

/* ============================================================================
 * Logical Operators - XPath 1.0 Section 3.4
 * ============================================================================ */

// and operator
TEST_F(XPathOperatorsTest, And_TrueTrue) {
    auto result = eval_xpath("true() and true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, And_TrueFalse) {
    auto result = eval_xpath("true() and false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, And_FalseTrue) {
    auto result = eval_xpath("false() and true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, And_FalseFalse) {
    auto result = eval_xpath("false() and false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, And_WithNumbers) {
    auto result = eval_xpath("1 and 2");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, And_WithZero) {
    auto result = eval_xpath("1 and 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, And_WithStrings) {
    auto result = eval_xpath("'hello' and 'world'");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, And_WithEmptyString) {
    auto result = eval_xpath("'hello' and ''");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, And_WithNodeset) {
    auto result = eval_xpath("//value and true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, And_WithEmptyNodeset) {
    auto result = eval_xpath("//nonexistent and true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

// or operator
TEST_F(XPathOperatorsTest, Or_TrueTrue) {
    auto result = eval_xpath("true() or true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Or_TrueFalse) {
    auto result = eval_xpath("true() or false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Or_FalseTrue) {
    auto result = eval_xpath("false() or true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Or_FalseFalse) {
    auto result = eval_xpath("false() or false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Or_WithNumbers) {
    auto result = eval_xpath("0 or 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Or_WithZero) {
    auto result = eval_xpath("1 or 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Or_WithStrings) {
    auto result = eval_xpath("'' or ''");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Or_WithEmptyString) {
    auto result = eval_xpath("'hello' or ''");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Or_WithNodeset) {
    auto result = eval_xpath("//value or false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Or_WithEmptyNodeset) {
    auto result = eval_xpath("//nonexistent or false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Equality Operators - XPath 1.0 Section 3.4
 * ============================================================================ */

// = operator
TEST_F(XPathOperatorsTest, Equal_Numbers) {
    auto result = eval_xpath("5 = 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Equal_NumbersUnequal) {
    auto result = eval_xpath("5 = 10");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Equal_Strings) {
    auto result = eval_xpath("'hello' = 'hello'");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Equal_StringsUnequal) {
    auto result = eval_xpath("'hello' = 'world'");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Equal_Booleans) {
    auto result = eval_xpath("true() = true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Equal_BooleansUnequal) {
    auto result = eval_xpath("true() = false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Equal_NumberString) {
    auto result = eval_xpath("5 = '5'");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Equal_NumberBoolean) {
    auto result = eval_xpath("1 = true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Equal_Nodeset) {
    auto result = eval_xpath("//value[@id='v1'] = 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Equal_EmptyNodeset) {
    auto result = eval_xpath("//nonexistent = 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

// != operator
TEST_F(XPathOperatorsTest, NotEqual_Numbers) {
    auto result = eval_xpath("5 != 10");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, NotEqual_NumbersEqual) {
    auto result = eval_xpath("5 != 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, NotEqual_Strings) {
    auto result = eval_xpath("'hello' != 'world'");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, NotEqual_StringsEqual) {
    auto result = eval_xpath("'hello' != 'hello'");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, NotEqual_Booleans) {
    auto result = eval_xpath("true() != false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Relational Operators - XPath 1.0 Section 3.4
 * ============================================================================ */

// < operator
TEST_F(XPathOperatorsTest, LessThan_True) {
    auto result = eval_xpath("5 < 10");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, LessThan_False) {
    auto result = eval_xpath("10 < 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, LessThan_Equal) {
    auto result = eval_xpath("5 < 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, LessThan_Negative) {
    auto result = eval_xpath("-3 < 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, LessThan_Strings) {
    auto result = eval_xpath("'5' < '10'");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

// <= operator
TEST_F(XPathOperatorsTest, LessThanOrEqual_Less) {
    auto result = eval_xpath("5 <= 10");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, LessThanOrEqual_Equal) {
    auto result = eval_xpath("5 <= 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, LessThanOrEqual_Greater) {
    auto result = eval_xpath("10 <= 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, LessThanOrEqual_Negative) {
    auto result = eval_xpath("-3 <= 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, LessThanOrEqual_Zero) {
    auto result = eval_xpath("0 <= 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

// > operator
TEST_F(XPathOperatorsTest, GreaterThan_True) {
    auto result = eval_xpath("10 > 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, GreaterThan_False) {
    auto result = eval_xpath("5 > 10");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, GreaterThan_Equal) {
    auto result = eval_xpath("5 > 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, GreaterThan_Negative) {
    auto result = eval_xpath("0 > -3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, GreaterThan_Strings) {
    auto result = eval_xpath("'10' > '5'");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

// >= operator
TEST_F(XPathOperatorsTest, GreaterThanOrEqual_Greater) {
    auto result = eval_xpath("10 >= 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, GreaterThanOrEqual_Equal) {
    auto result = eval_xpath("5 >= 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, GreaterThanOrEqual_Less) {
    auto result = eval_xpath("5 >= 10");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, GreaterThanOrEqual_Negative) {
    auto result = eval_xpath("0 >= -3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, GreaterThanOrEqual_Zero) {
    auto result = eval_xpath("0 >= 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Arithmetic Operators - XPath 1.0 Section 3.5
 * ============================================================================ */

// + operator
TEST_F(XPathOperatorsTest, Add_Positive) {
    auto result = eval_xpath("5 + 3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 8.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Add_Negative) {
    auto result = eval_xpath("5 + -3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 2.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Add_Zero) {
    auto result = eval_xpath("5 + 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 5.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Add_Decimals) {
    auto result = eval_xpath("2.5 + 3.5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 6.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Add_Strings) {
    auto result = eval_xpath("'5' + '3'");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 8.0);
    taurus_xpath_result_free(result);
}

// - operator
TEST_F(XPathOperatorsTest, Subtract_Positive) {
    auto result = eval_xpath("10 - 3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 7.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Subtract_Negative) {
    auto result = eval_xpath("5 - -3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 8.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Subtract_Zero) {
    auto result = eval_xpath("5 - 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 5.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Subtract_ResultNegative) {
    auto result = eval_xpath("3 - 10");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, -7.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Subtract_Decimals) {
    auto result = eval_xpath("7.5 - 2.5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 5.0);
    taurus_xpath_result_free(result);
}

// * operator
TEST_F(XPathOperatorsTest, Multiply_Positive) {
    auto result = eval_xpath("5 * 3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 15.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Multiply_Negative) {
    auto result = eval_xpath("5 * -3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, -15.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Multiply_Zero) {
    auto result = eval_xpath("5 * 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Multiply_Decimals) {
    auto result = eval_xpath("2.5 * 3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 7.5);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Multiply_NegativeNegative) {
    auto result = eval_xpath("-5 * -3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 15.0);
    taurus_xpath_result_free(result);
}

// div operator
TEST_F(XPathOperatorsTest, Divide_Positive) {
    auto result = eval_xpath("10 div 2");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 5.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Divide_Negative) {
    auto result = eval_xpath("10 div -2");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, -5.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Divide_Decimals) {
    auto result = eval_xpath("7.5 div 2.5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 3.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Divide_ByZero) {
    auto result = eval_xpath("10 div 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_INFINITY(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Divide_NegativeByZero) {
    auto result = eval_xpath("-10 div 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NEG_INFINITY(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Divide_ZeroByZero) {
    auto result = eval_xpath("0 div 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NAN(result);
    taurus_xpath_result_free(result);
}

// mod operator
TEST_F(XPathOperatorsTest, Mod_Positive) {
    auto result = eval_xpath("10 mod 3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 1.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Mod_Negative) {
    auto result = eval_xpath("10 mod -3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 1.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Mod_NegativeDividend) {
    auto result = eval_xpath("-10 mod 3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, -1.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Mod_EvenDivision) {
    auto result = eval_xpath("10 mod 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Mod_Decimals) {
    auto result = eval_xpath("5.5 mod 2");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 1.5);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Union Operator - XPath 1.0 Section 3.3
 * ============================================================================ */

TEST_F(XPathOperatorsTest, Union_TwoNodesets) {
    auto result = eval_xpath("//value[@id='v1'] | //value[@id='v2']");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 2);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Union_MultipleNodesets) {
    auto result = eval_xpath("//value[@id='v1'] | //value[@id='v2'] | //value[@id='v3']");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 3);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Union_WithEmpty) {
    auto result = eval_xpath("//value[@id='v1'] | //nonexistent");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Union_Overlapping) {
    auto result = eval_xpath("//value | //value[@id='v1']");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 6);  // Deduplication
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Union_DifferentPaths) {
    auto result = eval_xpath("//value | //text");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 10);  // 6 values + 4 texts
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Union_EmptyNodesets) {
    auto result = eval_xpath("//nonexistent1 | //nonexistent2");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_EMPTY_NODESET(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Union_WithAttributes) {
    auto result = eval_xpath("//item/@attr | //value[@id='v1']");
    ASSERT_NE(result, nullptr);
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GT(count, 0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Union_DocumentOrder) {
    auto result = eval_xpath("//value[@id='v2'] | //value[@id='v1']");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 2);
    // First result should be v1 (earlier in document order)
    auto name = get_nodeset_element_text(result, 0);
    EXPECT_EQ(name, "5");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Operator Precedence Tests - XPath 1.0 Section 3.7
 * ============================================================================ */

TEST_F(XPathOperatorsTest, Precedence_MultiplyAdd) {
    auto result = eval_xpath("2 + 3 * 4");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 14.0);  // 2 + 12, not 20
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Precedence_AddMultiply) {
    auto result = eval_xpath("3 * 4 + 2");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 14.0);  // 12 + 2
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Precedence_DivideSubtract) {
    auto result = eval_xpath("10 - 6 div 2");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 7.0);  // 10 - 3
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Precedence_ComparisonAnd) {
    auto result = eval_xpath("5 > 3 and 2 < 4");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Precedence_ComparisonOr) {
    auto result = eval_xpath("5 > 10 or 2 < 4");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Precedence_ArithmeticComparison) {
    auto result = eval_xpath("3 + 2 > 4");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Precedence_MultipleMultiply) {
    auto result = eval_xpath("2 * 3 * 4");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 24.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Precedence_MixedArithmetic) {
    auto result = eval_xpath("10 + 5 * 2 - 3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 17.0);  // 10 + 10 - 3
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Precedence_ModDiv) {
    auto result = eval_xpath("10 mod 3 * 2");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 2.0);  // 1 * 2
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Precedence_EqualityRelational) {
    auto result = eval_xpath("5 > 3 = true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Type Conversion Tests
 * ============================================================================ */

TEST_F(XPathOperatorsTest, TypeConversion_StringNumber) {
    auto result = eval_xpath("'10' + 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 15.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, TypeConversion_BooleanNumber) {
    auto result = eval_xpath("true() = 1");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, TypeConversion_NumberString) {
    auto result = eval_xpath("5 = '5'");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, TypeConversion_InvalidString) {
    auto result = eval_xpath("'abc' + 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NAN(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, TypeConversion_EmptyString) {
    // Per XPath 1.0 spec: empty string doesn't match number pattern -> NaN
    // NaN + 5 = NaN (verified against libxml2 behavior)
    auto result = eval_xpath("'' + 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NAN(result);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Complex Expression Tests
 * ============================================================================ */

TEST_F(XPathOperatorsTest, Complex_LogicalAndArithmetic) {
    auto result = eval_xpath("(5 + 3) > 7 and (10 - 2) < 9");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Complex_UnionWithPredicate) {
    auto result = eval_xpath("//value[@id='v1'] | //value[@id='v2' and number(.) = 10]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 2);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Complex_MultipleComparisons) {
    auto result = eval_xpath("5 > 3 and 10 < 20 and 7 = 7");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Complex_NodesetComparison) {
    auto result = eval_xpath("//value[@id='v1'] = 5 and //value[@id='v2'] = 10");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathOperatorsTest, Complex_MixedOperators) {
    auto result = eval_xpath("(5 + 3) * 2 > 15 or 10 div 2 = 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}