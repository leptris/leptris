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

// Regression (CodeQL critical, alert #32): freeing the same result
// twice used to treat the thread-local free-list next-pointer as a
// live nodeset — heap corruption reachable from public-API misuse.
// The free-list now parks entries behind an internal CACHED sentinel
// and a second free is a no-op.
TEST(XPathResults, DoubleFreeIsANoOp) {
    const char xml[] = "<r><a x='1'/><a x='2'/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    for (int i = 0; i < 3; i++) {
        LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//a[@x='1']");
        ASSERT_NE(r, (LeptrisXPathResult)0);
        leptris_xpath_result_free(r);
        leptris_xpath_result_free(r);  /* must be a silent no-op */
    }

    /* The engine (and its free-list) must still work afterwards. */
    LeptrisXPathResult r2 = leptris_xpath_eval(doc, nullptr, "//a");
    ASSERT_NE(r2, (LeptrisXPathResult)0);
    leptris_xpath_result_free(r2);

    leptris_document_free(doc);
}

// Mixed nodesets (architecture review, candidate A): nodesets contain
// element nodes AND synthetic attribute nodes. The kind/get_node/
// node_name/node_value quartet is the public way to consume them;
// leptris_xpath_result_get stays elements-only.
TEST(XPathResults, MixedNodesetAttributeKind) {
    const char xml[] = "<r><a x='1'/><a x='2'/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//a/@x");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    ASSERT_EQ(leptris_xpath_result_count(r), 2u);

    EXPECT_EQ(leptris_xpath_result_node_kind(r, 0), LEPTRIS_XPATH_NODE_ATTRIBUTE);
    EXPECT_EQ(leptris_xpath_result_node_kind(r, 1), LEPTRIS_XPATH_NODE_ATTRIBUTE);
    EXPECT_STREQ(leptris_xpath_result_node_name(r, 0), "x");
    EXPECT_STREQ(leptris_xpath_result_node_value(r, 0), "1");
    EXPECT_STREQ(leptris_xpath_result_node_value(r, 1), "2");

    /* Elements-only accessor must not miscast an attribute node. */
    EXPECT_EQ(leptris_xpath_result_get(r, 0), nullptr);
    /* The node handle itself is retrievable whatever the kind. */
    ASSERT_NE(leptris_xpath_result_get_node(r, 0), nullptr);

    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathResults, MixedNodesetElementKind) {
    const char xml[] = "<r><a x='1'/><a x='2'/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//a");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    ASSERT_EQ(leptris_xpath_result_count(r), 2u);

    for (size_t i = 0; i < 2; i++) {
        EXPECT_EQ(leptris_xpath_result_node_kind(r, i), LEPTRIS_XPATH_NODE_ELEMENT);
        LeptrisNodeRef n = leptris_xpath_result_get_node(r, i);
        ASSERT_NE(n, nullptr);
        LeptrisElement e = leptris_node_as_element(n);
        ASSERT_NE(e, nullptr);
        EXPECT_STREQ(leptris_element_name(e), "a");
        /* Element nodes report their name; no string-value via the
         * node accessors (issue #477 mixed-nodeset contract). */
        EXPECT_STREQ(leptris_xpath_result_node_name(r, i), "a");
        EXPECT_EQ(leptris_xpath_result_node_value(r, i), nullptr);
        /* And the elements-only accessor agrees with get_node. */
        EXPECT_EQ(leptris_xpath_result_get(r, i), e);
    }

    /* Out of range / wrong type -> safe defaults. */
    EXPECT_EQ(leptris_xpath_result_node_kind(r, 99), LEPTRIS_XPATH_NODE_OTHER);
    EXPECT_EQ(leptris_xpath_result_get_node(r, 99), nullptr);
    EXPECT_EQ(leptris_xpath_result_node_name(r, 99), nullptr);
    EXPECT_EQ(leptris_xpath_result_node_value(r, 99), nullptr);

    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathResults, GetNodesBatchCopiesElementsOnly) {
    const char xml[] = "<r><a x='1'/><a x='2'/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* Mixed result: 2 attribute nodes, 0 elements. */
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//a/@x");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    LeptrisElement out[4] = {0};
    EXPECT_EQ(leptris_xpath_result_get_nodes(r, out, 4), 0u);
    leptris_xpath_result_free(r);

    /* Element result: both copied. */
    LeptrisXPathResult r2 = leptris_xpath_eval(doc, nullptr, "//a");
    ASSERT_NE(r2, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_get_nodes(r2, out, 4), 2u);
    EXPECT_STREQ(leptris_element_name(out[0]), "a");
    leptris_xpath_result_free(r2);

    leptris_document_free(doc);
}

