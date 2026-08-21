// test/xpath/test_xpath.cpp — XPath engine specs.

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstring>

namespace {

constexpr char kBasic[] =
    "<catalog>"
    "  <book id='b1'><title>First</title><price>10</price></book>"
    "  <book id='b2'><title>Second</title><price>20</price></book>"
    "  <book id='b3'><title>Third</title><price>30</price></book>"
    "</catalog>";

LeptrisDocument ParseWith(const char* xml) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    EXPECT_NE(doc, nullptr);
    return doc;
}

TEST(XPathAxes, DescendantOrSelfFindsAll) {
    LeptrisDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//book");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 3u);

    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathFunctions, CountReturnsNodeSetSize) {
    LeptrisDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "count(//book)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_NUMBER);
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(r), 3.0);

    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathFunctions, StringFunctionExpandsEntities) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r>hello &amp; world</r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "string(/r)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_STRING);

    char* s = leptris_xpath_result_string(r);
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "hello & world");
    leptris_free_string(s);

    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathPredicates, LastSelectsFinalChild) {
    LeptrisDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r =
        leptris_xpath_eval(doc, nullptr, "//book[last()]/title");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 1u);

    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathPredicates, PositionalPredicateSelectsCorrectChild) {
    LeptrisDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r =
        leptris_xpath_eval(doc, nullptr, "//book[2]/title");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 1u);

    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathPredicates, AttributeValuePredicate) {
    LeptrisDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r =
        leptris_xpath_eval(doc, nullptr, "//book[@id='b2']/title");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 1u);

    leptris_xpath_result_free(r);
    leptris_document_free(doc);
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

    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char* queries[] = {
        "//a", "//a[@x='1']", "count(//a)", "string(//a/b)",
        "//a[1]/b | //a[2]/b", "name(//a[1])", "namespace-uri(//ns:c)",
        "//a[parent::r]", "//a[descendant::b]", "//a[position() > 1]",
        "//*[contains(string(.), 'text')]",
    };

    for (const char* q : queries) {
        LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, q);
        if (r) leptris_xpath_result_free(r);
    }

    leptris_document_free(doc);
    /* Under leaks --atExit --: 0 bytes leaked. */
}

}  // namespace


// ---- XPath variables (TODO 86 / 94) --------------------------------------

TEST(XPathVariables, BooleanVariableEvaluates) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><a/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathVariableSet vars = leptris_xpath_variable_set_new();
    ASSERT_NE(vars, nullptr);
    EXPECT_EQ(leptris_xpath_variable_set_boolean(vars, "flag", 1), LEPTRIS_OK);

    LeptrisXPathResult r = leptris_xpath_eval_with_vars(doc, "$flag", vars);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_BOOLEAN);
    EXPECT_EQ(leptris_xpath_result_boolean(r), 1);

    leptris_xpath_result_free(r);
    leptris_xpath_variable_set_free(vars);
    leptris_document_free(doc);
}

TEST(XPathVariables, NumberVariableEvaluates) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><a/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathVariableSet vars = leptris_xpath_variable_set_new();
    ASSERT_NE(vars, nullptr);
    EXPECT_EQ(leptris_xpath_variable_set_number(vars, "n", 42.5), LEPTRIS_OK);

    LeptrisXPathResult r = leptris_xpath_eval_with_vars(doc, "$n + 1", vars);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_NUMBER);
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(r), 43.5);

    leptris_xpath_result_free(r);
    leptris_xpath_variable_set_free(vars);
    leptris_document_free(doc);
}

TEST(XPathVariables, StringVariableEvaluates) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><a>hello</a></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathVariableSet vars = leptris_xpath_variable_set_new();
    ASSERT_NE(vars, nullptr);
    EXPECT_EQ(leptris_xpath_variable_set_string(vars, "greeting", "hello"), LEPTRIS_OK);

    LeptrisXPathResult r = leptris_xpath_eval_with_vars(doc, "$greeting", vars);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_STRING);
    char* s = leptris_xpath_result_string(r);
    EXPECT_STREQ(s, "hello");
    leptris_free_string(s);

    leptris_xpath_result_free(r);
    leptris_xpath_variable_set_free(vars);
    leptris_document_free(doc);
}

