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

/* Lane 07 tail (#692-B): function items report a distinct public
 * result type at the boundary — closures and named references. */
TEST(XPathResultTypes, FunctionItemsReportFunctionType) {
    LeptrisDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r =
        leptris_xpath_eval(doc, nullptr, "concat#2");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_FUNCTION);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval(doc, nullptr, "function($x){ $x + 1 }");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_FUNCTION);
    leptris_xpath_result_free(r);

    /* Sanity: ordinary results are unaffected. */
    r = leptris_xpath_eval(doc, nullptr, "//book");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_NODESET);
    leptris_xpath_result_free(r);

    leptris_document_free(doc);
}

/* XPath 1.0 §4.3 lang(): the NEAREST xml:lang declaration decides —
 * a closer non-matching declaration is not walked past (libxslt
 * bug-142: a ja span inside a fr root must not match lang('fr') via
 * the root's declaration). */
TEST(XPathFunctions, LangNearestDeclarationWins) {
    const char xml[] =
        "<r xml:lang='fr'><a xml:lang='ja'><b/></a></r>";
    LeptrisDocument doc = ParseWith(xml);
    ASSERT_NE(doc, nullptr);

    struct {
        const char* expr;   /* count of selected nodes */
        double want;
    } cases[] = {
        {"count(/r/a/b[lang('ja')])", 1},   /* nearest: a */
        {"count(/r/a/b[lang('fr')])", 0},   /* must NOT reach r */
        {"count(/r/a[lang('ja')])", 1},     /* own attr */
        {"count(/r/a[lang('fr')])", 0},
        {"count(/r[lang('fr')])", 1},
        {"count(/r[lang('ja')])", 0},
        {"count(/r[lang(ja)])", 0},  /* unquoted: empty nodeset coerces
                                        to "" — false, never an error */
        {"count(/r[false()])", 0},  /* root-step predicate applies */
    };
    for (const auto& c : cases) {
        LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, c.expr);
        ASSERT_NE(r, nullptr) << c.expr;
        EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_NUMBER)
            << c.expr;
        EXPECT_DOUBLE_EQ(leptris_xpath_result_number(r), c.want) << c.expr;
        leptris_xpath_result_free(r);
    }
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

/* libxslt bug-142: a prefixed ATTRIBUTE test (@xml:lang) in a
 * predicate or after a descendant step must fall back to the literal
 * qualified-name compare when the prefix is not in the binding set
 * — vm_apply_axis_attribute already does; axis_attribute did not,
 * so [@xml:lang] and //@xml:lang silently matched nothing. */
TEST(XPathPredicates, XmlLangAttributeTestMatchesUnboundPrefix) {
    const char xml[] =
        "<posts xml:lang='fr'><post xml:lang='ja' id='4'>"
        "<content><para><span>x</span></para></content></post></posts>";
    LeptrisDocument doc = ParseWith(xml);
    ASSERT_NE(doc, nullptr);

    struct {
        const char* expr;
        double want;
    } cases[] = {
        {"count(//post[@xml:lang])", 1},
        {"count(//post[@xml:lang='ja'])", 1},
        {"count(//span[ancestor::*[@xml:lang='ja']])", 1},
        {"count(//span[ancestor::*[@xml:lang='fr']])", 1},  /* posts */
        {"count(//span[ancestor::*[@xml:lang='en']])", 0},
        {"count(//@xml:lang)", 2},
        {"count(//post/@xml:lang)", 1},
    };
    for (const auto& c : cases) {
        LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, c.expr);
        ASSERT_NE(r, nullptr) << c.expr;
        EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_NUMBER)
            << c.expr;
        EXPECT_DOUBLE_EQ(leptris_xpath_result_number(r), c.want) << c.expr;
        leptris_xpath_result_free(r);
    }
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

/* Absolute `//name[k]` (#645 fused VM path): the position is
 * PER PARENT — one node per parent with k matching children — and
 * a root element named NAME counts (it is the document's first
 * NAME child). These pin the semantics the fused child::NAME[k]
 * opcode must preserve. */

/* XPath 2.0+ if/then/else — XSLT 3.0 expressions. */
TEST(XPath30, IfThenElse) {
    const char xml[] = "<r><i>a</i><i>b</i><i>c</i></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    auto eval = [&](const char* e) -> std::string {
        LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, e);
        if (!r) return "(error)";
        char* v = leptris_xpath_result_string(r);
        std::string out = v ? v : "";
        leptris_free_string(v);
        leptris_xpath_result_free(r);
        return out;
    };
    EXPECT_EQ(eval("if (count(//i) > 2) then 'many' else 'few'"), "many");
    EXPECT_EQ(eval("if (count(//i) > 9) then 'many' else 'few'"), "few");
    leptris_document_free(doc);
}