// ---- issue #477: mixed-nodeset tag-space collision + accessors --

TEST(XPathResults, MixedNodesetKindsNamesValues) {
    /* Element, text, comment, CDATA all in one //node() result.
     * Regression: synthetic attribute nodes used tag 1, colliding
     * with real text nodes (public tag 1) — node_name/miscast crashed
     * on text entries. */
    const char xml[] = "<r><a id='1'>hello</a><!-- c --><b><![CDATA[cd]]></b></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//node()");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    size_t n = leptris_xpath_result_count(r);
    ASSERT_EQ(n, 6u);  /* r + a, hello, comment, b, cd (root included) */

    /* Every entry must be classified by its real kind (no crash,
     * no miscast), and name/value must match the node's content. */
    int elements = 0, texts = 0, others = 0;
    for (size_t i = 0; i < n; i++) {
        LeptrisXPathNodeKind k = leptris_xpath_result_node_kind(r, i);
        const char* name = leptris_xpath_result_node_name(r, i);
        const char* value = leptris_xpath_result_node_value(r, i);
        switch (k) {
            case LEPTRIS_XPATH_NODE_ELEMENT:
                elements++;
                /* r, a and b are the three elements in the result. */
                EXPECT_TRUE(name != nullptr &&
                            (std::strcmp(name, "a") == 0 ||
                             std::strcmp(name, "b") == 0 ||
                             std::strcmp(name, "r") == 0));
                break;
            case LEPTRIS_XPATH_NODE_TEXT:
                texts++;
                EXPECT_EQ(name, nullptr);
                ASSERT_NE(value, nullptr);
                EXPECT_TRUE(std::strcmp(value, "hello") == 0 ||
                            std::strcmp(value, "cd") == 0);
                break;
            default:
                others++;
                EXPECT_EQ(name, nullptr);
                ASSERT_NE(value, nullptr);
                EXPECT_STREQ(value, " c ");
                break;
        }
    }
    EXPECT_EQ(elements, 3);
    EXPECT_EQ(texts, 2);
    EXPECT_EQ(others, 1);

    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathResults, MixedNodesetAttributeNameValue) {
    const char xml[] = "<r><a id='x1'>t</a></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//a/@id");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    ASSERT_EQ(leptris_xpath_result_count(r), 1u);
    EXPECT_EQ(leptris_xpath_result_node_kind(r, 0), LEPTRIS_XPATH_NODE_ATTRIBUTE);
    EXPECT_STREQ(leptris_xpath_result_node_name(r, 0), "id");
    EXPECT_STREQ(leptris_xpath_result_node_value(r, 0), "x1");

    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathResults, NameFunctionsOnAttributeNodeset) {
    /* Regression: name()/local-name()/namespace-uri() read their arg
     * nodeset's first node AFTER freeing the result that owned the
     * synthetic attribute node — dangling read. */
    const char xml[] = "<r><a id='x1'>t</a></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "name(//a/@id)");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    char* s = leptris_xpath_result_string(r);
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "id");
    leptris_free_string(s);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval(doc, nullptr, "local-name(//a/@id)");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    s = leptris_xpath_result_string(r);
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "id");
    leptris_free_string(s);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval(doc, nullptr, "namespace-uri(//a/@id)");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    s = leptris_xpath_result_string(r);
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "");
    leptris_free_string(s);
    leptris_xpath_result_free(r);

    leptris_document_free(doc);
}

TEST(XPathResults, TextNodeTestAsExpression) {
    /* Regression: bare text()/node() in expression position was
     * parsed as an unknown FUNCTION call, so a[text()] and
     * a[text()='...'] predicates matched nothing. */
    const char xml[] = "<r><a>hello</a><b/><c>zz</c></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//a[text()]");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_count(r), 1u);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval(doc, nullptr, "//b[text()]");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_count(r), 0u);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval(doc, nullptr, "//c[text()='zz']");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_count(r), 1u);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval(doc, nullptr, "//c[text()='nope']");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_count(r), 0u);
    leptris_xpath_result_free(r);

    /* text() no longer double-counts elements with text content. */
    r = leptris_xpath_eval(doc, nullptr, "//text()");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    ASSERT_EQ(leptris_xpath_result_count(r), 2u);
    EXPECT_EQ(leptris_xpath_result_node_kind(r, 0), LEPTRIS_XPATH_NODE_TEXT);
    EXPECT_EQ(leptris_xpath_result_node_kind(r, 1), LEPTRIS_XPATH_NODE_TEXT);
    leptris_xpath_result_free(r);

    leptris_document_free(doc);
}

