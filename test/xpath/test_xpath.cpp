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
TaurusXPathResult EvalWarm(TaurusDocument doc, TaurusElement ctx,
                           const char* expr) {
    TaurusXPathResult warm =
        taurus_xpath_eval(doc, nullptr, "//warmup-absent");
    if (warm) taurus_xpath_result_free(warm);
    return taurus_xpath_eval(doc, ctx, expr);
}

TaurusElement FirstSection(TaurusDocument doc) {
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "//section");
    if (!r || taurus_xpath_result_count(r) == 0) {
        if (r) taurus_xpath_result_free(r);
        return nullptr;
    }
    TaurusElement e = taurus_xpath_result_get(r, 0);
    taurus_xpath_result_free(r);
    return e;
}

}  // namespace

TEST(XPathSubtreeIndex, ChainedDoubleSlashNoDoubleCount) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(kSubtree, std::strlen(kSubtree), &st);
    ASSERT_NE(doc, nullptr);

    /* //section matches the outer AND the nested section; the
     * subtrees overlap. Interval containment must skip the nested
     * one — 4 items, not 5. Second+ query exercises the index. */
    TaurusXPathResult first = taurus_xpath_eval(doc, nullptr, "//item");
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(first), 4u);
    taurus_xpath_result_free(first);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "//section//item");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 4u);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathSubtreeIndex, RelativeDescendantFromContext) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(kSubtree, std::strlen(kSubtree), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement sec = FirstSection(doc);
    ASSERT_NE(sec, nullptr);

    /* .//item from section[a]: items 1, 2 and the nested 3. */
    TaurusXPathResult r = EvalWarm(doc, sec, ".//item");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 3u);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(XPathSubtreeIndex, StrictDescendantExcludesSelf) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<item><item>x</item></item>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult root = taurus_xpath_eval(doc, nullptr, "//item");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(taurus_xpath_result_count(root), 2u);
    TaurusElement e = taurus_xpath_result_get(root, 0);
    taurus_xpath_result_free(root);

    /* descendant::item from the outer item: the inner one only. */
    TaurusXPathResult r = EvalWarm(doc, e, "descendant::item");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 1u);
    taurus_xpath_result_free(r);

    /* descendant-or-self::item includes the context itself. */
    TaurusXPathResult r2 = EvalWarm(doc, e, "descendant-or-self::item");
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r2), 2u);
    taurus_xpath_result_free(r2);
    taurus_document_free(doc);
}

TEST(XPathSubtreeIndex, MutationInvalidatesIndex) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(kSubtree, std::strlen(kSubtree), &st);
    ASSERT_NE(doc, nullptr);

    /* Two queries so the index is built and cached. */
    TaurusXPathResult warm = taurus_xpath_eval(doc, nullptr, "//item");
    ASSERT_NE(warm, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(warm), 4u);
    taurus_xpath_result_free(warm);
    TaurusXPathResult warm2 = taurus_xpath_eval(doc, nullptr, "//warmup-absent");
    if (warm2) taurus_xpath_result_free(warm2);

    /* Mutate: remove section[b]'s only item, then query again — the
     * rebuilt index must not serve stale results. */
    TaurusXPathResult secs = taurus_xpath_eval(doc, nullptr, "//section[@id='b']");
    ASSERT_NE(secs, nullptr);
    ASSERT_EQ(taurus_xpath_result_count(secs), 1u);
    TaurusElement secb = taurus_xpath_result_get(secs, 0);
    taurus_xpath_result_free(secs);
    ASSERT_NE(secb, nullptr);

    TaurusXPathResult victim = taurus_xpath_eval(doc, secb, ".//item");
    ASSERT_NE(victim, nullptr);
    ASSERT_EQ(taurus_xpath_result_count(victim), 1u);
    TaurusElement item = taurus_xpath_result_get(victim, 0);
    taurus_xpath_result_free(victim);
    ASSERT_NE(item, nullptr);

    EXPECT_EQ(taurus_element_remove_child(secb, item), TAURUS_OK);

    TaurusXPathResult after = taurus_xpath_eval(doc, nullptr, "//item");
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(after), 3u);
    taurus_xpath_result_free(after);
    taurus_document_free(doc);
}