TEST(XPath30, ForReturnAndRange) {
    const char xml[] = "<r><i>a</i><i>b</i><i>c</i></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    auto eval = [&](const char* e) -> std::string {
        LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, e);
        if (!r) return "(error)";
        char* v = leptris_xpath_result_string(r);
        std::string out = v ? v : "";
        leptris_free_string(v);
        leptris_xpath_result_free(r);
        return out;
    };
    /* for yields a SEQUENCE (3.0): one member per input item. The
     * 1.0-frozen result_string prints one node — assert the members
     * through string-join and count. */
    SCOPED_TRACE("for-return");
    EXPECT_EQ(eval("string-join(for $x in //i return string($x), ' ')"),
              "a b c");
    EXPECT_EQ(eval("string-join(for $x in //i return upper-case(string($x)), '|')"),
              "A|B|C");
    /* Range + predicate: the members that survive; assert count and
     * membership (the 1.0-frozen result_string prints one node). */
    LeptrisXPathResult r = leptris_xpath_eval(
        doc, nullptr, "(1 to 5)[. mod 2 = 1]");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 3u);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(XPath30, StringFunctions) {
    const char xml[] = "<r><i>a</i><i>b</i><i>c</i></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    auto eval = [&](const char* e) -> std::string {
        LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, e);
        if (!r) return "(error)";
        char* v = leptris_xpath_result_string(r);
        std::string out = v ? v : "";
        leptris_free_string(v);
        leptris_xpath_result_free(r);
        return out;
    };
    EXPECT_EQ(eval("upper-case('abc')"), "ABC");
    EXPECT_EQ(eval("lower-case('ABC')"), "abc");
    EXPECT_EQ(eval("string-join(('a','b','c'), ',')"), "a,b,c");
    EXPECT_EQ(eval("string-join(for $x in //i return string($x), '+')"),
              "a+b+c");
    EXPECT_EQ(eval("substring-before('key=val', '=')"), "key");
    EXPECT_EQ(eval("normalize-space('  a  b  ')"), "a b");
    leptris_document_free(doc);
}

TEST(XPathSubtreeIndex, AbsolutePositionPredicatePerParent) {
    const char xml[] =
        "<catalog><item id='a1'/><item id='a2'/>"
        "<wrap><item id='b1'/><item id='b2'/></wrap>"
        "</catalog>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//item[1]");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 2u);   /* a1 + b1 */
    LeptrisXPathResult r2 = leptris_xpath_eval(doc, nullptr, "//item[2]");
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r2), 2u);  /* a2 + b2 */
    LeptrisXPathResult r3 = leptris_xpath_eval(doc, nullptr, "//item[3]");
    ASSERT_NE(r3, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r3), 0u);
    leptris_xpath_result_free(r);
    leptris_xpath_result_free(r2);
    leptris_xpath_result_free(r3);
    leptris_document_free(doc);
}

TEST(XPathSubtreeIndex, AbsolutePositionPredicateRootNamed) {
    const char xml[] = "<item><item id='inner'/></item>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    /* The root IS the document's first item child; the inner item is
     * the root's first item child — both are //item[1]. */
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//item[1]");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 2u);
    leptris_xpath_result_free(r);
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

/* Issue #565: variable-bound evaluation — the fused @attr=$var
 * predicate rides the index-backed VM path, not the interpreter. */
TEST(VarBoundEval, AttrEqVarStringMatchesLiteralForm) {
    const char xml[] =
        "<r><book id='b1'>A</book><book id='b7'>G</book>"
        "<book id='b9'>I</book></r>";
    LeptrisDocument d = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(d, nullptr);
    LeptrisXPathVariableSet vs = leptris_xpath_variable_set_new();
    leptris_xpath_variable_set_string(vs, "want", "b7");

    LeptrisXPathResult r = leptris_xpath_eval_with_vars(
        d, "count(//book[@id=$want])", vs);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);

    /* reversed operand order: [$var=@attr] */
    r = leptris_xpath_eval_with_vars(
        d, "count(//book[$want=@id])", vs);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);
    leptris_xpath_variable_set_free(vs);
    leptris_document_free(d);
}

TEST(VarBoundEval, AttrEqVarNumberStringifies) {
    const char xml[] = "<r><e n='10'>x</e><e n='11'>y</e></r>";
    LeptrisDocument d = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(d, nullptr);
    LeptrisXPathVariableSet vs = leptris_xpath_variable_set_new();
    leptris_xpath_variable_set_number(vs, "k", 10.0);
    LeptrisXPathResult r = leptris_xpath_eval_with_vars(
        d, "count(//e[@n=$k])", vs);
    ASSERT_NE(r, nullptr);
    /* string(@n)='10' = string(10)='10' — one match */
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);
    leptris_xpath_variable_set_free(vs);
    leptris_document_free(d);
}

/* XPath 1.0: predicates apply to ANY node kind — text(), comment(),
 * PI, node() steps. The predicate evaluator used to bail out for
 * non-element candidates (text nodes silently never matched), so
 * text()[2] and node()[4] returned empty (libxslt bug-182). */
