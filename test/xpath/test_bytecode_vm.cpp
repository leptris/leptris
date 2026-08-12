// test/xpath/test_bytecode_vm.cpp — TODO 120 Phase A: bytecode + VM specs.
//
// The bytecode compiler + VM are linked into libtaurus but not yet
// wired into taurus_xpath_eval (AST evaluator is still the default).
// These specs verify the new files don't break existing XPath paths
// and that simple literal queries still work end-to-end via the
// public API.

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "taurus.h"

namespace {

TEST(XPathBytecode, NumberLiteralRoundTrips) {
    const char xml[] = "<r/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "1.5");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 1.5);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

TEST(XPathBytecode, StringLiteralRoundTrips) {
    const char xml[] = "<r/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "'hello'");
    ASSERT_NE(r, nullptr);
    char* s = taurus_xpath_result_string(r);
    EXPECT_STREQ(s, "hello");
    taurus_free_string(s);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

TEST(XPathBytecode, PathQueryStillWorks) {
    const char xml[] = "<root><a>1</a><a>2</a></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "count(//a)");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 2.0);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

// Specialized axis opcodes (TODO 126). Each must produce results
// identical to the AST-evaluator path. The compiler emits the
// specialized opcodes only when the step shape matches: no
// namespace prefix, no predicate, single name test or wildcard.

TEST(XPathBytecodeSpecializedAxes, ChildNameMatches) {
    const char xml[] =
        "<root><a>1</a><b>2</b><a>3</a></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* `child::a` and bare `a` both lower to BC_AXIS_CHILD_NAME. */
    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr, "count(//a)");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r1), 2.0);
    taurus_xpath_result_free(r1);

    /* Wildcard path uses BC_AXIS_CHILD_WILD. */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "count(//*)");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 4.0);  /* root + 3 children */
    taurus_xpath_result_free(r2);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSpecializedAxes, AttributeNameMatches) {
    const char xml[] =
        "<root a='1' b='2' c='3'/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* `attribute::a` and `@a` both lower to BC_AXIS_ATTRIBUTE_NAME. */
    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr, "count(/root/@a)");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r1), 1.0);
    taurus_xpath_result_free(r1);

    /* Attribute wildcard — BC_AXIS_ATTRIBUTE_WILD. */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "count(/root/@*)");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 3.0);
    taurus_xpath_result_free(r2);

    /* Attribute value via string(). */
    TaurusXPathResult r3 = taurus_xpath_eval(doc, nullptr, "string(/root/@b)");
    ASSERT_NE(r3, nullptr);
    char* s = taurus_xpath_result_string(r3);
    EXPECT_STREQ(s, "2");
    taurus_free_string(s);
    taurus_xpath_result_free(r3);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSpecializedAxes, SelfAndParentAxes) {
    const char xml[] =
        "<root><child><leaf/></child></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* self::* lowers to BC_AXIS_SELF_WILD. */
    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr,
                                                "count(/root/self::*)");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r1), 1.0);
    taurus_xpath_result_free(r1);

    /* parent::* lowers to BC_AXIS_PARENT_WILD. */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr,
                                                "count(//leaf/parent::*)");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 1.0);
    taurus_xpath_result_free(r2);

    /* Named self filters. */
    TaurusXPathResult r3 = taurus_xpath_eval(doc, nullptr,
                                                "count(/root/self::root)");
    ASSERT_NE(r3, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r3), 1.0);
    taurus_xpath_result_free(r3);

    TaurusXPathResult r4 = taurus_xpath_eval(doc, nullptr,
                                                "count(/root/self::other)");
    ASSERT_NE(r4, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r4), 0.0);
    taurus_xpath_result_free(r4);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSpecializedAxes, PredicateFallsBackToAxisStep) {
    /* Predicated steps stay on BC_AXIS_STEP — the inline handler
     * doesn't support predicates. Ensures the compiler's fast-path
     * gating doesn't accidentally inline a predicated step. */
    const char xml[] =
        "<root><a id='1'>one</a><a id='2'>two</a></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr,
                                               "count(//a[@id='2'])");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 1.0);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSpecializedAxes, NamespacePrefixFallsBack) {
    /* Steps with namespace prefix (`ns:name`) cannot use the inline
     * handler — they need xpath_context_resolve_prefix. The
     * compiler must fall back to BC_AXIS_STEP. */
    const char xml[] =
        "<root xmlns:ns='http://example.com'><ns:child/></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr,
                                               "count(//ns:child)");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 1.0);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSpecializedAxes, DescendantAxes) {
    const char xml[] =
        "<root><a><b><c/></b></a><a><b/></a></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr,
                                                "count(/root/descendant::*)");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r1), 5.0);
    taurus_xpath_result_free(r1);

    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr,
                                                "count(/root/descendant::b)");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 2.0);
    taurus_xpath_result_free(r2);

    TaurusXPathResult r3 = taurus_xpath_eval(doc, nullptr,
                                                "count(/root/descendant-or-self::*)");
    ASSERT_NE(r3, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r3), 6.0);
    taurus_xpath_result_free(r3);

    TaurusXPathResult r4 = taurus_xpath_eval(doc, nullptr,
                                                "count(//a/descendant-or-self::a)");
    ASSERT_NE(r4, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r4), 2.0);
    taurus_xpath_result_free(r4);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSpecializedAxes, DescendantFromMultiRoot) {
    /* Multi-input descendant must dedup correctly. The wildcard-all
     * path with a descendant step is tricky for the parser, so the
     * test uses an explicit root-relative path that's known to parse. */
    const char xml[] =
        "<root><a><x/></a><b><x/></b></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr,
                                               "count(/root/*/descendant::x)");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 2.0);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSimplePredicates, AttributeExistsPredicate) {
    /* [@attr] lowers to BC_PRED_ATTR_EXISTS. */
    const char xml[] =
        "<root><a id='1'>x</a><a>y</a><a id='2'>z</a></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr,
                                               "count(//a[@id])");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 2.0);
    taurus_xpath_result_free(r);

    /* Combined with descendant axis: BC_AXIS_DESCENDANT_NAME + BC_PRED_ATTR_EXISTS. */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr,
                                                "count(/root/descendant::a[@id])");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 2.0);
    taurus_xpath_result_free(r2);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSimplePredicates, AttributeEqualsStringPredicate) {
    /* [@attr = 'literal'] lowers to BC_PRED_ATTR_EQ_STRING. */
    const char xml[] =
        "<root><a t='x'>1</a><a t='y'>2</a><a t='x'>3</a></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr,
                                               "count(//a[@t='x'])");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 2.0);
    taurus_xpath_result_free(r);

    /* No match. */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr,
                                                "count(//a[@t='z'])");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 0.0);
    taurus_xpath_result_free(r2);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSimplePredicates, PositionPredicate) {
    /* [N] lowers to BC_PRED_POSITION. */
    const char xml[] =
        "<root><a>1</a><a>2</a><a>3</a><a>4</a></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr,
                                                "count(//a[1])");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r1), 1.0);
    taurus_xpath_result_free(r1);

    /* Out-of-bounds position. */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr,
                                                "count(//a[99])");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 0.0);
    taurus_xpath_result_free(r2);

    /* Combined with descendant: descendant::a[2] = second a in pre-order. */
    TaurusXPathResult r3 = taurus_xpath_eval(doc, nullptr,
                                                "string(/root/descendant::a[2])");
    ASSERT_NE(r3, nullptr);
    char* s = taurus_xpath_result_string(r3);
    EXPECT_STREQ(s, "2");
    taurus_free_string(s);
    taurus_xpath_result_free(r3);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSimplePredicates, ChainedPredicates) {
    /* Multiple simple predicates chain: [@a][@b] = has-attr-a AND has-attr-b. */
    const char xml[] =
        "<root><a x='1' y='2'>m</a><a x='1'>n</a><a y='2'>o</a></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr,
                                               "count(//a[@x][@y])");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 1.0);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSimplePredicates, ChildNumberComparePredicate) {
    /* [child::n OP num] lowers to BC_PRED_CHILD_NUM_CMP (TODO 159). */
    const char xml[] =
        "<root>"
        "  <book><price>10</price><title>A</title></book>"
        "  <book><price>30</price><title>B</title></book>"
        "  <book><price>35.50</price><title>C</title></book>"
        "  <book><price>40</price><title>D</title></book>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* GT: price > 30 -> 2 (35.50, 40). */
    TaurusXPathResult r_gt = taurus_xpath_eval(doc, nullptr,
                                                "count(//book[price > 30])");
    ASSERT_NE(r_gt, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r_gt), 2.0);
    taurus_xpath_result_free(r_gt);

    /* GTE: price >= 30 -> 3 (30, 35.50, 40). */
    TaurusXPathResult r_gte = taurus_xpath_eval(doc, nullptr,
                                                 "count(//book[price >= 30])");
    ASSERT_NE(r_gte, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r_gte), 3.0);
    taurus_xpath_result_free(r_gte);

    /* LT: price < 35 -> 2 (10, 30). */
    TaurusXPathResult r_lt = taurus_xpath_eval(doc, nullptr,
                                                "count(//book[price < 35])");
    ASSERT_NE(r_lt, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r_lt), 2.0);
    taurus_xpath_result_free(r_lt);

    /* EQ: price = 30 -> 1. */
    TaurusXPathResult r_eq = taurus_xpath_eval(doc, nullptr,
                                                "count(//book[price = 30])");
    ASSERT_NE(r_eq, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r_eq), 1.0);
    taurus_xpath_result_free(r_eq);

    /* NEQ: price != 30 -> 3. */
    TaurusXPathResult r_neq = taurus_xpath_eval(doc, nullptr,
                                                 "count(//book[price != 30])");
    ASSERT_NE(r_neq, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r_neq), 3.0);
    taurus_xpath_result_free(r_neq);

    /* Verify the title of the matched book (predicate + step). */
    TaurusXPathResult r_title = taurus_xpath_eval(doc, nullptr,
                                                   "string(//book[price > 39]/title)");
    ASSERT_NE(r_title, nullptr);
    char* s = taurus_xpath_result_string(r_title);
    EXPECT_STREQ(s, "D");
    taurus_free_string(s);
    taurus_xpath_result_free(r_title);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSimplePredicates, ChildNumberCompareNoChild) {
    /* If an element doesn't have the named child, predicate is false. */
    const char xml[] =
        "<root>"
        "  <a><price>40</price></a>"
        "  <a><other>40</other></a>"  /* no <price> child */
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr,
                                              "count(//a[price > 30])");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 1.0);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSimplePredicates, NumberChildComparePredicate) {
    /* [number(child::n) OP num] lowers to BC_PRED_CHILD_NUM_CMP (TODO 159 D2).
     * number() is semantically equivalent to reading text and parsing
     * via strtod, which is what the fused handler does. */
    const char xml[] =
        "<root>"
        "  <book><price>10</price></book>"
        "  <book><price>30</price></book>"
        "  <book><price>40</price></book>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* number(price) > 25 → 2 (30, 40). */
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr,
                                              "count(//book[number(price) > 25])");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 2.0);
    taurus_xpath_result_free(r);

    /* number(price) < 20 → 1 (10). */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr,
                                               "count(//book[number(price) < 20])");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 1.0);
    taurus_xpath_result_free(r2);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSimplePredicates, ComplexPredicateFallsBack) {
    /* Predicates with general expressions stay on the apply_predicates
     * path. Ensures the compiler's predicate classifier doesn't
     * accidentally inline a complex predicate. */
    const char xml[] =
        "<root><a id='1' p='10'>x</a><a id='2' p='20'>y</a></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* Comparison predicate — falls back. */
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr,
                                               "count(//a[@p > 15])");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 1.0);
    taurus_xpath_result_free(r);

    /* Function-call predicate — falls back. */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr,
                                                "count(//a[string-length() > 0])");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 2.0);
    taurus_xpath_result_free(r2);

    taurus_document_free(doc);
}

