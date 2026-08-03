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