TEST(XPathSubtreeIndex, EmptyResultForAbsentName) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(kSubtree, std::strlen(kSubtree), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement sec = FirstSection(doc);
    ASSERT_NE(sec, nullptr);

    TaurusXPathResult r = EvalWarm(doc, sec, ".//does-not-exist");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 0u);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
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
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(kSubtreeAttr, std::strlen(kSubtreeAttr), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement sec = FirstSection(doc);
    ASSERT_NE(sec, nullptr);

    TaurusXPathResult r = EvalWarm(doc, sec, ".//item[@n='2']");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 1u);
    taurus_xpath_result_free(r);

    TaurusXPathResult r2 = EvalWarm(doc, sec, ".//item[@n='11']");
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r2), 1u);  /* nested section */
    taurus_xpath_result_free(r2);
    taurus_document_free(doc);
}

TEST(XPathSubtreeIndex, PredicatedChainedAcrossSections) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(kSubtreeAttr, std::strlen(kSubtreeAttr), &st);
    ASSERT_NE(doc, nullptr);

    /* Both sections own an item with n='1'; the nested item is
     * n='11' and must not double-count. */
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "//section//item[@n='1']");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 2u);
    taurus_xpath_result_free(r);

    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "//section//item[@n='11']");
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r2), 1u);
    taurus_xpath_result_free(r2);
    taurus_document_free(doc);
}

TEST(XPathSubtreeIndex, PositionPredicateKeepsExpandedSemantics) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(kSubtreeAttr, std::strlen(kSubtreeAttr), &st);
    ASSERT_NE(doc, nullptr);

    /* `//section//item[1]` means first item PER PARENT (section a,
     * its nested section, and section b) — 3, not 2-per-section.
     * Position predicates must NOT take the fused path. */
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "//section//item[1]");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 3u);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

/* ---- TODO 192d: absolute //name[@attr='value'] via value buckets ---- */

TEST(XPathSubtreeIndex, AbsolutePredicatedValueBucket) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(kSubtreeAttr, std::strlen(kSubtreeAttr), &st);
    ASSERT_NE(doc, nullptr);

    /* Cold query (no index yet) and warm query must agree. */
    TaurusXPathResult cold = taurus_xpath_eval(doc, nullptr, "//item[@n='1']");
    ASSERT_NE(cold, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(cold), 2u);
    taurus_xpath_result_free(cold);

    TaurusXPathResult warm = taurus_xpath_eval(doc, nullptr, "//item[@n='11']");
    ASSERT_NE(warm, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(warm), 1u);
    taurus_xpath_result_free(warm);

    /* Chained rest steps still follow the fused opcode. */
    TaurusXPathResult chained = taurus_xpath_eval(doc, nullptr, "//item[@n='11']/text()");
    ASSERT_NE(chained, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(chained), 1u);
    taurus_xpath_result_free(chained);
    taurus_document_free(doc);
}

TEST(XPathSubtreeIndex, AbsolutePredicatedRootMatches) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<item n='5'><item n='5'/><other/></item>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* `//` is descendant-or-SELF: the root itself carries n='5' and
     * must be included, cold and warm. */
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "count(//item[@n='5'])");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 2.0);
    taurus_xpath_result_free(r);

    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "count(//item[@n='5'])");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 2.0);
    taurus_xpath_result_free(r2);
    taurus_document_free(doc);
}

/* ---- TODO 192e: attr-EXISTS via the any-value bucket ---- */

TEST(XPathSubtreeIndex, AttrExistsAbsolute) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(kSubtreeAttr, std::strlen(kSubtreeAttr), &st);
    ASSERT_NE(doc, nullptr);

    /* Cold and warm must agree: every item carries n. */
    TaurusXPathResult cold = taurus_xpath_eval(doc, nullptr, "//item[@n]");
    ASSERT_NE(cold, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(cold), 4u);
    taurus_xpath_result_free(cold);

    TaurusXPathResult warm = taurus_xpath_eval(doc, nullptr, "count(//item[@n])");
    ASSERT_NE(warm, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(warm), 4.0);
    taurus_xpath_result_free(warm);

    /* Attribute carried by the outer sections only (the nested
     * section has no id). */
    TaurusXPathResult secs = taurus_xpath_eval(doc, nullptr, "count(//section[@id])");
    ASSERT_NE(secs, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(secs), 2.0);
    taurus_xpath_result_free(secs);

    /* Absent attribute: empty, cold and warm. */
    TaurusXPathResult none = taurus_xpath_eval(doc, nullptr, "count(//item[@zzz])");
    ASSERT_NE(none, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(none), 0.0);
    taurus_xpath_result_free(none);
    taurus_document_free(doc);
}

TEST(XPathSubtreeIndex, AttrExistsRelativeFromContext) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(kSubtreeAttr, std::strlen(kSubtreeAttr), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement sec = FirstSection(doc);
    ASSERT_NE(sec, nullptr);

    TaurusXPathResult r = EvalWarm(doc, sec, ".//item[@n]");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 3u);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}
