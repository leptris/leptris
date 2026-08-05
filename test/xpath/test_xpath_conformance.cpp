// test/xpath/test_xpath_conformance.cpp
//
// Exhaustive per-feature coverage of the XPath 1.0 surface that
// taurus exposes. This is the in-tree fallback for TODO 69 (the
// full W3C XPath 1.0 conformance suite is not committed; this file
// gives every function, axis, and operator at least one direct
// spec so regressions surface quickly).
//
// Each spec is small and self-contained (parse a tiny document,
// evaluate one expression, assert one outcome). Failures pinpoint
// the exact feature that regressed.
//
// Known engine gaps documented here (each has its own TODO.fix/ entry):
//   - //comment() and //processing-instruction() return 0 even when
//     comments/PIs exist in the tree (TODO 109: XPath over non-element
//     nodes doesn't traverse them).
//   - substring('hello', 0, 2) returns 'he' instead of 'h' (TODO 110:
//     XPath substring rounding edge cases).

#include <gtest/gtest.h>

#include "taurus.h"

#include <cmath>
#include <cstring>
#include <string>

namespace {

TaurusDocument Parse(const char* xml) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    EXPECT_NE(doc, nullptr);
    return doc;
}

double Num(TaurusDocument doc, const char* expr) {
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, expr);
    EXPECT_NE(r, nullptr);
    double v = r ? taurus_xpath_result_number(r) : NAN;
    if (r) taurus_xpath_result_free(r);
    return v;
}

int Bool(TaurusDocument doc, const char* expr) {
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, expr);
    EXPECT_NE(r, nullptr);
    int v = r ? taurus_xpath_result_boolean(r) : 0;
    if (r) taurus_xpath_result_free(r);
    return v;
}

std::string Str(TaurusDocument doc, const char* expr) {
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, expr);
    EXPECT_NE(r, nullptr);
    std::string out;
    if (r) {
        char* s = taurus_xpath_result_string(r);
        if (s) out = s;
        taurus_free_string(s);
        taurus_xpath_result_free(r);
    }
    return out;
}

size_t Count(TaurusDocument doc, const char* expr) {
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, expr);
    EXPECT_NE(r, nullptr);
    size_t n = r ? taurus_xpath_result_count(r) : 0u;
    if (r) taurus_xpath_result_free(r);
    return n;
}

constexpr char kCatalog[] =
    "<catalog>"
    "  <book id='b1'><title>Alpha</title><price>10</price></book>"
    "  <book id='b2'><title>Beta</title><price>20</price></book>"
    "  <book id='b3'><title>Gamma</title><price>30</price></book>"
    "</catalog>";

// kCatalog node counts (used throughout the suite):
//   catalog          1
//   book             3   (children of catalog)
//   title            3
//   price            3
//   total elements  10
// All-numbered expectations below are derived from this structure.

}  // namespace

// ============================================================================
// String functions
// ============================================================================

TEST(XPathConformanceString, ConcatJoinsAllArgs) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "concat('foo','bar','baz')"), "foobarbaz");
    EXPECT_EQ(Str(doc, "concat('a','b')"), "ab");
    EXPECT_EQ(Str(doc, "concat('', '')"), "");
    taurus_document_free(doc);
}

TEST(XPathConformanceString, StartsWithIsPrefixTest) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "starts-with('hello','he')"));
    EXPECT_TRUE(Bool(doc, "starts-with('hello','hello')"));
    EXPECT_FALSE(Bool(doc, "starts-with('hello','world')"));
    EXPECT_TRUE(Bool(doc, "starts-with('hello','')"));
    taurus_document_free(doc);
}

TEST(XPathConformanceString, ContainsIsSubstringTest) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "contains('hello','ell')"));
    EXPECT_TRUE(Bool(doc, "contains('hello','')"));
    EXPECT_FALSE(Bool(doc, "contains('hello','xyz')"));
    taurus_document_free(doc);
}

TEST(XPathConformanceString, SubstringIsOneIndexed) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "substring('hello',2)"), "ello");
    EXPECT_EQ(Str(doc, "substring('hello',2,3)"), "ell");
    EXPECT_EQ(Str(doc, "substring('hello',1,5)"), "hello");
    // Edge cases (negative start, fractional) — TODO 110 documents
    // rounding divergences from the spec; omitted here.
    taurus_document_free(doc);
}

TEST(XPathConformanceString, SubstringBeforeReturnsPrefix) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "substring-before('hello world',' ')"), "hello");
    EXPECT_EQ(Str(doc, "substring-before('hello','x')"), "");
    taurus_document_free(doc);
}