TEST(XPathBytecodeAbsolutePath, RootMatch) {
    const char xml[] = "<catalog><book/><book/></catalog>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr, "count(/catalog)");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r1), 1.0);
    taurus_xpath_result_free(r1);

    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "count(/library)");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 0.0);
    taurus_xpath_result_free(r2);

    TaurusXPathResult r3 = taurus_xpath_eval(doc, nullptr, "count(/*)");
    ASSERT_NE(r3, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r3), 1.0);
    taurus_xpath_result_free(r3);

    taurus_document_free(doc);
}

TEST(XPathBytecodeAbsolutePath, DescendantOrSelfFusion) {
    const char xml[] =
        "<lib><b><c>X</c></b><b><c>Y</c></b><d><b><c>Z</c></b></d></lib>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr, "count(//b)");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r1), 3.0);
    taurus_xpath_result_free(r1);

    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "count(//c)");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 3.0);
    taurus_xpath_result_free(r2);

    TaurusXPathResult r3 = taurus_xpath_eval(doc, nullptr, "count(//*)");
    ASSERT_NE(r3, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r3), 8.0);  /* lib + 3 b + 3 c + d */
    taurus_xpath_result_free(r3);

    taurus_document_free(doc);
}

TEST(XPathBytecodeAbsolutePath, PredicateAfterDescendantOrSelf) {
    const char xml[] =
        "<lib><b id='1'>x</b><b id='2'>y</b><b id='1'>z</b></lib>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr,
                                               "count(//b[@id='1'])");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 2.0);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

