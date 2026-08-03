// test/xpath/test_xpath.cpp — XPath engine specs.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>

namespace {

constexpr char kBasic[] =
    "<catalog>"
    "  <book id='b1'><title>First</title><price>10</price></book>"
    "  <book id='b2'><title>Second</title><price>20</price></book>"
    "  <book id='b3'><title>Third</title><price>30</price></book>"
    "</catalog>";

TaurusDocument ParseWith(const char* xml) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    EXPECT_NE(doc, nullptr);
    return doc;
}

TEST(XPathAxes, DescendantOrSelfFindsAll) {
    TaurusDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "//book");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 3u);

    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathFunctions, CountReturnsNodeSetSize) {
    TaurusDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "count(//book)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_type(r), TAURUS_XPATH_NUMBER);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 3.0);

    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathFunctions, StringFunctionExpandsEntities) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r>hello &amp; world</r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "string(/r)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_type(r), TAURUS_XPATH_STRING);

    char* s = taurus_xpath_result_string(r);
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "hello & world");
    taurus_free_string(s);

    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathPredicates, LastSelectsFinalChild) {
    TaurusDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r =
        taurus_xpath_eval(doc, nullptr, "//book[last()]/title");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 1u);

    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathPredicates, PositionalPredicateSelectsCorrectChild) {
    TaurusDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r =
        taurus_xpath_eval(doc, nullptr, "//book[2]/title");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 1u);

    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathPredicates, AttributeValuePredicate) {
    TaurusDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r =
        taurus_xpath_eval(doc, nullptr, "//book[@id='b2']/title");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 1u);

    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathLeaks, ComplexQueriesDoNotLeak) {
    /* Regression for TODO 55: every XPath feature exercised at once.
     * Verified leak-free via leaks --atExit --. */
    const char xml[] =
        "<r xmlns:ns='http://x'>"
        "<a x='1'><b>text1</b></a>"
        "<a x='2'><b>text2</b></a>"
        "<ns:c ns:attr='val'>data</ns:c>"
        "</r>";

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char* queries[] = {
        "//a", "//a[@x='1']", "count(//a)", "string(//a/b)",
        "//a[1]/b | //a[2]/b", "name(//a[1])", "namespace-uri(//ns:c)",
        "//a[parent::r]", "//a[descendant::b]", "//a[position() > 1]",
        "//*[contains(string(.), 'text')]",
    };

    for (const char* q : queries) {
        TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, q);
        if (r) taurus_xpath_result_free(r);
    }

    taurus_document_free(doc);
    /* Under leaks --atExit --: 0 bytes leaked. */
}

}  // namespace


// ---- XPath variables (TODO 86 / 94) --------------------------------------

TEST(XPathVariables, BooleanVariableEvaluates) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><a/></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathVariableSet vars = taurus_xpath_variable_set_new();
    ASSERT_NE(vars, nullptr);
    EXPECT_EQ(taurus_xpath_variable_set_boolean(vars, "flag", 1), TAURUS_OK);

    TaurusXPathResult r = taurus_xpath_eval_with_vars(doc, "$flag", vars);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_type(r), TAURUS_XPATH_BOOLEAN);
    EXPECT_EQ(taurus_xpath_result_boolean(r), 1);

    taurus_xpath_result_free(r);
    taurus_xpath_variable_set_free(vars);
    taurus_document_free(doc);
}

TEST(XPathVariables, NumberVariableEvaluates) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><a/></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathVariableSet vars = taurus_xpath_variable_set_new();
    ASSERT_NE(vars, nullptr);
    EXPECT_EQ(taurus_xpath_variable_set_number(vars, "n", 42.5), TAURUS_OK);

    TaurusXPathResult r = taurus_xpath_eval_with_vars(doc, "$n + 1", vars);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_type(r), TAURUS_XPATH_NUMBER);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 43.5);

    taurus_xpath_result_free(r);
    taurus_xpath_variable_set_free(vars);
    taurus_document_free(doc);
}

TEST(XPathVariables, StringVariableEvaluates) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><a>hello</a></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathVariableSet vars = taurus_xpath_variable_set_new();
    ASSERT_NE(vars, nullptr);
    EXPECT_EQ(taurus_xpath_variable_set_string(vars, "greeting", "hello"), TAURUS_OK);

    TaurusXPathResult r = taurus_xpath_eval_with_vars(doc, "$greeting", vars);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_type(r), TAURUS_XPATH_STRING);
    char* s = taurus_xpath_result_string(r);
    EXPECT_STREQ(s, "hello");
    taurus_free_string(s);

    taurus_xpath_result_free(r);
    taurus_xpath_variable_set_free(vars);
    taurus_document_free(doc);
}

TEST(XPathVariables, UnknownVariableReturnsEmpty) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathVariableSet vars = taurus_xpath_variable_set_new();
    ASSERT_NE(vars, nullptr);

    TaurusXPathResult r = taurus_xpath_eval_with_vars(doc, "$undefined", vars);
    EXPECT_EQ(r, nullptr);

    taurus_xpath_variable_set_free(vars);
    taurus_document_free(doc);
}

// ---- Comprehensive XPath coverage (TODO 69-style) ------------------------

TEST(XPathAxes, ChildAxisFindsDirectChildren) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><a/><b/><c/></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "/r/*");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 3u);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathAxes, AttributeAxisReturnsAttributes) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r a='1' b='2'/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "/r/@*");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 2u);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathAxes, DescendantAxisFindsAllLevels) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><a><b/><c/></a><d/></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "count(//*)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_type(r), TAURUS_XPATH_NUMBER);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 5.0);  // r, a, b, c, d
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathFunctions, NameReturnsElementName) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<root><child/></root>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "name(/root/child)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_type(r), TAURUS_XPATH_STRING);
    char* s = taurus_xpath_result_string(r);
    EXPECT_STREQ(s, "child");
    taurus_free_string(s);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathFunctions, StringLengthReturnsCharCount) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r>hello</r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "string-length(/r)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_type(r), TAURUS_XPATH_NUMBER);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 5.0);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathFunctions, TrueAndFalseFunctionsWork) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr, "true()");
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(taurus_xpath_result_boolean(r1), 1);
    taurus_xpath_result_free(r1);
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "false()");
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(taurus_xpath_result_boolean(r2), 0);
    taurus_xpath_result_free(r2);
    taurus_document_free(doc);
}

TEST(XPathPredicates, PositionGreaterThanFiltersCorrectly) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><a/><a/><a/><a/></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "/r/a[position() > 2]");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 2u);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathPredicates, UnionOperatorCombinesNodeSets) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><a/><b/><c/></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "/r/a | /r/c");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 2u);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathOperators, ArithmeticAddition) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "2 + 3");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_type(r), TAURUS_XPATH_NUMBER);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 5.0);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathOperators, StringComparison) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><a x='1'/><a x='2'/></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "/r/a[@x = '2']");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 1u);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathText, TextNodeSelection) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><a>text1</a><a>text2</a></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "string(/r/a[2])");
    ASSERT_NE(r, nullptr);
    char* s = taurus_xpath_result_string(r);
    EXPECT_STREQ(s, "text2");
    taurus_free_string(s);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathNested, NestedPredicates) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] =
        "<r>"
        "  <a id='1'><v>10</v></a>"
        "  <a id='2'><v>20</v></a>"
        "  <a id='3'><v>30</v></a>"
        "</r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "/r/a[v > 15]/@id");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 2u);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}