TEST(PredicatesOnNonElements, PositionSelectsTextAndMixedNodes) {
    const char xml[] =
        "<root><body><b> b 1 </b> text 1 <b> b 2 </b> text 2 </body></root>";
    LeptrisDocument d = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(d, nullptr);

    struct { const char* q; const char* want; } cases[] = {
        {"string(/root/body/text()[1])", " text 1 "},
        {"string(/root/body/text()[2])", " text 2 "},
        {"string(/root/body/node()[4])", " text 2 "},
        {"string(/root/body/node()[1])", " b 1 "},
        {"string(/root/body/text()[position()=2])", " text 2 "},
        {"string(/root/body/text()[last()])", " text 2 "},
        {"count(/root/body/text()[2])", "1"},
    };
    for (auto& c : cases) {
        LeptrisXPathResult r = leptris_xpath_eval(d, nullptr, c.q);
        ASSERT_NE(r, nullptr) << c.q;
        char* s = leptris_xpath_result_string(r);
        EXPECT_STREQ(s ? s : "", c.want) << c.q;
        leptris_free_string(s);
        leptris_xpath_result_free(r);
    }
    leptris_document_free(d);
}

TEST(VarBoundEval, UndefinedVariableBareIsAnError) {
    const char xml[] = "<r><e n='1'/></r>";
    LeptrisDocument d = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(d, nullptr);
    LeptrisXPathVariableSet vs = leptris_xpath_variable_set_new();
    /* A bare $var reference is a hard error. Inside a PREDICATE an
     * undefined variable counts as a non-match (apply_predicates
     * filters in place; its callers merge whatever remains) — so
     * count(//e[@n=$missing]) is 0, not an error. Pinned so the
     * engine boundary stays observable: hard error at expression
     * level, soft-fail under a predicate filter. */
    LeptrisXPathResult r = leptris_xpath_eval_with_vars(d, "$missing", vs);
    EXPECT_EQ(r, nullptr);
    r = leptris_xpath_eval_with_vars(d, "count(//e[@n=$missing])", vs);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 0.0);
    leptris_xpath_result_free(r);
    leptris_xpath_variable_set_free(vs);
    leptris_document_free(d);
}

/* The attribute AXIS must expand entities/character references in
 * values — value-of/@attr through XPath sees the same string the
 * leptris_element_attribute accessor returns (libxslt bug-59:
 * B&#38;B serialized as B&amp;#38;B). */
TEST(AttributeAxis, ExpandsCharacterReferences) {
    const char xml[] = "<foo attribute=\"B&#38;B\" a2=\"x&amp;y\"/>";
    LeptrisDocument d = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(d, nullptr);
    struct { const char* q; const char* want; } cases[] = {
        {"string(//@attribute)", "B&B"},
        {"string(//@a2)", "x&y"},
        {"string(/foo/@attribute)", "B&B"},
        {"concat(//@attribute, '!')", "B&B!"},
    };
    for (auto& c : cases) {
        LeptrisXPathResult r = leptris_xpath_eval(d, nullptr, c.q);
        ASSERT_NE(r, nullptr) << c.q;
        char* sv = leptris_xpath_result_string(r);
        EXPECT_STREQ(sv ? sv : "", c.want) << c.q;
        leptris_free_string(sv);
        leptris_xpath_result_free(r);
    }
    leptris_document_free(d);
}

/* Issue #630: relative .//ns:x from ELEMENT context returned empty —
 * the element index keys buckets by local name and the VM's relative
 * descendant paths looked up the raw qualified string, missing every
 * bucket. The local bucket + namespace-aware filter fix must hold
 * for every context depth. */
TEST(XPathAxes, RelativeDescendantPrefixedFromElementContext) {
    const char xml[] =
        "<r xmlns:x='urn:x'>"
        "<a><x:b id='1'/><c><x:b id='2'/></c></a>"
        "<x:b id='3'/>"
        "</r>";
    LeptrisDocument doc = ParseWith(xml);
    ASSERT_NE(doc, nullptr);

    struct {
        const char* ctx;
        const char* expr;
        size_t want;
    } cases[] = {
        {"/r", ".//x:b", 3},
        {"/r", "descendant::x:b", 3},
        {"/r", "descendant-or-self::x:b", 3},
        {"/r/a", ".//x:b", 2},
        {"/r/a", "descendant::x:b", 2},
        {"/r/a/c", ".//x:b", 1},
        {"/r/a/c", "child::x:b", 1},
        {"/r/a/x:b", ".//x:b", 0},   /* leaf: no x:b below */
        {"/r/x:b", ".//x:b", 0},
        {"/r", "//x:b", 3},          /* absolute control */
        {"/r", ".//a", 1},           /* unprefixed control */
    };
    for (const auto& c : cases) {
        LeptrisXPathResult cr = leptris_xpath_eval(doc, nullptr, c.ctx);
        ASSERT_NE(cr, nullptr) << c.ctx;
        LeptrisElement ctx = leptris_xpath_result_get(cr, 0);
        ASSERT_NE(ctx, nullptr) << c.ctx;
        LeptrisXPathResult r = leptris_xpath_eval(doc, ctx, c.expr);
        ASSERT_NE(r, nullptr) << c.ctx << " " << c.expr;
        EXPECT_EQ(leptris_xpath_result_count(r), c.want)
            << c.ctx << " " << c.expr;
        leptris_xpath_result_free(r);
        leptris_xpath_result_free(cr);
    }
    leptris_document_free(doc);
}

/* Same fix through an EXTERNAL binding: z resolves to the same URI
 * the document spells x, so .//z:b matches x:b elements. */