TEST(XPathBytecodeAbsolutePath, MultiStepAbsolute) {
    const char xml[] = "<root><a><b/></a><a><b/></a></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "count(/root/a)");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 2.0);
    taurus_xpath_result_free(r);

    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "count(/root/a/b)");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 2.0);
    taurus_xpath_result_free(r2);

    taurus_document_free(doc);
}

TEST(XPathBytecodeAbsolutePath, PositionPredicatePerContext) {
    /* `//title[1]` means "first title child of each context", NOT
     * "first title globally". The compiler must NOT fuse position
     * predicates (they're context-sensitive). */
    const char xml[] =
        "<lib><b><t>A</t><t>B</t></b><b><t>C</t></b></lib>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "count(//t[1])");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 2.0);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

TEST(XPathBytecodeInlineFunctions, Count) {
    /* count() lowers to BC_FUNC_COUNT. */
    const char xml[] = "<r><a/><a/><a/></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr, "count(//a)");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r1), 3.0);
    taurus_xpath_result_free(r1);

    /* Empty nodeset. */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "count(//missing)");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 0.0);
    taurus_xpath_result_free(r2);

    /* With predicate. */
    TaurusXPathResult r3 = taurus_xpath_eval(doc, nullptr,
                                                "count(/r/a[position() = 1])");
    ASSERT_NE(r3, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r3), 1.0);
    taurus_xpath_result_free(r3);

    taurus_document_free(doc);
}