TEST(XPathResults, StringValueOfNonElementNodes) {
    const char xml[] = "<r><a id='x9'>hello</a><!-- c --><b><![CDATA[7]]></b></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    struct { const char* expr; const char* want; } cases[] = {
        {"string(//text())", "hello"},
        {"string(//comment())", " c "},
        {"string(//b)", "7"},
        {"string(//a/@id)", "x9"},
    };
    for (auto& c : cases) {
        LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, c.expr);
        ASSERT_NE(r, (LeptrisXPathResult)0) << c.expr;
        char* s = leptris_xpath_result_string(r);
        ASSERT_NE(s, nullptr) << c.expr;
        EXPECT_STREQ(s, c.want) << c.expr;
        leptris_free_string(s);
        leptris_xpath_result_free(r);
    }

    leptris_document_free(doc);
}

// ---- issue #485: document order in merged nodesets --

namespace {

/* Collect element names / kinds for a nodeset result, in result
 * order, as a comparable string ("a txt cmt b" style). */
std::string NodesetSequence(LeptrisXPathResult r) {
    std::string out;
    size_t n = leptris_xpath_result_count(r);
    for (size_t i = 0; i < n; i++) {
        if (!out.empty()) out += ' ';
        LeptrisXPathNodeKind k = leptris_xpath_result_node_kind(r, i);
        switch (k) {
            case LEPTRIS_XPATH_NODE_ELEMENT: {
                const char* nm = leptris_xpath_result_node_name(r, i);
                out += nm ? nm : "?";
                break;
            }
            case LEPTRIS_XPATH_NODE_TEXT: {
                const char* v = leptris_xpath_result_node_value(r, i);
                out += "t:";
                out += v ? v : "";
                break;
            }
            default:
                out += "other";
                break;
        }
    }
    return out;
}

}  // namespace

TEST(XPathResults, DocumentOrderMergedNodeset) {
    /* Children of an early element interleave with the element's
     * own later siblings: text inside <a> precedes the comment that
     * follows <a>. Regression: per-context append produced
     * [a, cmt, text, b, bb]. */
    const char xml[] =
        "<r><a>text</a><!-- c --><b>bb</b><c><d>dd</d></c></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//node()");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(NodesetSequence(r),
              "r a t:text other b t:bb c d t:dd");
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval(doc, nullptr, "//text()");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(NodesetSequence(r), "t:text t:bb t:dd");
    leptris_xpath_result_free(r);

    /* Union of overlapping sets stays in document order, deduped. */
    r = leptris_xpath_eval(doc, nullptr, "//b | //node()");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(NodesetSequence(r),
              "r a t:text other b t:bb c d t:dd");
    leptris_xpath_result_free(r);

    /* Deeply nested text orders across subtrees. */
    const char xml2[] = "<r><x><y>1</y></x><z>2</z></r>";
    LeptrisDocument doc2 = leptris_parse_string(xml2, std::strlen(xml2), &st);
    ASSERT_NE(doc2, nullptr);
    r = leptris_xpath_eval(doc2, nullptr, "//node()");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(NodesetSequence(r), "r x y t:1 z t:2");
    leptris_xpath_result_free(r);
    leptris_document_free(doc2);

    leptris_document_free(doc);
}

TEST(XPathResults, DocumentOrderReverseAxis) {
    /* Reverse axes report reverse document order: the ancestors of
     * later matches come first. */
    const char xml[] = "<r><a><x>1</x></a><b><x>2</x></b></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//x/ancestor::*");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(NodesetSequence(r), "b a r");
    leptris_xpath_result_free(r);

    leptris_document_free(doc);
}

TEST(XPathResults, AbsoluteTypeTestWalkOrder) {
    /* //text(), //node(), //comment(), //processing-instruction()
     * fold to a single pre-order walk (issue #485) — verify counts
     * and order through the folded path. */
    const char xml[] =
        "<r><?one data?><a>t1</a><?two data?><!--c--><b><![CDATA[cd]]></b></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//node()");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(NodesetSequence(r), "r other a t:t1 other other b t:cd");
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval(doc, nullptr, "//processing-instruction()");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_count(r), 2u);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval(doc, nullptr, "//processing-instruction('two')");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_count(r), 1u);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval(doc, nullptr, "//processing-instruction('nope')");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_count(r), 0u);
    leptris_xpath_result_free(r);

    leptris_document_free(doc);
}

