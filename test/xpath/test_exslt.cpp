// test/xpath/test_exslt.cpp — first-party EXSLT-style pack
// (TODO.concurrency/06).

#include <gtest/gtest.h>
#include "leptris.h"
#include <cstring>
#include <string>

namespace {

constexpr char kItems[] =
    "<items><item n='3'>alpha</item><item n='7'>beta</item>"
    "<item n='5'>gamma</item></items>";

LeptrisDocument MakeDoc() {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(kItems, std::strlen(kItems), &st);
    EXPECT_EQ(leptris_exslt_enable(doc), LEPTRIS_OK);
    return doc;
}

std::string EvalStr(LeptrisDocument doc, const char* q) {
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, q);
    if (!r) return "(null)";
    char* s = leptris_xpath_result_string(r);
    std::string out = s ? s : "";
    if (s) leptris_free_string(s);
    leptris_xpath_result_free(r);
    return out;
}

std::string Tokens(LeptrisDocument doc, const char* q) {
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, q);
    if (!r) return "(null)";
    std::string out;
    for (size_t i = 0; i < leptris_xpath_result_count(r); i++) {
        if (!out.empty()) out += '|';
        const char* v = leptris_xpath_result_node_value(r, i);
        out += v ? v : "";
    }
    leptris_xpath_result_free(r);
    return out;
}

double EvalNum(LeptrisDocument doc, const char* q) {
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, q);
    if (!r) return -999;
    double v = leptris_xpath_result_number(r);
    leptris_xpath_result_free(r);
    return v;
}

}  // namespace

TEST(Exslt, DisabledByDefault) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(kItems, std::strlen(kItems), &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(leptris_xpath_eval(doc, nullptr, "str:replace('a','-','')"),
              nullptr);
    leptris_document_free(doc);
}

TEST(Exslt, StrFunctions) {
    LeptrisDocument doc = MakeDoc();
    ASSERT_NE(doc, nullptr);

    EXPECT_EQ(EvalStr(doc, "str:replace('a-b-c','-','+')"), "a+b+c");
    EXPECT_EQ(EvalStr(doc, "str:replace('aaa','aa','b')"), "ba");
    EXPECT_EQ(EvalStr(doc, "str:replace('x','zz','y')"), "x");
    EXPECT_EQ(EvalStr(doc, "str:concat(//item)"), "alphabetagamma");
    EXPECT_EQ(EvalStr(doc, "str:padding(5,'x')"), "xxxxx");
    EXPECT_EQ(EvalStr(doc, "str:padding(3)"), "   ");

    EXPECT_EQ(Tokens(doc, "str:tokenize('a,b,,c',',')"), "a|b||c");
    EXPECT_EQ(Tokens(doc, "str:split('abc','')"), "a|b|c");
    EXPECT_EQ(Tokens(doc, "str:split('a::b','::')"), "a|b");

    /* Core XPath unaffected by the pack. */
    EXPECT_EQ(EvalStr(doc, "concat('a','b')"), "ab");
    EXPECT_EQ(EvalNum(doc, "count(//item)"), 3.0);

    leptris_document_free(doc);
}

TEST(Exslt, TokenizeKindsAreText) {
    LeptrisDocument doc = MakeDoc();
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "str:tokenize('a,b',',')");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    ASSERT_EQ(leptris_xpath_result_count(r), 2u);
    EXPECT_EQ(leptris_xpath_result_node_kind(r, 0), LEPTRIS_XPATH_NODE_TEXT);
    EXPECT_EQ(leptris_xpath_result_node_kind(r, 1), LEPTRIS_XPATH_NODE_TEXT);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(Exslt, SetFunctions) {
    LeptrisDocument doc = MakeDoc();
    ASSERT_NE(doc, nullptr);

    EXPECT_EQ(EvalNum(doc, "count(set:distinct(//item))"), 3.0);
    /* n=5 item is in both branches. */
    EXPECT_EQ(EvalNum(doc,
        "count(set:intersection(//item[@n>3], //item[@n<6]))"), 1.0);
    /* Everything except the n=3 item. */
    EXPECT_EQ(EvalNum(doc,
        "count(set:difference(//item, //item[@n=3]))"), 2.0);
    /* Document order alpha,beta,gamma; boundary gamma(n=5):
     * leading = alpha,beta; trailing = empty. */
    EXPECT_EQ(EvalNum(doc,
        "count(set:leading(//item, //item[@n=5]))"), 2.0);
    EXPECT_EQ(EvalNum(doc,
        "count(set:trailing(//item, //item[@n=5]))"), 0.0);
    /* Boundary alpha(n=3): leading empty, trailing beta,gamma
     * (boundary itself excluded from both). */
    EXPECT_EQ(EvalNum(doc,
        "count(set:leading(//item, //item[@n=3]))"), 0.0);
    EXPECT_EQ(EvalNum(doc,
        "count(set:trailing(//item, //item[@n=3]))"), 2.0);

    leptris_document_free(doc);
}

TEST(Exslt, MathFunctions) {
    LeptrisDocument doc = MakeDoc();
    ASSERT_NE(doc, nullptr);

    EXPECT_EQ(EvalNum(doc, "math:max(//item/@n)"), 7.0);
    EXPECT_EQ(EvalNum(doc, "math:min(//item/@n)"), 3.0);
    EXPECT_EQ(EvalNum(doc, "math:abs(-4)"), 4.0);
    EXPECT_EQ(EvalNum(doc, "math:abs(4)"), 4.0);
    EXPECT_EQ(EvalNum(doc, "math:sqrt(16)"), 4.0);
    EXPECT_EQ(EvalNum(doc, "math:power(2,10)"), 1024.0);

    leptris_document_free(doc);
}

TEST(Exslt, EnableRejectsNull) {
    EXPECT_EQ(leptris_exslt_enable(nullptr), LEPTRIS_ERROR_NULL_ARG);
}