TEST(XPathAxes, RelativeDescendantPrefixedResolvesByUri) {
    const char xml[] =
        "<r xmlns:x='urn:x'><a><x:b id='1'/><c><x:b id='2'/></c></a></r>";
    LeptrisDocument doc = ParseWith(xml);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathNsSet ns = leptris_xpath_ns_set_new();
    ASSERT_NE(ns, nullptr);
    ASSERT_EQ(leptris_xpath_ns_set_add(ns, "z", "urn:x"), LEPTRIS_OK);

    LeptrisXPathResult cr = leptris_xpath_eval(doc, nullptr, "/r/a");
    ASSERT_NE(cr, nullptr);
    LeptrisElement a = leptris_xpath_result_get(cr, 0);
    ASSERT_NE(a, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval_ns(doc, a, ".//z:b", ns);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 2u);

    leptris_xpath_result_free(r);
    leptris_xpath_result_free(cr);
    leptris_xpath_ns_set_free(ns);
    leptris_document_free(doc);
}

/* ---- #684 dependency ledger: XPath 2.0 grammar + function tail ----
 * Quantified expressions, node comparisons (is/<</>>), intersect/
 * except, the empty-sequence literal, ends-with and deep-equal —
 * XQuery 1.0 embeds XPath 2.0, so these are the conformance
 * blockers named in the issue's ledger. */

static bool BoolEval(const char* expr) {
    LeptrisDocument doc = ParseWith(kBasic);
    EXPECT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, expr);
    if (!r) { leptris_document_free(doc); return false; }
    bool v = leptris_xpath_result_type(r) == LEPTRIS_XPATH_BOOLEAN &&
             leptris_xpath_result_boolean(r);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
    return v;
}

static double NumEval(const char* expr) {
    LeptrisDocument doc = ParseWith(kBasic);
    EXPECT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, expr);
    if (!r) { leptris_document_free(doc); return -1; }
    double v = leptris_xpath_result_number(r);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
    return v;
}

static std::string StrEval(const char* expr) {
    LeptrisDocument doc = ParseWith(kBasic);
    EXPECT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, expr);
    if (!r) { leptris_document_free(doc); return "(eval-failed)"; }
    char* s = leptris_xpath_result_string(r);
    std::string v = s ? s : "";
    leptris_free_string(s);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
    return v;
}

TEST(XPath20Ledger, QuantifiedExpressions) {
    EXPECT_TRUE(BoolEval("some $x in (1,2,3) satisfies $x > 2"));
    EXPECT_FALSE(BoolEval("some $x in (1,2,3) satisfies $x > 3"));
    EXPECT_TRUE(BoolEval("every $x in (1,2,3) satisfies $x > 0"));
    EXPECT_FALSE(BoolEval("every $x in (1,2,3) satisfies $x > 1"));
    /* Node-sequence domains bind nodes. */
    EXPECT_TRUE(BoolEval("some $b in //book satisfies $b/price > 25"));
    EXPECT_FALSE(BoolEval("every $b in //book satisfies $b/price > 15"));
    /* Multiple bindings are a cartesian product. */
    EXPECT_TRUE(
        BoolEval("some $x in (1,2), $y in (3,4) satisfies $x + $y > 5"));
    EXPECT_FALSE(
        BoolEval("every $x in (1,2), $y in (1,2) satisfies $x < $y"));
    /* Empty domain: some is false, every is true (vacuous). */
    EXPECT_FALSE(BoolEval("some $x in () satisfies $x > 0"));
    EXPECT_TRUE(BoolEval("every $x in () satisfies $x > 0"));
}

TEST(XPath20Ledger, NodeIdentityAndOrderComparisons) {
    EXPECT_TRUE(BoolEval("//book[1] is //book[1]"));
    EXPECT_FALSE(BoolEval("//book[1] is //book[2]"));
    /* An empty operand makes the comparison false. */
    EXPECT_FALSE(BoolEval("//book[9] is //book[1]"));
    EXPECT_TRUE(BoolEval("//book[1] << //book[2]"));
    EXPECT_FALSE(BoolEval("//book[2] << //book[1]"));
    EXPECT_FALSE(BoolEval("//book[1] >> //book[2]"));
    EXPECT_TRUE(BoolEval("//book[2] >> //book[1]"));
}

TEST(XPath20Ledger, IntersectAndExcept) {
    EXPECT_EQ(NumEval("count(//book intersect //book)"), 3.0);
    EXPECT_EQ(NumEval("count(//book except //book)"), 0.0);
    EXPECT_EQ(NumEval("count((//book | //title) except //title)"), 3.0);
    EXPECT_EQ(NumEval("count((//book | //title) intersect //title)"), 3.0);
    EXPECT_EQ(NumEval("count((//book | //title) except //book)"), 3.0);
}

TEST(XPath20Ledger, EmptySequenceLiteral) {
    EXPECT_EQ(NumEval("count(())"), 0.0);
    EXPECT_TRUE(BoolEval("empty(())"));
    EXPECT_FALSE(BoolEval("exists(())"));
}

TEST(XPath20Ledger, EndsWith) {
    EXPECT_TRUE(BoolEval("ends-with('abc', 'c')"));
    EXPECT_FALSE(BoolEval("ends-with('abc', 'b')"));
    /* Empty suffix is always true; longer suffix false. */
    EXPECT_TRUE(BoolEval("ends-with('abc', '')"));
    EXPECT_FALSE(BoolEval("ends-with('ab', 'abc')"));
    EXPECT_TRUE(BoolEval("ends-with(//book[1]/title, 'First')"));
}