// ---- perf round 19: hashed index buckets + sort-based union dedup --

TEST(XPathResults, IndexWithManyDistinctValuesAndNames) {
    /* 300 distinct id values + 300 distinct element names force the
     * element-index build through its hashed bucket lookups. The
     * linear scans this replaced were O(distinct^2) — 300ms on a
     * 20k-unique-id document. */
    std::string xml = "<root>";
    for (int i = 0; i < 300; i++) {
        xml += "<e" + std::to_string(i) + " id='k" + std::to_string(i) + "'>v</e";
        xml += std::to_string(i) + ">";
    }
    xml += "</root>";

    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml.c_str(), xml.size(), &st);
    ASSERT_NE(doc, nullptr);

    /* Two //name queries: the second triggers the index build
     * (TODO 190). All lookups must hit the hashed buckets. */
    for (int round = 0; round < 2; round++) {
        LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//e7");
        ASSERT_NE(r, (LeptrisXPathResult)0);
        EXPECT_EQ(leptris_xpath_result_count(r), 1u);
        leptris_xpath_result_free(r);

        r = leptris_xpath_eval(doc, nullptr, "//e299");
        ASSERT_NE(r, (LeptrisXPathResult)0);
        EXPECT_EQ(leptris_xpath_result_count(r), 1u);
        leptris_xpath_result_free(r);

        r = leptris_xpath_eval(doc, nullptr, "//*[@id='k0']");
        ASSERT_NE(r, (LeptrisXPathResult)0);
        EXPECT_EQ(leptris_xpath_result_count(r), 1u);
        leptris_xpath_result_free(r);

        r = leptris_xpath_eval(doc, nullptr, "//*[@id='k150']");
        ASSERT_NE(r, (LeptrisXPathResult)0);
        EXPECT_EQ(leptris_xpath_result_count(r), 1u);
        leptris_xpath_result_free(r);

        /* Duplicate values map to every matching element. */
        r = leptris_xpath_eval(doc, nullptr, "//*[.='v']");
        ASSERT_NE(r, (LeptrisXPathResult)0);
        EXPECT_EQ(leptris_xpath_result_count(r), 300u);
        leptris_xpath_result_free(r);
    }

    /* Union dedup: overlapping sets, no duplicates in the result. */
    LeptrisXPathResult r = leptris_xpath_eval(
        doc, nullptr, "//*[@id='k5'] | //e5 | //*[@id='k5']");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_count(r), 1u);
    leptris_xpath_result_free(r);

    leptris_document_free(doc);
}

// ---- external namespace bindings (v1.2.0: XPointer xmlns) --------

TEST(XPathNamespaces, BoundPrefixMatchesByNamespaceUri) {
    /* t:title and the default-ns title are both in http://x; the
     * plain title is in no namespace. */
    const char xml[] =
        "<r xmlns:t='http://x'>"
        "<t:title>A</t:title>"
        "<other xmlns='http://x'><title>B</title></other>"
        "<title>plain</title>"
        "</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathNsSet ns = leptris_xpath_ns_set_new();
    ASSERT_NE(ns, nullptr);
    EXPECT_EQ(leptris_xpath_ns_set_add(ns, "p", "http://x"), LEPTRIS_OK);

    /* p:title matches by URI: both namespace-carried titles. */
    LeptrisXPathResult r = leptris_xpath_eval_ns(doc, nullptr, "//p:title", ns);
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_count(r), 2u);
    leptris_xpath_result_free(r);

    /* p:* is namespace-scoped: the three elements in http://x. */
    r = leptris_xpath_eval_ns(doc, nullptr, "//p:*", ns);
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_count(r), 3u);
    leptris_xpath_result_free(r);

    /* Re-binding replaces the URI: now nothing matches. */
    EXPECT_EQ(leptris_xpath_ns_set_add(ns, "p", "http://empty"), LEPTRIS_OK);
    r = leptris_xpath_eval_ns(doc, nullptr, "//p:title", ns);
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_count(r), 0u);
    leptris_xpath_result_free(r);

    leptris_xpath_ns_set_free(ns);
    leptris_document_free(doc);
}