TEST(XPathVariables, UnknownVariableReturnsEmpty) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathVariableSet vars = leptris_xpath_variable_set_new();
    ASSERT_NE(vars, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval_with_vars(doc, "$undefined", vars);
    EXPECT_EQ(r, nullptr);

    leptris_xpath_variable_set_free(vars);
    leptris_document_free(doc);
}

// ---- Comprehensive XPath coverage (TODO 69-style) ------------------------

TEST(XPathAxes, ChildAxisFindsDirectChildren) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><a/><b/><c/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "/r/*");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 3u);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathAxes, AttributeAxisReturnsAttributes) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r a='1' b='2'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "/r/@*");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 2u);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathAxes, DescendantAxisFindsAllLevels) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><a><b/><c/></a><d/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "count(//*)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_NUMBER);
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(r), 5.0);  // r, a, b, c, d
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathFunctions, NameReturnsElementName) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root><child/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "name(/root/child)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_STRING);
    char* s = leptris_xpath_result_string(r);
    EXPECT_STREQ(s, "child");
    leptris_free_string(s);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathFunctions, StringLengthReturnsCharCount) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r>hello</r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "string-length(/r)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_NUMBER);
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(r), 5.0);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathFunctions, TrueAndFalseFunctionsWork) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r1 = leptris_xpath_eval(doc, nullptr, "true()");
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(leptris_xpath_result_boolean(r1), 1);
    leptris_xpath_result_free(r1);
    LeptrisXPathResult r2 = leptris_xpath_eval(doc, nullptr, "false()");
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(leptris_xpath_result_boolean(r2), 0);
    leptris_xpath_result_free(r2);
    leptris_document_free(doc);
}

TEST(XPathPredicates, PositionGreaterThanFiltersCorrectly) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><a/><a/><a/><a/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "/r/a[position() > 2]");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 2u);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathPredicates, UnionOperatorCombinesNodeSets) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><a/><b/><c/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "/r/a | /r/c");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 2u);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathOperators, ArithmeticAddition) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "2 + 3");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_NUMBER);
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(r), 5.0);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathOperators, StringComparison) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><a x='1'/><a x='2'/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "/r/a[@x = '2']");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 1u);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathText, TextNodeSelection) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><a>text1</a><a>text2</a></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "string(/r/a[2])");
    ASSERT_NE(r, nullptr);
    char* s = leptris_xpath_result_string(r);
    EXPECT_STREQ(s, "text2");
    leptris_free_string(s);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathNested, NestedPredicates) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<r>"
        "  <a id='1'><v>10</v></a>"
        "  <a id='2'><v>20</v></a>"
        "  <a id='3'><v>30</v></a>"
        "</r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "/r/a[v > 15]/@id");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 2u);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

/* ---- TODO 192: subtree-interval index for relative descendants ---- */

namespace {

const char kSubtree[] =
    "<library>"
    "<section id='a'><item>1</item><item>2</item>"
    "<section><item>3</item></section></section>"
    "<section id='b'><item>4</item></section>"
    "</library>";

/* The element index builds on the second axis query (TODO 190), so
 * each spec runs a warm-up first — otherwise only the walk path
 * would be tested. */
LeptrisXPathResult EvalWarm(LeptrisDocument doc, LeptrisElement ctx,
                           const char* expr) {
    LeptrisXPathResult warm =
        leptris_xpath_eval(doc, nullptr, "//warmup-absent");
    if (warm) leptris_xpath_result_free(warm);
    return leptris_xpath_eval(doc, ctx, expr);
}

LeptrisElement FirstSection(LeptrisDocument doc) {
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//section");
    if (!r || leptris_xpath_result_count(r) == 0) {
        if (r) leptris_xpath_result_free(r);
        return nullptr;
    }
    LeptrisElement e = leptris_xpath_result_get(r, 0);
    leptris_xpath_result_free(r);
    return e;
}

}  // namespace