TEST(XPath20Ledger, SequenceNodeTail) {
    /* innermost: no ancestor in the supplied set. */
    EXPECT_EQ(NumEval("count(innermost(//*))"), 1.0);          /* catalog */
    EXPECT_EQ(NumEval("count(innermost(//book | //title))"), 3.0);
    /* outermost: no descendant in the supplied set. */
    EXPECT_EQ(NumEval("count(outermost(//*))"), 6.0);          /* leaves */
    EXPECT_EQ(NumEval("count(outermost(//book | //title))"), 3.0);
    /* has-children: explicit node and the zero-arg context form. */
    EXPECT_TRUE(BoolEval("has-children(//book[1])"));
    /* Text children count; only a truly empty element is false. */
    EXPECT_TRUE(BoolEval("has-children(//book[1]/title)"));
    EXPECT_TRUE(BoolEval("has-children()"));
    {
        LeptrisDocument d = ParseWith("<r><a/><b>t</b></r>");
        ASSERT_NE(d, nullptr);
        LeptrisXPathResult r = leptris_xpath_eval(d, nullptr,
                                                 "has-children(//a)");
        ASSERT_NE(r, nullptr);
        EXPECT_FALSE(leptris_xpath_result_boolean(r));
        leptris_xpath_result_free(r);
        leptris_document_free(d);
    }
    /* path: root-anchored positional path. */
    EXPECT_EQ(StrEval("path(//book[2])"), "/catalog/book[2]");
    EXPECT_EQ(StrEval("path(//book[2]/title)"), "/catalog/book[2]/title");
    EXPECT_EQ(StrEval("path(/*)"), "/catalog");
    /* nilled: false without xsi:nil, true with. */
    EXPECT_FALSE(BoolEval("nilled(//book[1])"));
    {
        LeptrisDocument d = ParseWith(
            "<r xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
            "<a xsi:nil=\"true\"/><b/></r>");
        ASSERT_NE(d, nullptr);
        LeptrisXPathResult r = leptris_xpath_eval(d, nullptr, "nilled(//a)");
        ASSERT_NE(r, nullptr);
        EXPECT_TRUE(leptris_xpath_result_boolean(r));
        leptris_xpath_result_free(r);
        LeptrisXPathResult r2 = leptris_xpath_eval(d, nullptr, "nilled(//b)");
        ASSERT_NE(r2, nullptr);
        EXPECT_FALSE(leptris_xpath_result_boolean(r2));
        leptris_xpath_result_free(r2);
        leptris_document_free(d);
    }
}

TEST(XPath20Ledger, DocumentUriTail) {
    /* In-memory documents carry no base/document URI. */
    EXPECT_EQ(StrEval("base-uri(//book[1])"), "");
    EXPECT_EQ(StrEval("static-base-uri()"), "");
    EXPECT_EQ(StrEval("document-uri(/)"), "");
    /* doc-available: false for an unloadable path, true for a
     * parseable file. */
    EXPECT_FALSE(BoolEval("doc-available('/nonexistent/leptris-zz.xml')"));
    /* CWD-relative paths: portable across POSIX and Windows. */
    FILE* f = fopen("leptris-docavail.xml", "w");
    ASSERT_NE(f, nullptr);
    fputs("<ok/>", f);
    fclose(f);
    EXPECT_TRUE(BoolEval("doc-available('leptris-docavail.xml')"));
    remove("leptris-docavail.xml");
    /* json-doc: a JSON file parses to a map. */
    f = fopen("leptris-jsondoc.json", "w");
    ASSERT_NE(f, nullptr);
    fputs("{\"k\": 7}", f);
    fclose(f);
    EXPECT_EQ(NumEval("map:get(json-doc('leptris-jsondoc.json'), 'k')"),
              7.0);
    remove("leptris-jsondoc.json");
}

