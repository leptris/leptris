/* test_xpath_functions_boolean.cc - XPath Boolean Functions W3C Tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * W3C XPath 1.0 conformance tests for boolean functions
 */

#include "xpath_test_utils.h"

using namespace taurus_test;

class XPathBooleanFunctionsTest : public XPathTestBase {
protected:
    void SetUp() override {
        XPathTestBase::SetUp();
        load_fixture("w3c/xpath/functions/boolean_tests.xml");
    }
};

/* ============================================================================
 * boolean() Function Tests - XPath 1.0 Section 4.3
 * ============================================================================ */

TEST_F(XPathBooleanFunctionsTest, Boolean_FromNumber_Positive) {
    auto result = eval_xpath("boolean(1)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Boolean_FromNumber_Zero) {
    auto result = eval_xpath("boolean(0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Boolean_FromNumber_Negative) {
    auto result = eval_xpath("boolean(-1)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Boolean_FromNumber_Decimal) {
    auto result = eval_xpath("boolean(3.14)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Boolean_FromNumber_NaN) {
    auto result = eval_xpath("boolean(0 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Boolean_FromString_NonEmpty) {
    auto result = eval_xpath("boolean('test')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Boolean_FromString_Empty) {
    auto result = eval_xpath("boolean('')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Boolean_FromString_Whitespace) {
    auto result = eval_xpath("boolean('   ')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true); // Whitespace is non-empty
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Boolean_FromString_False) {
    auto result = eval_xpath("boolean('false')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true); // String "false" is non-empty
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Boolean_FromString_Zero) {
    auto result = eval_xpath("boolean('0')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true); // String "0" is non-empty
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Boolean_FromNodeset_NonEmpty) {
    auto result = eval_xpath("boolean(//items/item)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Boolean_FromNodeset_Empty) {
    auto result = eval_xpath("boolean(//nonexistent)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Boolean_FromNodeset_Single) {
    auto result = eval_xpath("boolean(//single/element)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Boolean_FromBoolean_True) {
    auto result = eval_xpath("boolean(true())");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Boolean_FromBoolean_False) {
    auto result = eval_xpath("boolean(false())");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * not() Function Tests - XPath 1.0 Section 4.3
 * ============================================================================ */

TEST_F(XPathBooleanFunctionsTest, Not_True) {
    auto result = eval_xpath("not(true())");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Not_False) {
    auto result = eval_xpath("not(false())");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Not_Number_Zero) {
    auto result = eval_xpath("not(0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Not_Number_NonZero) {
    auto result = eval_xpath("not(1)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Not_String_Empty) {
    auto result = eval_xpath("not('')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Not_String_NonEmpty) {
    auto result = eval_xpath("not('test')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Not_Nodeset_Empty) {
    auto result = eval_xpath("not(//nonexistent)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Not_Nodeset_NonEmpty) {
    auto result = eval_xpath("not(//items/item)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Not_DoubleNegation) {
    auto result = eval_xpath("not(not(true()))");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Not_Expression) {
    auto result = eval_xpath("not(1 = 2)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * true() Function Tests - XPath 1.0 Section 4.3
 * ============================================================================ */

TEST_F(XPathBooleanFunctionsTest, True_Basic) {
    auto result = eval_xpath("true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, True_InComparison) {
    auto result = eval_xpath("true() = true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, True_WithFalse) {
    auto result = eval_xpath("true() = false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, True_And) {
    auto result = eval_xpath("true() and true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, True_Or) {
    auto result = eval_xpath("true() or false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * false() Function Tests - XPath 1.0 Section 4.3
 * ============================================================================ */

TEST_F(XPathBooleanFunctionsTest, False_Basic) {
    auto result = eval_xpath("false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, False_InComparison) {
    auto result = eval_xpath("false() = false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, False_WithTrue) {
    auto result = eval_xpath("false() = true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, False_And) {
    auto result = eval_xpath("false() and false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, False_Or) {
    auto result = eval_xpath("false() or false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * lang() Function Tests - XPath 1.0 Section 4.3
 * ============================================================================ */

TEST_F(XPathBooleanFunctionsTest, Lang_ExactMatch) {
    auto elem = taurus_element_child(root, 0); // First element with lang="en"
    for (size_t i = 0; i < taurus_element_child_count(root); i++) {
        TaurusElement child = taurus_element_child(root, i);
        const char* name = taurus_element_name(child);
        if (name && strcmp(name, "en") == 0) {
            elem = child;
            break;
        }
    }

    auto result = eval_xpath_ctx("lang('en')", elem);
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Lang_SubtagMatch) {
    // lang('en') should match lang='en-US'
    auto elem = taurus_element_child(root, 0);
    for (size_t i = 0; i < taurus_element_child_count(root); i++) {
        TaurusElement child = taurus_element_child(root, i);
        const char* name = taurus_element_name(child);
        if (name && strcmp(name, "en-us") == 0) {
            elem = child;
            break;
        }
    }

    auto result = eval_xpath_ctx("lang('en')", elem);
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Lang_NoMatch) {
    auto elem = taurus_element_child(root, 0);
    for (size_t i = 0; i < taurus_element_child_count(root); i++) {
        TaurusElement child = taurus_element_child(root, i);
        const char* name = taurus_element_name(child);
        if (name && strcmp(name, "fr") == 0) {
            elem = child;
            break;
        }
    }

    auto result = eval_xpath_ctx("lang('en')", elem);
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Lang_CaseInsensitive) {
    auto elem = taurus_element_child(root, 0);
    for (size_t i = 0; i < taurus_element_child_count(root); i++) {
        TaurusElement child = taurus_element_child(root, i);
        const char* name = taurus_element_name(child);
        if (name && strcmp(name, "en") == 0) {
            elem = child;
            break;
        }
    }

    auto result = eval_xpath_ctx("lang('EN')", elem);
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Lang_NoLangAttribute) {
    auto elem = taurus_element_child(root, 0);
    for (size_t i = 0; i < taurus_element_child_count(root); i++) {
        TaurusElement child = taurus_element_child(root, i);
        const char* name = taurus_element_name(child);
        if (name && strcmp(name, "no-lang") == 0) {
            elem = child;
            break;
        }
    }

    auto result = eval_xpath_ctx("lang('en')", elem);
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Integration Tests - Combining Boolean Functions
 * ============================================================================ */

TEST_F(XPathBooleanFunctionsTest, Integration_BooleanNot) {
    auto result = eval_xpath("not(boolean(''))");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Integration_TrueFalseAnd) {
    auto result = eval_xpath("true() and not(false())");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Integration_BooleanComparison) {
    auto result = eval_xpath("boolean(1) = true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Integration_NotWithComparison) {
    auto result = eval_xpath("not(1 < 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, Integration_BooleanNodeset) {
    auto result = eval_xpath("boolean(//items/item) and true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Logical Operator Tests (and, or)
 * ============================================================================ */

TEST_F(XPathBooleanFunctionsTest, LogicalAnd_TrueTrue) {
    auto result = eval_xpath("true() and true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, LogicalAnd_TrueFalse) {
    auto result = eval_xpath("true() and false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, LogicalAnd_FalseTrue) {
    auto result = eval_xpath("false() and true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, LogicalAnd_FalseFalse) {
    auto result = eval_xpath("false() and false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, LogicalOr_TrueTrue) {
    auto result = eval_xpath("true() or true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, LogicalOr_TrueFalse) {
    auto result = eval_xpath("true() or false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, LogicalOr_FalseTrue) {
    auto result = eval_xpath("false() or true()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, LogicalOr_FalseFalse) {
    auto result = eval_xpath("false() or false()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, LogicalAnd_ShortCircuit) {
    // false() should short-circuit, not evaluate second operand
    auto result = eval_xpath("false() and (1 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, LogicalOr_ShortCircuit) {
    // true() should short-circuit, not evaluate second operand
    auto result = eval_xpath("true() or (1 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, LogicalAnd_TypeConversion) {
    auto result = eval_xpath("1 and 2");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathBooleanFunctionsTest, LogicalOr_TypeConversion) {
    auto result = eval_xpath("0 or ''");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}