TEST(XPathSubtreeIndex, ChainedDoubleSlashNoDoubleCount) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(kSubtree, std::strlen(kSubtree), &st);
    ASSERT_NE(doc, nullptr);

    /* //section matches the outer AND the nested section; the
     * subtrees overlap. Interval containment must skip the nested
     * one — 4 items, not 5. Second+ query exercises the index. */
    LeptrisXPathResult first = leptris_xpath_eval(doc, nullptr, "//item");
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(first), 4u);
    leptris_xpath_result_free(first);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//section//item");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 4u);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathSubtreeIndex, RelativeDescendantFromContext) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(kSubtree, std::strlen(kSubtree), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement sec = FirstSection(doc);
    ASSERT_NE(sec, nullptr);

    /* .//item from section[a]: items 1, 2 and the nested 3. */
    LeptrisXPathResult r = EvalWarm(doc, sec, ".//item");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 3u);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathSubtreeIndex, StrictDescendantExcludesSelf) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<item><item>x</item></item>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult root = leptris_xpath_eval(doc, nullptr, "//item");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_xpath_result_count(root), 2u);
    LeptrisElement e = leptris_xpath_result_get(root, 0);
    leptris_xpath_result_free(root);

    /* descendant::item from the outer item: the inner one only. */
    LeptrisXPathResult r = EvalWarm(doc, e, "descendant::item");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 1u);
    leptris_xpath_result_free(r);

    /* descendant-or-self::item includes the context itself. */
    LeptrisXPathResult r2 = EvalWarm(doc, e, "descendant-or-self::item");
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r2), 2u);
    leptris_xpath_result_free(r2);
    leptris_document_free(doc);
}

TEST(XPathSubtreeIndex, MutationInvalidatesIndex) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(kSubtree, std::strlen(kSubtree), &st);
    ASSERT_NE(doc, nullptr);

    /* Two queries so the index is built and cached. */
    LeptrisXPathResult warm = leptris_xpath_eval(doc, nullptr, "//item");
    ASSERT_NE(warm, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(warm), 4u);
    leptris_xpath_result_free(warm);
    LeptrisXPathResult warm2 = leptris_xpath_eval(doc, nullptr, "//warmup-absent");
    if (warm2) leptris_xpath_result_free(warm2);

    /* Mutate: remove section[b]'s only item, then query again — the
     * rebuilt index must not serve stale results. */
    LeptrisXPathResult secs = leptris_xpath_eval(doc, nullptr, "//section[@id='b']");
    ASSERT_NE(secs, nullptr);
    ASSERT_EQ(leptris_xpath_result_count(secs), 1u);
    LeptrisElement secb = leptris_xpath_result_get(secs, 0);
    leptris_xpath_result_free(secs);
    ASSERT_NE(secb, nullptr);

    LeptrisXPathResult victim = leptris_xpath_eval(doc, secb, ".//item");
    ASSERT_NE(victim, nullptr);
    ASSERT_EQ(leptris_xpath_result_count(victim), 1u);
    LeptrisElement item = leptris_xpath_result_get(victim, 0);
    leptris_xpath_result_free(victim);
    ASSERT_NE(item, nullptr);

    EXPECT_EQ(leptris_element_remove_child(secb, item), LEPTRIS_OK);

    LeptrisXPathResult after = leptris_xpath_eval(doc, nullptr, "//item");
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(after), 3u);
    leptris_xpath_result_free(after);
    leptris_document_free(doc);
}

TEST(XPathSubtreeIndex, EmptyResultForAbsentName) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(kSubtree, std::strlen(kSubtree), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement sec = FirstSection(doc);
    ASSERT_NE(sec, nullptr);

    LeptrisXPathResult r = EvalWarm(doc, sec, ".//does-not-exist");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 0u);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

/* ---- TODO 192b: predicated relative descendants via the index ---- */

namespace {
/* Items carry n attributes for the predicated relative specs. */
const char kSubtreeAttr[] =
    "<library>"
    "<section id='a'><item n='1'>x</item><item n='2'>y</item>"
    "<section><item n='11'>z</item></section></section>"
    "<section id='b'><item n='1'>w</item></section>"
    "</library>";
}

TEST(XPathSubtreeIndex, PredicatedRelativeFromContext) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(kSubtreeAttr, std::strlen(kSubtreeAttr), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement sec = FirstSection(doc);
    ASSERT_NE(sec, nullptr);

    LeptrisXPathResult r = EvalWarm(doc, sec, ".//item[@n='2']");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 1u);
    leptris_xpath_result_free(r);

    LeptrisXPathResult r2 = EvalWarm(doc, sec, ".//item[@n='11']");
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r2), 1u);  /* nested section */
    leptris_xpath_result_free(r2);
    leptris_document_free(doc);
}