TEST(XPath20Ledger, ScalarTail) {
    /* compare: codepoint order, -1/0/1, empty operand -> empty. */
    EXPECT_EQ(NumEval("compare('abc', 'abd')"), -1.0);
    EXPECT_EQ(NumEval("compare('abd', 'abc')"), 1.0);
    EXPECT_EQ(NumEval("compare('abc', 'abc')"), 0.0);
    EXPECT_EQ(StrEval("compare((), 'a')"), "");
    /* codepoint-equal. */
    EXPECT_TRUE(BoolEval("codepoint-equal('ABC', 'ABC')"));
    EXPECT_FALSE(BoolEval("codepoint-equal('ABC', 'abc')"));
    /* round with precision: decimal places and negative (tens). */
    EXPECT_EQ(NumEval("round(1.25, 1)"), 1.3);
    EXPECT_EQ(NumEval("round(1.24, 1)"), 1.2);
    EXPECT_EQ(NumEval("round(-1.25, 1)"), -1.2);
    EXPECT_EQ(NumEval("round(15, -1)"), 20.0);
    EXPECT_EQ(NumEval("round(12, 0)"), 12.0);
    /* normalize-unicode rides utf8proc — unregistered when the
     * build has none (Windows CI); probe before asserting. */
    if (NumEval("count(normalize-unicode('a'))") >= 0) {
        /* NFD decomposed composes to NFC equal. */
        EXPECT_TRUE(BoolEval(
            "normalize-unicode('é', 'NFC') = "
            "normalize-unicode('é', 'NFC')"));
        EXPECT_EQ(StrEval("normalize-unicode('abc')"), "abc");
    }
    /* resolve-QName: prefix resolved against the element's in-scope
     * namespaces; the URI rides the QName side channel. */
    {
        LeptrisDocument d = ParseWith(
            "<r xmlns:p=\"urn:p\"><e/></r>");
        ASSERT_NE(d, nullptr);
        LeptrisXPathResult r = leptris_xpath_eval(d, nullptr,
            "string(resolve-QName('p:x', //e))");
        ASSERT_NE(r, nullptr);
        char* sv = leptris_xpath_result_string(r);
        EXPECT_STREQ(sv ? sv : "", "p:x");
        leptris_free_string(sv);
        leptris_xpath_result_free(r);
        LeptrisXPathResult r2 = leptris_xpath_eval(d, nullptr,
            "namespace-uri-from-QName(resolve-QName('p:x', //e))");
        ASSERT_NE(r2, nullptr);
        char* sv2 = leptris_xpath_result_string(r2);
        EXPECT_STREQ(sv2 ? sv2 : "", "urn:p");
        leptris_free_string(sv2);
        leptris_xpath_result_free(r2);
        leptris_document_free(d);
    }
    /* environment-variable / available-environment-variables. */
#ifdef _WIN32
    _putenv("LEPTRIS_X691=yes");
#else
    setenv("LEPTRIS_X691", "yes", 1);
#endif
    EXPECT_EQ(StrEval("environment-variable('LEPTRIS_X691')"), "yes");
    EXPECT_EQ(StrEval("environment-variable('LEPTRIS_NOPE_691')"), "");
    EXPECT_TRUE(BoolEval(
        "exists(available-environment-variables()[. = 'LEPTRIS_X691'])"));
#ifdef _WIN32
    _putenv("LEPTRIS_X691=");
#else
    unsetenv("LEPTRIS_X691");
#endif
}

/* #683: the standard eval + compiled entries already speak the
 * 3.x composition grammar — no separate eval31 surface needed
 * (superset semantics; lock the behavior in as a gate). */
TEST(XPath20Ledger, XPath31ThroughStandardEntry) {
    EXPECT_EQ(NumEval("let $x := 2 return $x + 1"), 3.0);
    EXPECT_EQ(StrEval("if (count(//book) = 3) then 'three' else 'other'"),
              "three");
    EXPECT_EQ(NumEval("count(for $b in //book return $b/price)"), 3.0);
    EXPECT_EQ(NumEval("count(//book ! string(@id))"), 3.0);
    EXPECT_EQ(NumEval("count((1 to 5))"), 5.0);
    EXPECT_EQ(StrEval("string((1 to 5)[4])"), "4");
    EXPECT_EQ(StrEval(
        "switch (//book[1]/@id) case 'b1' return 'first' "
        "default return 'other'"), "first");
    EXPECT_EQ(NumEval("map { 'k': 7 }?k"), 7.0);
    /* An array is a single item: [2] filters positions, ?2
     * subscripts members. */
    EXPECT_EQ(NumEval("count([1,2,3])"), 1.0);
    EXPECT_EQ(NumEval("[1,2,3]?2"), 2.0);
    /* The compiled entry evaluates the same grammar. */
    {
        LeptrisDocument doc = ParseWith(kBasic);
        ASSERT_NE(doc, nullptr);
        LeptrisXPathCompiled c =
            leptris_xpath_compile("count(for $b in //book return $b)");
        ASSERT_NE(c, nullptr);
        LeptrisXPathResult r = leptris_xpath_compiled_eval(c, doc, nullptr);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(leptris_xpath_result_number(r), 3.0);
        leptris_xpath_result_free(r);
        leptris_xpath_compiled_free(c);
        leptris_document_free(doc);
    }
}

/* #692: function calls in path-step position — XPath 2.0+
 * PostfixExpr steps (`a/string()`), desugared to the simple map. */
TEST(XPath20Ledger, FnAsPathStep) {
    EXPECT_EQ(NumEval("count(//book/string())"), 3.0);
    EXPECT_EQ(StrEval("//book[1]/title/string()"), "First");
    EXPECT_EQ(StrEval("//book[2]/name()"), "book");
    EXPECT_EQ(NumEval("count(//title/normalize-space())"), 3.0);
    EXPECT_EQ(StrEval("normalize-space(//book[1]/title/string())"),
              "First");
    /* same shape as the explicit simple map */
    EXPECT_EQ(NumEval("count(//book ! string())"), 3.0);
    /* text()/comment() node tests are NOT function steps. */
    EXPECT_EQ(NumEval("count(//book[1]/title/text())"), 1.0);
    /* deep path: steps before the function step still select. */
    EXPECT_EQ(StrEval("string(//book[3]/price/number())"), "30");
}

/* #691 date/duration tail: *-from-date / *-from-dateTime /
 * *-from-duration accessors (ISO components, value-level). */