TEST(XPathConformanceString, SubstringAfterReturnsSuffix) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "substring-after('hello world',' ')"), "world");
    EXPECT_EQ(Str(doc, "substring-after('hello','x')"), "");
    taurus_document_free(doc);
}

TEST(XPathConformanceString, StringLengthCountsChars) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "string-length('hello')"), 5.0);
    EXPECT_DOUBLE_EQ(Num(doc, "string-length('')"), 0.0);
    EXPECT_DOUBLE_EQ(Num(doc, "string-length('a b c')"), 5.0);
    taurus_document_free(doc);
}

TEST(XPathConformanceString, NormalizeSpaceCollapsesRuns) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "normalize-space('  hello   world  ')"), "hello world");
    EXPECT_EQ(Str(doc, "normalize-space('single')"), "single");
    EXPECT_EQ(Str(doc, "normalize-space('   ')"), "");
    taurus_document_free(doc);
}

TEST(XPathConformanceString, TranslateSubstitutesChars) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "translate('abc','ab','AB')"), "ABc");
    EXPECT_EQ(Str(doc, "translate('abc','ab','')"), "c");
    EXPECT_EQ(Str(doc, "translate('foobar','fo','ba')"), "baabar");
    taurus_document_free(doc);
}

// ============================================================================
// Boolean functions
// ============================================================================

TEST(XPathConformanceBoolean, BooleanIsTrueForNonEmpty) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "boolean('x')"));
    EXPECT_FALSE(Bool(doc, "boolean('')"));
    EXPECT_TRUE(Bool(doc, "boolean(1)"));
    EXPECT_FALSE(Bool(doc, "boolean(0)"));
    EXPECT_TRUE(Bool(doc, "boolean(/catalog/book)"));
    taurus_document_free(doc);
}

TEST(XPathConformanceBoolean, NotInvertsValue) {
    auto doc = Parse(kCatalog);
    EXPECT_FALSE(Bool(doc, "not(true())"));
    EXPECT_TRUE(Bool(doc, "not(false())"));
    EXPECT_TRUE(Bool(doc, "not('')"));
    taurus_document_free(doc);
}

TEST(XPathConformanceBoolean, TrueAndFalseAreConstants) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "true()"));
    EXPECT_FALSE(Bool(doc, "false()"));
    taurus_document_free(doc);
}

TEST(XPathConformanceBoolean, LangMatchesLanguage) {
    auto doc = Parse("<r xml:lang='en'><a/></r>");
    EXPECT_TRUE(Bool(doc, "lang('en')"));
    taurus_document_free(doc);
}

// ============================================================================
// Number functions
// ============================================================================

TEST(XPathConformanceNumber, NumberParsesString) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "number('42.5')"), 42.5);
    EXPECT_DOUBLE_EQ(Num(doc, "number('0')"), 0.0);
    EXPECT_TRUE(std::isnan(Num(doc, "number('hello')")));
    taurus_document_free(doc);
}

TEST(XPathConformanceNumber, SumAggregatesNodeSetText) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "sum(//price)"), 60.0);  // 10 + 20 + 30
    taurus_document_free(doc);
}

TEST(XPathConformanceNumber, FloorRoundsDown) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "floor(3.7)"), 3.0);
    EXPECT_DOUBLE_EQ(Num(doc, "floor(-3.7)"), -4.0);
    EXPECT_DOUBLE_EQ(Num(doc, "floor(3.0)"), 3.0);
    taurus_document_free(doc);
}

TEST(XPathConformanceNumber, CeilingRoundsUp) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "ceiling(3.2)"), 4.0);
    EXPECT_DOUBLE_EQ(Num(doc, "ceiling(-3.2)"), -3.0);
    EXPECT_DOUBLE_EQ(Num(doc, "ceiling(3.0)"), 3.0);
    taurus_document_free(doc);
}

TEST(XPathConformanceNumber, RoundRoundsHalfToPositiveInfinity) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "round(3.5)"), 4.0);
    EXPECT_DOUBLE_EQ(Num(doc, "round(3.4)"), 3.0);
    EXPECT_DOUBLE_EQ(Num(doc, "round(-3.5)"), -3.0);
    taurus_document_free(doc);
}

// ============================================================================
// Node-set functions
// ============================================================================

TEST(XPathConformanceNodeSet, CountReturnsNodeSetSize) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "count(//book)"), 3.0);
    EXPECT_DOUBLE_EQ(Num(doc, "count(//*)"), 10.0);
    taurus_document_free(doc);
}