TEST(XPathBytecodeInlineFunctions, BooleanFunctions) {
    const char xml[] = "<r/>";
    TaurusStatus st = TAURUS_OK;
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

    TaurusXPathResult r3 = taurus_xpath_eval(doc, nullptr, "not(false())");
    ASSERT_NE(r3, nullptr);
    EXPECT_EQ(taurus_xpath_result_boolean(r3), 1);
    taurus_xpath_result_free(r3);

    TaurusXPathResult r4 = taurus_xpath_eval(doc, nullptr, "boolean(/r)");
    ASSERT_NE(r4, nullptr);
    EXPECT_EQ(taurus_xpath_result_boolean(r4), 1);
    taurus_xpath_result_free(r4);

    taurus_document_free(doc);
}

TEST(XPathBytecodeInlineFunctions, StringAndNumber) {
    const char xml[] = "<r><a>42</a></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* string() with arg. */
    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr, "string(/r/a)");
    ASSERT_NE(r1, nullptr);
    char* s1 = taurus_xpath_result_string(r1);
    EXPECT_STREQ(s1, "42");
    taurus_free_string(s1);
    taurus_xpath_result_free(r1);

    /* number() converts string to number. */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "number(/r/a)");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 42.0);
    taurus_xpath_result_free(r2);

    /* number literal. */
    TaurusXPathResult r3 = taurus_xpath_eval(doc, nullptr, "number(3.14)");
    ASSERT_NE(r3, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r3), 3.14);
    taurus_xpath_result_free(r3);

    /* sum() of nodeset values. */
    TaurusXPathResult r4 = taurus_xpath_eval(doc, nullptr, "sum(/r/a)");
    ASSERT_NE(r4, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r4), 42.0);
    taurus_xpath_result_free(r4);

    taurus_document_free(doc);
}