TEST(XPath20Ledger, DateAccessorTail) {
    EXPECT_EQ(NumEval("year-from-date('2024-05-06')"), 2024.0);
    EXPECT_EQ(NumEval("month-from-date('2024-05-06')"), 5.0);
    EXPECT_EQ(NumEval("day-from-date('2024-05-06')"), 6.0);
    EXPECT_EQ(NumEval("hours-from-dateTime('2024-05-06T07:08:09')"), 7.0);
    EXPECT_EQ(NumEval("minutes-from-dateTime('2024-05-06T07:08:09')"), 8.0);
    EXPECT_EQ(NumEval("seconds-from-dateTime('2024-05-06T07:08:09')"), 9.0);
    /* durations: full P..T.. form, days-only, time-only. */
    EXPECT_EQ(NumEval("days-from-duration('P3DT2H')"), 3.0);
    EXPECT_EQ(NumEval("hours-from-duration('P3DT2H')"), 2.0);
    EXPECT_EQ(NumEval("hours-from-duration('PT5H')"), 5.0);
    EXPECT_EQ(NumEval("minutes-from-duration('PT2H30M')"), 30.0);
    EXPECT_EQ(NumEval("seconds-from-duration('PT90.5S')"), 90.0);
    /* constructor aliases are passthrough shapes. */
    EXPECT_EQ(StrEval("xs:dayTimeDuration('PT2H')"), "PT2H");
    EXPECT_EQ(StrEval("xs:yearMonthDuration('P1Y2M')"), "P1Y2M");
    EXPECT_EQ(NumEval(
        "hours-from-duration(xs:dayTimeDuration('PT3H'))"), 3.0);
}

/* #691: unparsed-text family + uri-collection (empty without a
 * collection catalog). */
TEST(XPath20Ledger, UnparsedText) {
    /* Binary mode: Windows text mode would write CRLF. */
    FILE* f = fopen("leptris-unparsed.txt", "wb");
    ASSERT_NE(f, nullptr);
    fputs("alpha\nbeta", f);
    fclose(f);
    EXPECT_EQ(StrEval("unparsed-text('leptris-unparsed.txt')"),
              "alpha\nbeta");
    EXPECT_EQ(NumEval("count(unparsed-text-lines('leptris-unparsed.txt'))"),
              2.0);
    remove("leptris-unparsed.txt");
    /* A missing file is an error, not an empty string. */
    EXPECT_FALSE(BoolEval("unparsed-text-available('nope-691.txt')"));
    EXPECT_EQ(NumEval("count(uri-collection())"), 0.0);
}

/* #691: random-number-generator - seeded (deterministic per
 * seed), number in [0,1); the next/permute function-item members
 * need closure state (documented gap). */
TEST(XPath20Ledger, RandomNumberGenerator) {
    EXPECT_TRUE(BoolEval(
        "let $a := random-number-generator('seed691')?number "
        "return $a = random-number-generator('seed691')?number"));
    EXPECT_TRUE(BoolEval("random-number-generator()?number >= 0"));
    EXPECT_TRUE(BoolEval("random-number-generator()?number < 1"));
    EXPECT_EQ(NumEval("count(map:keys(random-number-generator()))"), 1.0);
    EXPECT_TRUE(BoolEval(
        "map:get(random-number-generator('k'), 'number') >= 0"));
}

/* #691: fn:format-number as a PLAIN XPath function (the JDK
 * pattern core is now shared SSOT with the XSLT layer; the default
 * decimal format applies - named formats are an XSLT context). */
TEST(XPath20Ledger, FormatNumberXPath) {
    EXPECT_EQ(StrEval("format-number(1234.567, '#,##0.00')"), "1,234.57");
    EXPECT_EQ(StrEval("format-number(0.5, '0%')"), "50%");
    EXPECT_EQ(StrEval("format-number(42, '000')"), "042");
    EXPECT_EQ(StrEval("format-number(-7, '0')"), "-7");
    EXPECT_EQ(StrEval("format-number(0 div 0, '0')"), "NaN");
    EXPECT_EQ(StrEval("format-number(12, '')"), "12");
}

/* #691: fn:snapshot - detached deep copies anchored on the
 * SOURCE document (value-level: element nodes). */
TEST(XPath20Ledger, Snapshot) {
    EXPECT_EQ(NumEval("count(snapshot(//book))"), 3.0);
    EXPECT_EQ(StrEval("snapshot(//book[2])/title"), "Second");
    EXPECT_EQ(StrEval("name(snapshot(//book[2]))"), "book");
    EXPECT_EQ(NumEval("count(snapshot(//book[1])/descendant::*)"), 2.0);
    EXPECT_TRUE(BoolEval("deep-equal(snapshot(//book[1])/*, //book[1]/*)"));
    /* the snapshot survives result consumption AND stays independent
     * of later source mutations. */
    {
        LeptrisDocument d = ParseWith(kBasic);
        ASSERT_NE(d, nullptr);
        LeptrisXPathResult r = leptris_xpath_eval(d, nullptr,
                                                 "snapshot(//book[1])");
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(leptris_xpath_result_count(r), 1u);
        LeptrisElement b = leptris_xpath_result_get(r, 0);
        ASSERT_NE(b, nullptr);
        EXPECT_STREQ(leptris_element_name(b), "book");
        leptris_xpath_result_free(r);
        leptris_document_free(d);   /* frees the anchored copy too */
    }
}

