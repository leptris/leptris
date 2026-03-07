/* test_xpath_functions_string.cc - XPath String Functions W3C Tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * W3C XPath 1.0 conformance tests for string functions
 */

#include "xpath_test_utils.h"

using namespace taurus_test;

class XPathStringFunctionsTest : public XPathTestBase {
protected:
    void SetUp() override {
        XPathTestBase::SetUp();
        load_fixture("w3c/xpath/functions/string_tests.xml");
    }
};

/* ============================================================================
 * string() Function Tests - XPath 1.0 Section 4.2
 * ============================================================================ */

TEST_F(XPathStringFunctionsTest, String_FromNumber) {
    auto result = eval_xpath("string(123)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "123");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, String_FromDecimal) {
    auto result = eval_xpath("string(456.789)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "456.789");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, String_FromBoolean_True) {
    auto result = eval_xpath("string(true())");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "true");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, String_FromBoolean_False) {
    auto result = eval_xpath("string(false())");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "false");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, String_FromNodeset) {
    auto result = eval_xpath("string(//text)");
    ASSERT_NE(result, nullptr);
    // Should return string value of first node
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_STRING);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, String_NoArgs_ContextNode) {
    auto result = eval_xpath("string()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_STRING);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, String_EmptyString) {
    auto result = eval_xpath("string('')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, String_FromNaN) {
    auto result = eval_xpath("string(0 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "NaN");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, String_FromInfinity) {
    auto result = eval_xpath("string(1 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "Infinity");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, String_FromNegativeInfinity) {
    auto result = eval_xpath("string(-1 div 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "-Infinity");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * concat() Function Tests - XPath 1.0 Section 4.2
 * ============================================================================ */

TEST_F(XPathStringFunctionsTest, Concat_TwoStrings) {
    auto result = eval_xpath("concat('Hello', ' World')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "Hello World");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Concat_ThreeStrings) {
    auto result = eval_xpath("concat('Hello', ' ', 'World')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "Hello World");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Concat_FourStrings) {
    auto result = eval_xpath("concat('A', 'B', 'C', 'D')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "ABCD");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Concat_WithNumbers) {
    auto result = eval_xpath("concat('Count: ', 42)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "Count: 42");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Concat_EmptyStrings) {
    auto result = eval_xpath("concat('', 'test', '')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "test");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Concat_WithBoolean) {
    auto result = eval_xpath("concat('Result: ', true())");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "Result: true");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * starts-with() Function Tests - XPath 1.0 Section 4.2
 * ============================================================================ */

TEST_F(XPathStringFunctionsTest, StartsWith_True) {
    auto result = eval_xpath("starts-with('Hello World', 'Hello')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, StartsWith_False) {
    auto result = eval_xpath("starts-with('Hello World', 'World')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, StartsWith_EmptyPrefix) {
    auto result = eval_xpath("starts-with('Hello', '')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, StartsWith_EmptyString) {
    auto result = eval_xpath("starts-with('', 'test')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, StartsWith_ExactMatch) {
    auto result = eval_xpath("starts-with('test', 'test')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, StartsWith_CaseSensitive) {
    auto result = eval_xpath("starts-with('Hello', 'hello')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * contains() Function Tests - XPath 1.0 Section 4.2
 * ============================================================================ */

TEST_F(XPathStringFunctionsTest, Contains_Found) {
    auto result = eval_xpath("contains('Hello World', 'Wor')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Contains_NotFound) {
    auto result = eval_xpath("contains('Hello World', 'xyz')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Contains_EmptySubstring) {
    auto result = eval_xpath("contains('test', '')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Contains_AtStart) {
    auto result = eval_xpath("contains('testing', 'test')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Contains_AtEnd) {
    auto result = eval_xpath("contains('testing', 'ing')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Contains_CaseSensitive) {
    auto result = eval_xpath("contains('Hello', 'hello')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, false);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * substring() Function Tests - XPath 1.0 Section 4.2
 * ============================================================================ */

TEST_F(XPathStringFunctionsTest, Substring_TwoArgs_FromStart) {
    auto result = eval_xpath("substring('ABCDEFG', 1)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "ABCDEFG");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Substring_TwoArgs_FromMiddle) {
    auto result = eval_xpath("substring('ABCDEFG', 4)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "DEFG");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Substring_ThreeArgs) {
    auto result = eval_xpath("substring('ABCDEFG', 2, 3)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "BCD");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Substring_ThreeArgs_ExactLength) {
    auto result = eval_xpath("substring('12345', 1, 5)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "12345");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Substring_ZeroLength) {
    auto result = eval_xpath("substring('test', 1, 0)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Substring_PastEnd) {
    auto result = eval_xpath("substring('test', 10)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Substring_NegativeStart) {
    auto result = eval_xpath("substring('ABCDEFG', -2, 5)");
    ASSERT_NE(result, nullptr);
    /* XPath spec: start < 1 is clamped to 1, so we get 5 chars from position 1 */
    EXPECT_XPATH_STRING(result, "ABCDE");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Substring_LengthExceedsString) {
    auto result = eval_xpath("substring('test', 2, 100)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "est");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * substring-before() Function Tests - XPath 1.0 Section 4.2
 * ============================================================================ */

TEST_F(XPathStringFunctionsTest, SubstringBefore_Found) {
    auto result = eval_xpath("substring-before('Hello-World', '-')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "Hello");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, SubstringBefore_NotFound) {
    auto result = eval_xpath("substring-before('Hello', '-')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, SubstringBefore_AtStart) {
    auto result = eval_xpath("substring-before('-test', '-')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, SubstringBefore_Multiple) {
    auto result = eval_xpath("substring-before('a:b:c', ':')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "a");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, SubstringBefore_EmptyDelimiter) {
    auto result = eval_xpath("substring-before('test', '')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * substring-after() Function Tests - XPath 1.0 Section 4.2
 * ============================================================================ */

TEST_F(XPathStringFunctionsTest, SubstringAfter_Found) {
    auto result = eval_xpath("substring-after('Hello-World', '-')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "World");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, SubstringAfter_NotFound) {
    auto result = eval_xpath("substring-after('Hello', '-')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, SubstringAfter_AtEnd) {
    auto result = eval_xpath("substring-after('test-', '-')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, SubstringAfter_Multiple) {
    auto result = eval_xpath("substring-after('a:b:c', ':')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "b:c");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, SubstringAfter_EmptyDelimiter) {
    auto result = eval_xpath("substring-after('test', '')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "test");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * string-length() Function Tests - XPath 1.0 Section 4.2
 * ============================================================================ */

TEST_F(XPathStringFunctionsTest, StringLength_Basic) {
    auto result = eval_xpath("string-length('Hello')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 5.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, StringLength_Empty) {
    auto result = eval_xpath("string-length('')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, StringLength_Whitespace) {
    auto result = eval_xpath("string-length('   ')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 3.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, StringLength_NoArgs_ContextNode) {
    auto result = eval_xpath("string-length()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_NUMBER);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, StringLength_WithSpecialChars) {
    auto result = eval_xpath("string-length('a\nb\tc')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 5.0);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * normalize-space() Function Tests - XPath 1.0 Section 4.2
 * ============================================================================ */

TEST_F(XPathStringFunctionsTest, NormalizeSpace_Leading) {
    auto result = eval_xpath("normalize-space('   Hello')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "Hello");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, NormalizeSpace_Trailing) {
    auto result = eval_xpath("normalize-space('Hello   ')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "Hello");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, NormalizeSpace_Multiple) {
    auto result = eval_xpath("normalize-space('  Hello   World  ')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "Hello World");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, NormalizeSpace_OnlyWhitespace) {
    auto result = eval_xpath("normalize-space('     ')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, NormalizeSpace_Tabs) {
    auto result = eval_xpath("normalize-space('\tHello\tWorld\t')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "Hello World");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, NormalizeSpace_Newlines) {
    auto result = eval_xpath("normalize-space('Hello\nWorld')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "Hello World");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, NormalizeSpace_NoWhitespace) {
    auto result = eval_xpath("normalize-space('HelloWorld')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "HelloWorld");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * translate() Function Tests - XPath 1.0 Section 4.2
 * ============================================================================ */

TEST_F(XPathStringFunctionsTest, Translate_Basic) {
    auto result = eval_xpath("translate('abc', 'abc', 'ABC')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "ABC");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Translate_PartialMatch) {
    auto result = eval_xpath("translate('abcd', 'abc', 'ABC')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "ABCd");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Translate_Remove) {
    auto result = eval_xpath("translate('abcd', 'bd', '')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "ac");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Translate_MismatchedLength) {
    auto result = eval_xpath("translate('abc', 'abc', 'AB')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "AB");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Translate_NoMatch) {
    auto result = eval_xpath("translate('xyz', 'abc', 'ABC')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "xyz");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Translate_Empty) {
    auto result = eval_xpath("translate('', 'abc', 'ABC')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Translate_Digits) {
    auto result = eval_xpath("translate('2+2=4', '0123456789', '9876543210')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "7+7=5");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Integration Tests - Combining Multiple Functions
 * ============================================================================ */

TEST_F(XPathStringFunctionsTest, Integration_ConcatSubstring) {
    auto result = eval_xpath("concat(substring('Hello', 1, 2), substring('World', 1, 2))");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "HeWo");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Integration_NormalizeConcat) {
    auto result = eval_xpath("normalize-space(concat('  Hello  ', '  World  '))");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "Hello World");
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Integration_LengthSubstring) {
    auto result = eval_xpath("string-length(substring('ABCDEFG', 2, 3))");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 3.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathStringFunctionsTest, Integration_ContainsSubstring) {
    auto result = eval_xpath("contains(substring('Hello World', 7), 'Wor')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_BOOLEAN(result, true);
    taurus_xpath_result_free(result);
}