TEST(XPathSubtreeIndex, PredicatedChainedAcrossSections) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(kSubtreeAttr, std::strlen(kSubtreeAttr), &st);
    ASSERT_NE(doc, nullptr);

    /* Both sections own an item with n='1'; the nested item is
     * n='11' and must not double-count. */
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//section//item[@n='1']");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 2u);
    leptris_xpath_result_free(r);

    LeptrisXPathResult r2 = leptris_xpath_eval(doc, nullptr, "//section//item[@n='11']");
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r2), 1u);
    leptris_xpath_result_free(r2);
    leptris_document_free(doc);
}

TEST(XPathSubtreeIndex, PositionPredicateKeepsExpandedSemantics) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(kSubtreeAttr, std::strlen(kSubtreeAttr), &st);
    ASSERT_NE(doc, nullptr);

    /* `//section//item[1]` means first item PER PARENT (section a,
     * its nested section, and section b) — 3, not 2-per-section.
     * Position predicates must NOT take the fused path. */
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//section//item[1]");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 3u);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

/* ---- TODO 192d: absolute //name[@attr='value'] via value buckets ---- */

TEST(XPathSubtreeIndex, AbsolutePredicatedValueBucket) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(kSubtreeAttr, std::strlen(kSubtreeAttr), &st);
    ASSERT_NE(doc, nullptr);

    /* Cold query (no index yet) and warm query must agree. */
    LeptrisXPathResult cold = leptris_xpath_eval(doc, nullptr, "//item[@n='1']");
    ASSERT_NE(cold, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(cold), 2u);
    leptris_xpath_result_free(cold);

    LeptrisXPathResult warm = leptris_xpath_eval(doc, nullptr, "//item[@n='11']");
    ASSERT_NE(warm, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(warm), 1u);
    leptris_xpath_result_free(warm);

    /* Chained rest steps still follow the fused opcode. */
    LeptrisXPathResult chained = leptris_xpath_eval(doc, nullptr, "//item[@n='11']/text()");
    ASSERT_NE(chained, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(chained), 1u);
    leptris_xpath_result_free(chained);
    leptris_document_free(doc);
}

TEST(XPathSubtreeIndex, AbsolutePredicatedRootMatches) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<item n='5'><item n='5'/><other/></item>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* `//` is descendant-or-SELF: the root itself carries n='5' and
     * must be included, cold and warm. */
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "count(//item[@n='5'])");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(r), 2.0);
    leptris_xpath_result_free(r);

    LeptrisXPathResult r2 = leptris_xpath_eval(doc, nullptr, "count(//item[@n='5'])");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(r2), 2.0);
    leptris_xpath_result_free(r2);
    leptris_document_free(doc);
}

/* ---- TODO 192e: attr-EXISTS via the any-value bucket ---- */

TEST(XPathSubtreeIndex, AttrExistsAbsolute) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(kSubtreeAttr, std::strlen(kSubtreeAttr), &st);
    ASSERT_NE(doc, nullptr);

    /* Cold and warm must agree: every item carries n. */
    LeptrisXPathResult cold = leptris_xpath_eval(doc, nullptr, "//item[@n]");
    ASSERT_NE(cold, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(cold), 4u);
    leptris_xpath_result_free(cold);

    LeptrisXPathResult warm = leptris_xpath_eval(doc, nullptr, "count(//item[@n])");
    ASSERT_NE(warm, nullptr);
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(warm), 4.0);
    leptris_xpath_result_free(warm);

    /* Attribute carried by the outer sections only (the nested
     * section has no id). */
    LeptrisXPathResult secs = leptris_xpath_eval(doc, nullptr, "count(//section[@id])");
    ASSERT_NE(secs, nullptr);
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(secs), 2.0);
    leptris_xpath_result_free(secs);

    /* Absent attribute: empty, cold and warm. */
    LeptrisXPathResult none = leptris_xpath_eval(doc, nullptr, "count(//item[@zzz])");
    ASSERT_NE(none, nullptr);
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(none), 0.0);
    leptris_xpath_result_free(none);
    leptris_document_free(doc);
}

TEST(XPathSubtreeIndex, AttrExistsRelativeFromContext) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(kSubtreeAttr, std::strlen(kSubtreeAttr), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement sec = FirstSection(doc);
    ASSERT_NE(sec, nullptr);

    LeptrisXPathResult r = EvalWarm(doc, sec, ".//item[@n]");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 3u);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}