TEST(XPathBytecodeInlineFunctions, NameFunctions) {
    /* name() / local-name() / namespace-uri() stay on BC_FUNC_CALL —
     * the QName construction (prefix + local) requires more plumbing
     * than the inline handler saves. These specs verify the fallback
     * path still produces correct results. */
    const char xml[] = "<r xmlns:ns='http://example.com'><ns:a id='x'/></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr, "name(/r/ns:a)");
    ASSERT_NE(r1, nullptr);
    char* s1 = taurus_xpath_result_string(r1);
    EXPECT_STREQ(s1, "ns:a");
    taurus_free_string(s1);
    taurus_xpath_result_free(r1);

    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "local-name(/r/ns:a)");
    ASSERT_NE(r2, nullptr);
    char* s2 = taurus_xpath_result_string(r2);
    EXPECT_STREQ(s2, "a");
    taurus_free_string(s2);
    taurus_xpath_result_free(r2);

    taurus_document_free(doc);
}

TEST(XPathBytecodeInlineFunctions, PositionLast) {
    const char xml[] = "<r><a/><a/><a/></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* position() inside predicate — but predicate falls back to AST eval,
     * so this tests the fallback path. Still: result must be correct. */
    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr,
                                                "count(/r/a[position() = last()])");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r1), 1.0);
    taurus_xpath_result_free(r1);

    /* last() as a top-level number (degenerate but valid). */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr,
                                                "count(/r/a[last()])");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 1.0);
    taurus_xpath_result_free(r2);

    taurus_document_free(doc);
}

TEST(XPathBytecodeFusedAxisPredicate, DescendantWildWithAttrExists) {
    /* descendant::*[@id] lowers to BC_AXIS_DESCENDANT_WILD_PRED_ATTR_EXISTS
     * when input is the document root (TODO 134). */
    const char xml[] =
        "<root><a id='1'>x</a><a>y</a><a id='2'>z</a><b id='3'>w</b></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusXPathResult r = taurus_xpath_eval(doc, root, "count(descendant::*[@id])");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 3.0);
    taurus_xpath_result_free(r);

    TaurusXPathResult r2 = taurus_xpath_eval(doc, root,
                                                "count(descendant::*[@id='1'])");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 1.0);
    taurus_xpath_result_free(r2);

    TaurusXPathResult r3 = taurus_xpath_eval(doc, root, "count(a[@id])");
    ASSERT_NE(r3, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r3), 2.0);
    taurus_xpath_result_free(r3);

    taurus_document_free(doc);
}

}  // namespace