TEST(XPathNamespaces, UnboundPrefixesKeepLiteralSemantics) {
    const char xml[] =
        "<r xmlns:t='http://x'><t:title>A</t:title><b/><t:c/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* No bindings: literal prefix comparison. */
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//t:title");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_count(r), 1u);
    leptris_xpath_result_free(r);

    /* Regression: //t:* previously matched EVERY element (the
     * wildcard fusion and the matcher both ignored the prefix);
     * it must be namespace-scoped to t:. */
    r = leptris_xpath_eval(doc, nullptr, "//t:*");
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_count(r), 2u);
    leptris_xpath_result_free(r);

    /* NULL bindings on the ns entry point = plain evaluation. */
    r = leptris_xpath_eval_ns(doc, nullptr, "//t:c", nullptr);
    ASSERT_NE(r, (LeptrisXPathResult)0);
    EXPECT_EQ(leptris_xpath_result_count(r), 1u);
    leptris_xpath_result_free(r);

    leptris_document_free(doc);
}

TEST(XPathNamespaces, BadBindingArgumentsRejected) {
    LeptrisXPathNsSet ns = leptris_xpath_ns_set_new();
    ASSERT_NE(ns, nullptr);
    EXPECT_EQ(leptris_xpath_ns_set_add(ns, nullptr, "u"), LEPTRIS_ERROR_NULL_ARG);
    EXPECT_EQ(leptris_xpath_ns_set_add(ns, "p", nullptr), LEPTRIS_ERROR_NULL_ARG);
    EXPECT_EQ(leptris_xpath_ns_set_add(ns, "", "u"), LEPTRIS_ERROR_NULL_ARG);
    leptris_xpath_ns_set_free(ns);
    leptris_xpath_ns_set_free(nullptr);
}

// ---- TODO.remaining/07: compile-time folding of literal string fns --

TEST(XPathConstantFolding, ConcatOfLiterals) {
    LeptrisDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(
        doc, nullptr, "concat('leptris', '-', 'is', ' ', 'fast')");
    ASSERT_NE(r, nullptr);
    char* s = leptris_xpath_result_string(r);
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "leptris-is fast");
    leptris_free_string(s);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathConstantFolding, ConcatWithRuntimeArgStaysRuntime) {
    /* position() is not a literal: the fold must NOT fire; the
     * result still comes out right via the normal path. */
    LeptrisDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(
        doc, nullptr, "concat('n', position())");
    ASSERT_NE(r, nullptr);
    char* s = leptris_xpath_result_string(r);
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "n1");
    leptris_free_string(s);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPathConstantFolding, ContainsLiteralsFoldToBoolean) {
    LeptrisDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r1 = leptris_xpath_eval(
        doc, nullptr, "contains('leptris', 'ptri')");
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(leptris_xpath_result_boolean(r1), 1);
    leptris_xpath_result_free(r1);

    LeptrisXPathResult r2 = leptris_xpath_eval(
        doc, nullptr, "contains('leptris', 'x')");
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(leptris_xpath_result_boolean(r2), 0);
    leptris_xpath_result_free(r2);

    leptris_document_free(doc);
}

TEST(XPathConstantFolding, ContainsFoldWorksInBooleanContext) {
    /* Folded boolean feeding the `and` operator. */
    LeptrisDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(
        doc, nullptr, "contains('abc','b') and contains('abc','z')");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_boolean(r), 0);
    leptris_xpath_result_free(r);

    LeptrisXPathResult r2 = leptris_xpath_eval(
        doc, nullptr, "contains('abc','a') and contains('abc','c')");
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(leptris_xpath_result_boolean(r2), 1);
    leptris_xpath_result_free(r2);

    leptris_document_free(doc);
}

TEST(XPathConstantFolding, SubstringLiteralsFoldWithRounding) {
    LeptrisDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    struct { const char* expr; const char* want; } cases[] = {
        { "substring('12345', 2, 3)",           "234"   },
        { "substring('12345', 2)",              "2345"  },
        { "substring('12345', 1.5, 2.6)",       "234"   }, /* round: 2,3 */
        { "substring('12345', 0, 3)",           "12"    }, /* pos<1 clamps */
        { "substring('12345', -1, 3)",          "1"     },
        /* UTF-8: characters, not bytes. */
        { "substring('café-au-lait', 4, 2)",    "\xc3\xa9-" },
        { "substring('caf\xc3\xa9', 4)",       "\xc3\xa9" },
    };
    for (auto& c : cases) {
        LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, c.expr);
        ASSERT_NE(r, nullptr) << c.expr;
        char* s = leptris_xpath_result_string(r);
        ASSERT_NE(s, nullptr) << c.expr;
        EXPECT_STREQ(s, c.want) << c.expr;
        leptris_free_string(s);
        leptris_xpath_result_free(r);
    }
    leptris_document_free(doc);
}