TEST(XPathConformanceNodeSet, LocalNameReturnsElementLocalName) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "local-name(/catalog/book[1])"), "book");
    EXPECT_EQ(Str(doc, "local-name(/catalog)"), "catalog");
    taurus_document_free(doc);
}

TEST(XPathConformanceNodeSet, NameReturnsQualifiedElementName) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "name(/catalog/book[1])"), "book");
    taurus_document_free(doc);
}

TEST(XPathConformanceNodeSet, NamespaceUriReturnsUriForNamespacedNode) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "namespace-uri(/catalog/book[1])"), "");
    taurus_document_free(doc);
}

TEST(XPathConformanceNodeSet, PositionReturnsContextPosition) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "position()"), 1.0);
    taurus_document_free(doc);
}

TEST(XPathConformanceNodeSet, LastReturnsContextSize) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "last()"), 1.0);
    taurus_document_free(doc);
}

TEST(XPathConformanceNodeSet, IdSelectsByIdAttribute) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "id('b1')"), 1u);
    EXPECT_EQ(Count(doc, "id('b1 b2')"), 2u);
    EXPECT_EQ(Count(doc, "id('nope')"), 0u);
    taurus_document_free(doc);
}

// ============================================================================
// All 13 axes
// ============================================================================

TEST(XPathConformanceAxes, ChildAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "/catalog/child::book"), 3u);
    EXPECT_EQ(Count(doc, "/catalog/book"), 3u);  // child is default
    taurus_document_free(doc);
}

TEST(XPathConformanceAxes, DescendantAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "/catalog/descendant::*"), 9u);  // 3 books + 3 titles + 3 prices
    taurus_document_free(doc);
}

TEST(XPathConformanceAxes, DescendantOrSelfAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "/catalog/descendant-or-self::*"), 10u);  // catalog + 9 descendants
    taurus_document_free(doc);
}

TEST(XPathConformanceAxes, ParentAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "//book/parent::*"), 1u);  // all books share parent — deduped
    EXPECT_EQ(Count(doc, "//title/parent::book"), 3u);
    taurus_document_free(doc);
}

TEST(XPathConformanceAxes, AncestorAxis) {
    auto doc = Parse(kCatalog);
    // Each title has 2 ancestors (book, catalog); dedup across titles = 4.
    EXPECT_EQ(Count(doc, "//title/ancestor::*"), 4u);
    taurus_document_free(doc);
}

TEST(XPathConformanceAxes, AncestorOrSelfAxis) {
    auto doc = Parse(kCatalog);
    // //title[1] selects 3 titles (each is the first child of its book).
    // Each contributes (title + book + catalog); deduped = 7.
    EXPECT_EQ(Count(doc, "//title[1]/ancestor-or-self::*"), 7u);
    // A single-title path collapses to title + book + catalog = 3.
    EXPECT_EQ(Count(doc, "/catalog/book[1]/title/ancestor-or-self::*"), 3u);
    taurus_document_free(doc);
}

TEST(XPathConformanceAxes, FollowingSiblingAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "//book[1]/following-sibling::*"), 2u);
    EXPECT_EQ(Count(doc, "//book[3]/following-sibling::*"), 0u);
    taurus_document_free(doc);
}

TEST(XPathConformanceAxes, PrecedingSiblingAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "//book[2]/preceding-sibling::*"), 1u);
    EXPECT_EQ(Count(doc, "//book[1]/preceding-sibling::*"), 0u);
    taurus_document_free(doc);
}

TEST(XPathConformanceAxes, FollowingAxis) {
    auto doc = Parse(kCatalog);
    // book[1] is followed by: book2 + title + price + book3 + title + price = 6.
    EXPECT_EQ(Count(doc, "//book[1]/following::*"), 6u);
    taurus_document_free(doc);
}

TEST(XPathConformanceAxes, PrecedingAxis) {
    auto doc = Parse(kCatalog);
    // book[3] is preceded by: book1 + title + price + book2 + title + price = 6.
    EXPECT_EQ(Count(doc, "//book[3]/preceding::*"), 6u);
    taurus_document_free(doc);
}

TEST(XPathConformanceAxes, SelfAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "/catalog/self::*"), 1u);
    EXPECT_EQ(Count(doc, "//book/self::*"), 3u);
    taurus_document_free(doc);
}

TEST(XPathConformanceAxes, AttributeAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "//book/@id"), 3u);
    EXPECT_EQ(Count(doc, "//book[1]/attribute::id"), 1u);
    taurus_document_free(doc);
}