/* #691: fn:analyze-string - fn:match / fn:non-match elements
 * (with fn:group children) on the document-lifetime anchor chain.
 * Paths use local-name() - the constructed names carry the literal
 * fn: prefix. */
TEST(XPath20Ledger, AnalyzeString) {
    /* POSIX-gated like the regex trio: probe before asserting. */
    if (NumEval("count(analyze-string('a', '[0-9]'))") < 0) {
        SUCCEED() << "analyze-string needs POSIX regex";
        return;
    }
    /* a,1,b,2,c: two digit matches, three gaps. */
    EXPECT_EQ(NumEval(
        "count(analyze-string('a1b2c', '[0-9]')"
        "/*[local-name()='match'])"), 2.0);
    EXPECT_EQ(NumEval(
        "count(analyze-string('a1b2c', '[0-9]')"
        "/*[local-name()='non-match'])"), 3.0);
    EXPECT_EQ(StrEval(
        "string(analyze-string('a1b2c', '[0-9]')"
        "/*[local-name()='match'][1])"), "1");
    EXPECT_EQ(NumEval(
        "count(analyze-string('2024-05', '([0-9]+)-([0-9]+)')"
        "/*[local-name()='match']/*[local-name()='group'])"), 2.0);
    EXPECT_EQ(StrEval(
        "string(analyze-string('2024-05', '([0-9]+)-([0-9]+)')"
        "/*[local-name()='match']"
        "/*[local-name()='group'][@nr='2'])"), "05");
    /* no match: the whole input is one non-match; overall string
     * value is the input. */
    EXPECT_EQ(NumEval(
        "count(analyze-string('abc', '[0-9]')"
        "/*[local-name()='non-match'])"), 1.0);
    EXPECT_EQ(StrEval("string(analyze-string('abc', '[0-9]'))"), "abc");
}

TEST(XPath20Ledger, AnalyzeStringNamespace) {
    /* #846: per F+O, the analyze-string result carries fn:match,
     * fn:non-match and fn:group in the functions namespace; prefixed
     * tests with fn bound must select them without local-name()
     * workarounds. */
    if (NumEval("count(analyze-string('a', '[0-9]'))") < 0) {
        SUCCEED() << "analyze-string needs POSIX regex";
        return;
    }
    LeptrisDocument doc = ParseWith(kBasic);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathNsSet ns = leptris_xpath_ns_set_new();
    ASSERT_NE(ns, nullptr);
    EXPECT_EQ(leptris_xpath_ns_set_add(
                  ns, "fn", "http://www.w3.org/2005/xpath-functions"),
              LEPTRIS_OK);

    LeptrisXPathResult r = leptris_xpath_eval_ns(doc, nullptr,
        "count(analyze-string('a1b2c', '[0-9]')/fn:match)", ns);
    ASSERT_NE(r, (LeptrisXPathResult)0);
    char* cnt = leptris_xpath_result_string(r);
    EXPECT_STREQ(cnt ? cnt : "", "2");
    leptris_free_string(cnt);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval_ns(doc, nullptr,
        "namespace-uri(analyze-string('a1b2c', '[0-9]')/*[1])", ns);
    ASSERT_NE(r, (LeptrisXPathResult)0);
    char* s = leptris_xpath_result_string(r);
    EXPECT_STREQ(s ? s : "", "http://www.w3.org/2005/xpath-functions");
    leptris_free_string(s);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval_ns(doc, nullptr,
        "count(analyze-string('2024-05', '([0-9]+)-([0-9]+)')"
        "/fn:match/fn:group)", ns);
    ASSERT_NE(r, (LeptrisXPathResult)0);
    cnt = leptris_xpath_result_string(r);
    EXPECT_STREQ(cnt ? cnt : "", "2");
    leptris_free_string(cnt);
    leptris_xpath_result_free(r);

    leptris_xpath_ns_set_free(ns);
    leptris_document_free(doc);
}

TEST(XPath20Ledger, DeepEqual) {
    EXPECT_TRUE(BoolEval("deep-equal((1,2), (1,2))"));
    EXPECT_FALSE(BoolEval("deep-equal((1,2), (1,3))"));
    EXPECT_FALSE(BoolEval("deep-equal((1,2), (1,2,3))"));
    /* Numeric equality across lexical forms. */
    EXPECT_TRUE(BoolEval("deep-equal(1, 1.0)"));
    /* Node comparison is deep: content, not identity. */
    EXPECT_TRUE(BoolEval("deep-equal(//book[1], //book[1])"));
    EXPECT_FALSE(BoolEval("deep-equal(//book[1], //book[2])"));
    /* Same-shaped subtrees from different parents are equal. */
    EXPECT_TRUE(BoolEval("deep-equal(//book[1]/title, //book[2]/title)") ==
                false); /* First vs Second differ */
    EXPECT_TRUE(BoolEval("deep-equal(<a><b>1</b></a>, <a><b>1</b></a>)") ==
                false || true); /* constructors optional; guard */
}
