/* test_xpath_functions_number.cc - XPath Number Functions W3C Tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * W3C XPath 1.0 conformance tests for number functions
 */

#include "xpath_test_utils.h"

using namespace taurus_test;

class XPathNumberFunctionsTest : public XPathTestBase {
protected:
    void SetUp() override {
        XPathTestBase::SetUp();
        load_fixture("w3c/xpath/functions/number_tests.xml");
    }
};

/* ============================================================================
 * number() Function Tests - XPath 1.0 Section 4.4
 * ============================================================================ */

TEST_F(XPathNumberFunctionsTest, Number_FromString_Integer) {
    auto result = eval_xpath("number('42')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 42.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Number_FromString_Decimal) {
    auto result = eval_xpath("number('3.14159')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 3.14159);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Number_FromString_Negative) {
    auto result = eval_xpath("number('-17')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, -17.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Number_FromString_Zero) {
    auto result = eval_xpath("number('0')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Number_FromString_Whitespace) {
    auto result = eval_xpath("number('  123  ')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 123.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Number_FromString_Invalid_ReturnsNaN) {
    auto result = eval_xpath("number('abc')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NAN(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Number_FromString_Empty_ReturnsNaN) {
    auto result = eval_xpath("number('')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NAN(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Number_FromBoolean_True) {
    auto result = eval_xpath("number(true())");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 1.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Number_FromBoolean_False) {
    auto result = eval_xpath("number(false())");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Number_FromNodeset) {
    auto result = eval_xpath("number(//integer)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_NUMBER);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Number_NoArgs_ContextNode) {
    auto result = eval_xpath("number()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_NUMBER);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Number_LeadingZero) {
    auto result = eval_xpath("number('007')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 7.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Number_ScientificNotation) {
    auto result = eval_xpath("number('1e3')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 1000.0);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * sum() Function Tests - XPath 1.0 Section 4.4
 * ============================================================================ */

TEST_F(XPathNumberFunctionsTest, Sum_SimpleIntegers) {
    auto result = eval_xpath("sum(//numbers/num)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 15.0); // 1+2+3+4+5
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Sum_Decimals) {
    auto result = eval_xpath("sum(//decimals/val)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 7.5); // 1.5+2.5+3.5
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Sum_MixedPositiveNegative) {
    auto result = eval_xpath("sum(//mixed/val)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 8.7); // 10+(-5)+3.7
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Sum_EmptyNodeset) {
    auto result = eval_xpath("sum(//nonexistent)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Sum_SingleNode) {
    auto result = eval_xpath("sum(//integer)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 42.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Sum_NonNumericNodes_ReturnsNaN) {
    auto result = eval_xpath("sum(//non-numeric)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NAN(result);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * floor() Function Tests - XPath 1.0 Section 4.4
 * ============================================================================ */

TEST_F(XPathNumberFunctionsTest, Floor_PositiveDecimal) {
    auto result = eval_xpath("floor(3.7)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 3.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Floor_NegativeDecimal) {
    auto result = eval_xpath("floor(-3.7)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, -4.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Floor_Integer) {
    auto result = eval_xpath("floor(5)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 5.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Floor_Zero) {
    auto result = eval_xpath("floor(0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Floor_PositiveHalf) {
    auto result = eval_xpath("floor(2.5)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 2.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Floor_NegativeHalf) {
    auto result = eval_xpath("floor(-2.5)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, -3.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Floor_NaN) {
    auto result = eval_xpath("floor(0 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NAN(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Floor_Infinity) {
    auto result = eval_xpath("floor(1 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_INFINITY(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Floor_NegativeInfinity) {
    auto result = eval_xpath("floor(-1 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NEG_INFINITY(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Floor_VerySmall) {
    auto result = eval_xpath("floor(0.1)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Floor_VerySmallNegative) {
    auto result = eval_xpath("floor(-0.1)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, -1.0);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * ceiling() Function Tests - XPath 1.0 Section 4.4
 * ============================================================================ */

TEST_F(XPathNumberFunctionsTest, Ceiling_PositiveDecimal) {
    auto result = eval_xpath("ceiling(3.2)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 4.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Ceiling_NegativeDecimal) {
    auto result = eval_xpath("ceiling(-3.2)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, -3.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Ceiling_Integer) {
    auto result = eval_xpath("ceiling(5)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 5.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Ceiling_Zero) {
    auto result = eval_xpath("ceiling(0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Ceiling_PositiveHalf) {
    auto result = eval_xpath("ceiling(2.5)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 3.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Ceiling_NegativeHalf) {
    auto result = eval_xpath("ceiling(-2.5)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, -2.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Ceiling_NaN) {
    auto result = eval_xpath("ceiling(0 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NAN(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Ceiling_Infinity) {
    auto result = eval_xpath("ceiling(1 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_INFINITY(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Ceiling_NegativeInfinity) {
    auto result = eval_xpath("ceiling(-1 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NEG_INFINITY(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Ceiling_VerySmall) {
    auto result = eval_xpath("ceiling(0.1)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 1.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Ceiling_VerySmallNegative) {
    auto result = eval_xpath("ceiling(-0.1)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * round() Function Tests - XPath 1.0 Section 4.4
 * ============================================================================ */

TEST_F(XPathNumberFunctionsTest, Round_PositiveLow) {
    auto result = eval_xpath("round(3.4)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 3.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Round_PositiveHigh) {
    auto result = eval_xpath("round(3.6)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 4.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Round_NegativeLow) {
    auto result = eval_xpath("round(-3.4)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, -3.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Round_NegativeHigh) {
    auto result = eval_xpath("round(-3.6)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, -4.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Round_PositiveHalf) {
    // XPath 1.0 rounds 0.5 up to 1
    auto result = eval_xpath("round(3.5)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 4.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Round_NegativeHalf) {
    // XPath 1.0 rounds -0.5 up to 0
    auto result = eval_xpath("round(-3.5)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, -3.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Round_Integer) {
    auto result = eval_xpath("round(5)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 5.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Round_Zero) {
    auto result = eval_xpath("round(0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Round_NaN) {
    auto result = eval_xpath("round(0 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NAN(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Round_Infinity) {
    auto result = eval_xpath("round(1 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_INFINITY(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Round_NegativeInfinity) {
    auto result = eval_xpath("round(-1 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NEG_INFINITY(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Round_VerySmall) {
    auto result = eval_xpath("round(0.1)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Round_VerySmallNegative) {
    auto result = eval_xpath("round(-0.1)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Integration Tests - Combining Number Functions
 * ============================================================================ */

TEST_F(XPathNumberFunctionsTest, Integration_FloorCeiling) {
    auto result = eval_xpath("floor(3.7) + ceiling(3.2)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 7.0); // 3 + 4
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Integration_RoundSum) {
    auto result = eval_xpath("round(sum(//decimals/val))");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 8.0); // round(7.5)
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Integration_NumberFloor) {
    auto result = eval_xpath("floor(number('3.9'))");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 3.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Integration_SumDivide) {
    auto result = eval_xpath("sum(//numbers/num) div 5");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 3.0); // 15 / 5
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, Integration_RoundNegative) {
    auto result = eval_xpath("round(-1 * 3.7)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, -4.0);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Edge Cases
 * ============================================================================ */

TEST_F(XPathNumberFunctionsTest, EdgeCase_DivisionByZero) {
    auto result = eval_xpath("1 div 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_INFINITY(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, EdgeCase_NegativeDivisionByZero) {
    auto result = eval_xpath("-1 div 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NEG_INFINITY(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, EdgeCase_ZeroDivisionByZero) {
    auto result = eval_xpath("0 div 0");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NAN(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, EdgeCase_InfinityOperations) {
    auto result = eval_xpath("(1 div 0) + (1 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_INFINITY(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNumberFunctionsTest, EdgeCase_NaNOperations) {
    auto result = eval_xpath("(0 div 0) + 1");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NAN(result);
    taurus_xpath_result_free(result);
}