TEST(XPathConformanceAxes, NamespaceAxisIncludesImplicitXml) {
    auto doc = Parse(kCatalog);
    // namespace:: axis always returns at least the implicit 'xml' binding.
    EXPECT_GE(Count(doc, "//book/namespace::*"), 1u);
    taurus_document_free(doc);
}

// ============================================================================
// Operators
// ============================================================================

TEST(XPathConformanceOperators, ArithmeticAddition) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "1 + 2"), 3.0);
    EXPECT_DOUBLE_EQ(Num(doc, "1.5 + 2.5"), 4.0);
    taurus_document_free(doc);
}

TEST(XPathConformanceOperators, ArithmeticSubtraction) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "10 - 3"), 7.0);
    EXPECT_DOUBLE_EQ(Num(doc, "10 - 3 - 2"), 5.0);  // left-associative
    taurus_document_free(doc);
}

TEST(XPathConformanceOperators, ArithmeticMultiplication) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "3 * 4"), 12.0);
    EXPECT_DOUBLE_EQ(Num(doc, "1 + 2 * 3"), 7.0);  // precedence
    EXPECT_DOUBLE_EQ(Num(doc, "(1 + 2) * 3"), 9.0);
    taurus_document_free(doc);
}

TEST(XPathConformanceOperators, ArithmeticDivision) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "10 div 2"), 5.0);
    EXPECT_NEAR(Num(doc, "10 div 3"), 3.33333, 0.0001);
    taurus_document_free(doc);
}

TEST(XPathConformanceOperators, ArithmeticModulo) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "10 mod 3"), 1.0);
    EXPECT_DOUBLE_EQ(Num(doc, "10 mod 4"), 2.0);
    taurus_document_free(doc);
}

TEST(XPathConformanceOperators, UnaryNegation) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "-5"), -5.0);
    EXPECT_DOUBLE_EQ(Num(doc, "-3 + 5"), 2.0);
    taurus_document_free(doc);
}

TEST(XPathConformanceOperators, Equality) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "1 = 1"));
    EXPECT_FALSE(Bool(doc, "1 = 2"));
    taurus_document_free(doc);
}

TEST(XPathConformanceOperators, Inequality) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "1 != 2"));
    EXPECT_FALSE(Bool(doc, "1 != 1"));
    taurus_document_free(doc);
}

TEST(XPathConformanceOperators, LessThan) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "1 < 2"));
    EXPECT_FALSE(Bool(doc, "2 < 1"));
    EXPECT_FALSE(Bool(doc, "2 < 2"));
    taurus_document_free(doc);
}

TEST(XPathConformanceOperators, LessOrEqual) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "2 <= 2"));
    EXPECT_TRUE(Bool(doc, "1 <= 2"));
    EXPECT_FALSE(Bool(doc, "3 <= 2"));
    taurus_document_free(doc);
}

TEST(XPathConformanceOperators, GreaterThan) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "3 > 2"));
    EXPECT_FALSE(Bool(doc, "2 > 3"));
    EXPECT_FALSE(Bool(doc, "2 > 2"));
    taurus_document_free(doc);
}

TEST(XPathConformanceOperators, GreaterOrEqual) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "2 >= 2"));
    EXPECT_TRUE(Bool(doc, "3 >= 2"));
    EXPECT_FALSE(Bool(doc, "1 >= 2"));
    taurus_document_free(doc);
}

TEST(XPathConformanceOperators, BooleanAnd) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "true() and true()"));
    EXPECT_FALSE(Bool(doc, "true() and false()"));
    EXPECT_FALSE(Bool(doc, "false() and false()"));
    taurus_document_free(doc);
}

TEST(XPathConformanceOperators, BooleanOr) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "true() or false()"));
    EXPECT_TRUE(Bool(doc, "false() or true()"));
    EXPECT_FALSE(Bool(doc, "false() or false()"));
    taurus_document_free(doc);
}

TEST(XPathConformanceOperators, UnionCombinesNodeSets) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "//book | //price"), 6u);
    EXPECT_EQ(Count(doc, "//book[1] | //book[1]"), 1u);  // deduped
    taurus_document_free(doc);
}

// ============================================================================
// Node tests
// ============================================================================
//
// Note: comment() and processing-instruction() node tests currently
// return empty results even when the relevant nodes exist in the
// tree — the XPath engine doesn't descend into non-element children.
// This is tracked as TODO 109. The specs below cover the node tests
// that DO work today; specs for comment/PI traversal live in TODO 109.

TEST(XPathConformanceNodeTest, TextMatchesTextNodes) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "//title/text()"), 3u);
    taurus_document_free(doc);
}
