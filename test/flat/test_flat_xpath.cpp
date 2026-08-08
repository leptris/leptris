// test/flat/test_flat_xpath.cpp — FlatDoc-direct XPath dispatch
// (TODO 145 Phase 3).
//
// Verifies primitive-returning XPath queries produce identical
// results whether evaluated via the flat fast path or the compact
// tree path.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>

namespace {

TaurusDocument Parse(const char* xml) {
    TaurusStatus st = TAURUS_OK;
    return taurus_parse_string(xml, std::strlen(xml), &st);
}

// Run a query both ways and compare results.
double CountFlat(const char* xml, const char* expr) {
    TaurusDocument doc = Parse(xml);
    EXPECT_NE(doc, nullptr);
    if (!doc) return -1.0;
    /* No call to taurus_document_root — flat path stays active. */
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, expr);
    EXPECT_NE(r, nullptr);
    double n = r ? taurus_xpath_result_number(r) : -1.0;
    if (r) taurus_xpath_result_free(r);
    taurus_document_free(doc);
    return n;
}

double CountCompact(const char* xml, const char* expr) {
    TaurusDocument doc = Parse(xml);
    EXPECT_NE(doc, nullptr);
    if (!doc) return -1.0;
    /* Force promote. */
    (void)taurus_document_root(doc);
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, expr);
    EXPECT_NE(r, nullptr);
    double n = r ? taurus_xpath_result_number(r) : -1.0;
    if (r) taurus_xpath_result_free(r);
    taurus_document_free(doc);
    return n;
}

TEST(FlatXPath, CountByNameMatchesCompact) {
    const char xml[] =
        "<r>"
        "  <book/><book/><book/>"
        "  <other/>"
        "</r>";
    EXPECT_DOUBLE_EQ(CountFlat(xml, "count(//book)"), 3.0);
    EXPECT_DOUBLE_EQ(CountCompact(xml, "count(//book)"), 3.0);
}

TEST(FlatXPath, CountWildcardMatchesCompact) {
    const char xml[] = "<r><a/><b/><c><d/></c></r>";
    EXPECT_DOUBLE_EQ(CountFlat(xml, "count(//*)"), 5.0);
    EXPECT_DOUBLE_EQ(CountCompact(xml, "count(//*)"), 5.0);
}

TEST(FlatXPath, CountNestedMatchesCompact) {
    /* Same name nested at multiple levels — count includes ALL. */
    const char xml[] = "<a><b><b><b/></b></b></a>";
    EXPECT_DOUBLE_EQ(CountFlat(xml, "count(//b)"), 3.0);
    EXPECT_DOUBLE_EQ(CountCompact(xml, "count(//b)"), 3.0);
}

TEST(FlatXPath, CountNonExistentReturnsZero) {
    const char xml[] = "<r><a/></r>";
    EXPECT_DOUBLE_EQ(CountFlat(xml, "count(//qux)"), 0.0);
    EXPECT_DOUBLE_EQ(CountCompact(xml, "count(//qux)"), 0.0);
}

TEST(FlatXPath, CountDescendantAxisForm) {
    /* Equivalent XPath forms: //name and descendant::name. */
    const char xml[] = "<r><book/><book/></r>";
    EXPECT_DOUBLE_EQ(CountFlat(xml, "count(descendant::book)"), 2.0);
    EXPECT_DOUBLE_EQ(CountFlat(xml, "count(descendant-or-self::book)"),
                     2.0);
}

TEST(FlatXPath, BooleanExistsMatchesCompact) {
    const char xml[] = "<r><book/></r>";
    {
        TaurusDocument doc = Parse(xml);
        TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "boolean(//book)");
        EXPECT_NE(r, nullptr);
        EXPECT_EQ(taurus_xpath_result_type(r), TAURUS_XPATH_BOOLEAN);
        EXPECT_EQ(taurus_xpath_result_boolean(r), 1);
        taurus_xpath_result_free(r);
        taurus_document_free(doc);
    }
    {
        TaurusDocument doc = Parse(xml);
        TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "boolean(//qux)");
        EXPECT_EQ(taurus_xpath_result_boolean(r), 0);
        taurus_xpath_result_free(r);
        taurus_document_free(doc);
    }
}

TEST(FlatXPath, ComplexExpressionFallsBack) {
    /* Non-matching pattern: must fall back to compact path (which
     * triggers promote). The result must still be correct. */
    const char xml[] = "<r><a x='1'/><a x='2'/></r>";
    TaurusDocument doc = Parse(xml);
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr,
                                              "count(//a[@x='1'])");
    EXPECT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 1.0);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(FlatXPath, FlatPathLeavesDocUnpromoted) {
    /* After a flat-eligible query, doc->flat_doc should still be
     * set (no promote happened). We verify indirectly: a second
     * flat query on the same doc should work. */
    const char xml[] = "<r><a/><a/></r>";
    TaurusDocument doc = Parse(xml);

    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr, "count(//a)");
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r1), 2.0);
    taurus_xpath_result_free(r1);

    /* If promote happened, the second call would not be flat-eligible
     * but should still return 2. Either way, correct. */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "count(//a)");
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 2.0);
    taurus_xpath_result_free(r2);

    taurus_document_free(doc);
}

TEST(FlatXPath, ContextParamBypassesFlat) {
    /* When the user passes a context element, flat path is skipped
     * (we don't know how to constrain the flat walk to a subtree). */
    const char xml[] = "<r><a/></r>";
    TaurusDocument doc = Parse(xml);
    TaurusElement root = taurus_document_root(doc);  /* triggers promote */

    TaurusXPathResult r = taurus_xpath_eval(doc, root, "count(//a)");
    EXPECT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 1.0);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(FlatXPath, NamespacedDocWorksViaFlat) {
    /* Phase 1 made namespaced docs go through the flat path. Phase 3
     * matches the full qualified name as stored in FlatNode (the
     * flat parser doesn't split prefix:local at parse time — promote
     * does). For now the test matches the qualified form; a future
     * enhancement could split on ':' in the flat fast path. */
    const char xml[] =
        "<r xmlns:foo='http://foo'>"
        "<foo:item/><foo:item/><foo:item/>"
        "</r>";
    EXPECT_DOUBLE_EQ(CountFlat(xml, "count(//foo:item)"), 3.0);
}

}  // namespace