/* Issue #525 (XPath 1.0 §2.3): an unprefixed name test matches only
 * NO-namespace elements — prefix-less elements under a default xmlns
 * are namespaced and must not match. */
TEST(NamespaceConformance, UnprefixedNameTestMatchesNoNamespaceOnly) {
    auto count = [](LeptrisDocument d, const char* expr) -> int {
        LeptrisXPathResult r = leptris_xpath_eval(d, nullptr, expr);
        int n = r ? (int)leptris_xpath_result_number(r) : -1;
        if (r) leptris_xpath_result_free(r);
        return n;
    };

    const char* prefixed =
        "<library xmlns:p='urn:p'><p:note>x</p:note></library>";
    LeptrisDocument d1 = leptris_parse_string(prefixed, strlen(prefixed), nullptr);
    ASSERT_NE(d1, nullptr);
    EXPECT_EQ(count(d1, "count(//p:note)"), 1);
    EXPECT_EQ(count(d1, "count(//note)"), 0);
    leptris_document_free(d1);

    const char* defaulted = "<r xmlns='urn:d'><n/></r>";
    LeptrisDocument d2 = leptris_parse_string(defaulted, strlen(defaulted), nullptr);
    ASSERT_NE(d2, nullptr);
    EXPECT_EQ(count(d2, "count(//n)"), 0);
    leptris_document_free(d2);

    const char* plain = "<r><n/></r>";
    LeptrisDocument d3 = leptris_parse_string(plain, strlen(plain), nullptr);
    ASSERT_NE(d3, nullptr);
    EXPECT_EQ(count(d3, "count(//n)"), 1);
    leptris_document_free(d3);

    /* Mixed document: each form selects exactly its own elements. */
    const char* mixed =
        "<r xmlns:p='urn:p'><a/><p:a/><a xmlns='urn:d'/></r>";
    LeptrisDocument d4 = leptris_parse_string(mixed, strlen(mixed), nullptr);
    ASSERT_NE(d4, nullptr);
    EXPECT_EQ(count(d4, "count(//a)"), 1);
    leptris_document_free(d4);
}

/* Issue #514: mixed (union) nodesets exposed attribute entries as
 * dangling pointers — kind=OTHER, NULL name/value, garbage tags. */
TEST(MixedNodeset, UnionAttributesKeepIdentity) {
    const char xml[] = "<r><a id='1'>x</a><a id='2'>y</a></r>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//a | //a/@id");
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(leptris_xpath_result_count(r), 4u);
    EXPECT_EQ(leptris_xpath_result_node_kind(r, 0), LEPTRIS_XPATH_NODE_ELEMENT);
    EXPECT_EQ(leptris_xpath_result_node_kind(r, 1), LEPTRIS_XPATH_NODE_ATTRIBUTE);
    EXPECT_STREQ(leptris_xpath_result_node_name(r, 1), "id");
    EXPECT_STREQ(leptris_xpath_result_node_value(r, 1), "1");
    EXPECT_EQ(leptris_xpath_result_node_kind(r, 3), LEPTRIS_XPATH_NODE_ATTRIBUTE);
    EXPECT_STREQ(leptris_xpath_result_node_value(r, 3), "2");
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

/* Issue #557: an ABSOLUTE path with a prefixed name test must offer
 * the namespaced root element — the document node seeds absolute
 * paths, and the root IS a child of the document. */
TEST(NsAbsolutePaths, PrefixedDoubleSlashMatchesNamespacedRoot) {
    const char xml[] = "<x:r xmlns:x='urn:x'><x:a/></x:r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathNsSet ns = leptris_xpath_ns_set_new();
    leptris_xpath_ns_set_add(ns, "x", "urn:x");

    LeptrisXPathResult r = leptris_xpath_eval_ns(doc, nullptr, "count(//x:r)", ns);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval_ns(doc, nullptr, "count(//x:a)", ns);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval_ns(doc, nullptr, "count(/x:r)", ns);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval_ns(doc, nullptr, "count(/descendant::x:r)", ns);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);

    leptris_xpath_ns_set_free(ns);
    leptris_document_free(doc);
}
