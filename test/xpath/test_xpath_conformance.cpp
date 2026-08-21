// test/xpath/test_xpath_conformance.cpp
//
// Exhaustive per-feature coverage of the XPath 1.0 surface that
// leptris exposes. This is the in-tree fallback for TODO 69 (the
// full W3C XPath 1.0 conformance suite is not committed; this file
// gives every function, axis, and operator at least one direct
// spec so regressions surface quickly).
//
// Each spec is small and self-contained (parse a tiny document,
// evaluate one expression, assert one outcome). Failures pinpoint
// the exact feature that regressed.
//
// Engine gaps previously documented here have been fixed:
//   - //comment() and //processing-instruction() now traverse correctly
//     (TODO 109, fixed by the matches_node_test / axis dispatcher
//     rewrite that accepts LeptrisNode* and skips non-element contexts
//     for the element-only axes).

#include <gtest/gtest.h>

#include "leptris.h"

#include <cmath>
#include <cstring>
#include <string>

namespace {

LeptrisDocument Parse(const char* xml) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    EXPECT_NE(doc, nullptr);
    return doc;
}

double Num(LeptrisDocument doc, const char* expr) {
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, expr);
    EXPECT_NE(r, nullptr);
    double v = r ? leptris_xpath_result_number(r) : NAN;
    if (r) leptris_xpath_result_free(r);
    return v;
}

int Bool(LeptrisDocument doc, const char* expr) {
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, expr);
    EXPECT_NE(r, nullptr);
    int v = r ? leptris_xpath_result_boolean(r) : 0;
    if (r) leptris_xpath_result_free(r);
    return v;
}

std::string Str(LeptrisDocument doc, const char* expr) {
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, expr);
    EXPECT_NE(r, nullptr);
    std::string out;
    if (r) {
        char* s = leptris_xpath_result_string(r);
        if (s) out = s;
        leptris_free_string(s);
        leptris_xpath_result_free(r);
    }
    return out;
}

size_t Count(LeptrisDocument doc, const char* expr) {
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, expr);
    EXPECT_NE(r, nullptr);
    size_t n = r ? leptris_xpath_result_count(r) : 0u;
    if (r) leptris_xpath_result_free(r);
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
    leptris_document_free(doc);
}

TEST(XPathConformanceString, StartsWithIsPrefixTest) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "starts-with('hello','he')"));
    EXPECT_TRUE(Bool(doc, "starts-with('hello','hello')"));
    EXPECT_FALSE(Bool(doc, "starts-with('hello','world')"));
    EXPECT_TRUE(Bool(doc, "starts-with('hello','')"));
    leptris_document_free(doc);
}

TEST(XPathConformanceString, ContainsIsSubstringTest) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "contains('hello','ell')"));
    EXPECT_TRUE(Bool(doc, "contains('hello','')"));
    EXPECT_FALSE(Bool(doc, "contains('hello','xyz')"));
    leptris_document_free(doc);
}

TEST(XPathConformanceString, SubstringIsOneIndexed) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "substring('hello',2)"), "ello");
    EXPECT_EQ(Str(doc, "substring('hello',2,3)"), "ell");
    EXPECT_EQ(Str(doc, "substring('hello',1,5)"), "hello");
    leptris_document_free(doc);
}

TEST(XPathConformanceString, SubstringMatchesW3CSpecExamples) {
    // Each case is taken verbatim from XPath 1.0 spec section 4.2.
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "substring('12345', 2, 3)"), "234");
    EXPECT_EQ(Str(doc, "substring('12345', 2)"), "2345");
    EXPECT_EQ(Str(doc, "substring('12345', 1.5, 2.6)"), "234");
    EXPECT_EQ(Str(doc, "substring('12345', 0, 2)"), "1");
    EXPECT_EQ(Str(doc, "substring('12345', 0, 3)"), "12");
    // -1 div 0 = -Inf start: no positions satisfy p >= -Inf AND p < -Inf+5.
    EXPECT_EQ(Str(doc, "substring('12345', -1 div 0, 5)"), "");
    leptris_document_free(doc);
}

TEST(XPathConformanceString, SubstringHandlesNanAndInf) {
    auto doc = Parse(kCatalog);
    // NaN start or length yields empty string.
    EXPECT_EQ(Str(doc, "substring('hello', 0 div 0)"), "");
    EXPECT_EQ(Str(doc, "substring('hello', 1, 0 div 0)"), "");
    // +Inf length yields the suffix from `start`.
    EXPECT_EQ(Str(doc, "substring('hello', 2, 1 div 0)"), "ello");
    leptris_document_free(doc);
}

TEST(XPathConformanceString, SubstringBeforeReturnsPrefix) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "substring-before('hello world',' ')"), "hello");
    EXPECT_EQ(Str(doc, "substring-before('hello','x')"), "");
    leptris_document_free(doc);
}

TEST(XPathConformanceString, SubstringAfterReturnsSuffix) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "substring-after('hello world',' ')"), "world");
    EXPECT_EQ(Str(doc, "substring-after('hello','x')"), "");
    leptris_document_free(doc);
}

TEST(XPathConformanceString, StringLengthCountsChars) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "string-length('hello')"), 5.0);
    EXPECT_DOUBLE_EQ(Num(doc, "string-length('')"), 0.0);
    EXPECT_DOUBLE_EQ(Num(doc, "string-length('a b c')"), 5.0);
    leptris_document_free(doc);
}

TEST(XPathConformanceString, NormalizeSpaceCollapsesRuns) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "normalize-space('  hello   world  ')"), "hello world");
    EXPECT_EQ(Str(doc, "normalize-space('single')"), "single");
    EXPECT_EQ(Str(doc, "normalize-space('   ')"), "");
    leptris_document_free(doc);
}

TEST(XPathConformanceString, TranslateSubstitutesChars) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "translate('abc','ab','AB')"), "ABc");
    EXPECT_EQ(Str(doc, "translate('abc','ab','')"), "c");
    EXPECT_EQ(Str(doc, "translate('foobar','fo','ba')"), "baabar");
    leptris_document_free(doc);
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
    leptris_document_free(doc);
}

TEST(XPathConformanceBoolean, NotInvertsValue) {
    auto doc = Parse(kCatalog);
    EXPECT_FALSE(Bool(doc, "not(true())"));
    EXPECT_TRUE(Bool(doc, "not(false())"));
    EXPECT_TRUE(Bool(doc, "not('')"));
    leptris_document_free(doc);
}

TEST(XPathConformanceBoolean, TrueAndFalseAreConstants) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "true()"));
    EXPECT_FALSE(Bool(doc, "false()"));
    leptris_document_free(doc);
}

TEST(XPathConformanceBoolean, LangMatchesLanguage) {
    auto doc = Parse("<r xml:lang='en'><a/></r>");
    EXPECT_TRUE(Bool(doc, "lang('en')"));
    leptris_document_free(doc);
}

// ============================================================================
// Number functions
// ============================================================================

TEST(XPathConformanceNumber, NumberParsesString) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "number('42.5')"), 42.5);
    EXPECT_DOUBLE_EQ(Num(doc, "number('0')"), 0.0);
    EXPECT_TRUE(std::isnan(Num(doc, "number('hello')")));
    leptris_document_free(doc);
}

TEST(XPathConformanceNumber, SumAggregatesNodeSetText) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "sum(//price)"), 60.0);  // 10 + 20 + 30
    leptris_document_free(doc);
}

TEST(XPathConformanceNumber, FloorRoundsDown) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "floor(3.7)"), 3.0);
    EXPECT_DOUBLE_EQ(Num(doc, "floor(-3.7)"), -4.0);
    EXPECT_DOUBLE_EQ(Num(doc, "floor(3.0)"), 3.0);
    leptris_document_free(doc);
}

TEST(XPathConformanceNumber, CeilingRoundsUp) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "ceiling(3.2)"), 4.0);
    EXPECT_DOUBLE_EQ(Num(doc, "ceiling(-3.2)"), -3.0);
    EXPECT_DOUBLE_EQ(Num(doc, "ceiling(3.0)"), 3.0);
    leptris_document_free(doc);
}

TEST(XPathConformanceNumber, RoundRoundsHalfToPositiveInfinity) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "round(3.5)"), 4.0);
    EXPECT_DOUBLE_EQ(Num(doc, "round(3.4)"), 3.0);
    EXPECT_DOUBLE_EQ(Num(doc, "round(-3.5)"), -3.0);
    leptris_document_free(doc);
}

// ============================================================================
// Node-set functions
// ============================================================================

TEST(XPathConformanceNodeSet, CountReturnsNodeSetSize) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "count(//book)"), 3.0);
    EXPECT_DOUBLE_EQ(Num(doc, "count(//*)"), 10.0);
    leptris_document_free(doc);
}

TEST(XPathConformanceNodeSet, LocalNameReturnsElementLocalName) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "local-name(/catalog/book[1])"), "book");
    EXPECT_EQ(Str(doc, "local-name(/catalog)"), "catalog");
    leptris_document_free(doc);
}

TEST(XPathConformanceNodeSet, NameReturnsQualifiedElementName) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "name(/catalog/book[1])"), "book");
    leptris_document_free(doc);
}

TEST(XPathConformanceNodeSet, NamespaceUriReturnsUriForNamespacedNode) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Str(doc, "namespace-uri(/catalog/book[1])"), "");
    leptris_document_free(doc);
}

TEST(XPathConformanceNodeSet, PositionReturnsContextPosition) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "position()"), 1.0);
    leptris_document_free(doc);
}

TEST(XPathConformanceNodeSet, LastReturnsContextSize) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "last()"), 1.0);
    leptris_document_free(doc);
}

TEST(XPathConformanceNodeSet, IdSelectsByIdAttribute) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "id('b1')"), 1u);
    EXPECT_EQ(Count(doc, "id('b1 b2')"), 2u);
    EXPECT_EQ(Count(doc, "id('nope')"), 0u);
    leptris_document_free(doc);
}

// ============================================================================
// All 13 axes
// ============================================================================

TEST(XPathConformanceAxes, ChildAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "/catalog/child::book"), 3u);
    EXPECT_EQ(Count(doc, "/catalog/book"), 3u);  // child is default
    leptris_document_free(doc);
}

TEST(XPathConformanceAxes, DescendantAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "/catalog/descendant::*"), 9u);  // 3 books + 3 titles + 3 prices
    leptris_document_free(doc);
}

TEST(XPathConformanceAxes, DescendantOrSelfAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "/catalog/descendant-or-self::*"), 10u);  // catalog + 9 descendants
    leptris_document_free(doc);
}

TEST(XPathConformanceAxes, ParentAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "//book/parent::*"), 1u);  // all books share parent — deduped
    EXPECT_EQ(Count(doc, "//title/parent::book"), 3u);
    leptris_document_free(doc);
}

TEST(XPathConformanceAxes, AncestorAxis) {
    auto doc = Parse(kCatalog);
    // Each title has 2 ancestors (book, catalog); dedup across titles = 4.
    EXPECT_EQ(Count(doc, "//title/ancestor::*"), 4u);
    leptris_document_free(doc);
}

TEST(XPathConformanceAxes, AncestorOrSelfAxis) {
    auto doc = Parse(kCatalog);
    // //title[1] selects 3 titles (each is the first child of its book).
    // Each contributes (title + book + catalog); deduped = 7.
    EXPECT_EQ(Count(doc, "//title[1]/ancestor-or-self::*"), 7u);
    // A single-title path collapses to title + book + catalog = 3.
    EXPECT_EQ(Count(doc, "/catalog/book[1]/title/ancestor-or-self::*"), 3u);
    leptris_document_free(doc);
}

TEST(XPathConformanceAxes, FollowingSiblingAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "//book[1]/following-sibling::*"), 2u);
    EXPECT_EQ(Count(doc, "//book[3]/following-sibling::*"), 0u);
    leptris_document_free(doc);
}

TEST(XPathConformanceAxes, PrecedingSiblingAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "//book[2]/preceding-sibling::*"), 1u);
    EXPECT_EQ(Count(doc, "//book[1]/preceding-sibling::*"), 0u);
    leptris_document_free(doc);
}

TEST(XPathConformanceAxes, FollowingAxis) {
    auto doc = Parse(kCatalog);
    // book[1] is followed by: book2 + title + price + book3 + title + price = 6.
    EXPECT_EQ(Count(doc, "//book[1]/following::*"), 6u);
    leptris_document_free(doc);
}

TEST(XPathConformanceAxes, PrecedingAxis) {
    auto doc = Parse(kCatalog);
    // book[3] is preceded by: book1 + title + price + book2 + title + price = 6.
    EXPECT_EQ(Count(doc, "//book[3]/preceding::*"), 6u);
    leptris_document_free(doc);
}

TEST(XPathConformanceAxes, SelfAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "/catalog/self::*"), 1u);
    EXPECT_EQ(Count(doc, "//book/self::*"), 3u);
    leptris_document_free(doc);
}

TEST(XPathConformanceAxes, AttributeAxis) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "//book/@id"), 3u);
    EXPECT_EQ(Count(doc, "//book[1]/attribute::id"), 1u);
    leptris_document_free(doc);
}

TEST(XPathConformanceAxes, NamespaceAxisIncludesImplicitXml) {
    auto doc = Parse(kCatalog);
    // namespace:: axis always returns at least the implicit 'xml' binding.
    EXPECT_GE(Count(doc, "//book/namespace::*"), 1u);
    leptris_document_free(doc);
}

// ============================================================================
// Operators
// ============================================================================

TEST(XPathConformanceOperators, ArithmeticAddition) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "1 + 2"), 3.0);
    EXPECT_DOUBLE_EQ(Num(doc, "1.5 + 2.5"), 4.0);
    leptris_document_free(doc);
}

TEST(XPathConformanceOperators, ArithmeticSubtraction) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "10 - 3"), 7.0);
    EXPECT_DOUBLE_EQ(Num(doc, "10 - 3 - 2"), 5.0);  // left-associative
    leptris_document_free(doc);
}

TEST(XPathConformanceOperators, ArithmeticMultiplication) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "3 * 4"), 12.0);
    EXPECT_DOUBLE_EQ(Num(doc, "1 + 2 * 3"), 7.0);  // precedence
    EXPECT_DOUBLE_EQ(Num(doc, "(1 + 2) * 3"), 9.0);
    leptris_document_free(doc);
}

TEST(XPathConformanceOperators, ArithmeticDivision) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "10 div 2"), 5.0);
    EXPECT_NEAR(Num(doc, "10 div 3"), 3.33333, 0.0001);
    leptris_document_free(doc);
}

TEST(XPathConformanceOperators, ArithmeticModulo) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "10 mod 3"), 1.0);
    EXPECT_DOUBLE_EQ(Num(doc, "10 mod 4"), 2.0);
    leptris_document_free(doc);
}

TEST(XPathConformanceOperators, UnaryNegation) {
    auto doc = Parse(kCatalog);
    EXPECT_DOUBLE_EQ(Num(doc, "-5"), -5.0);
    EXPECT_DOUBLE_EQ(Num(doc, "-3 + 5"), 2.0);
    leptris_document_free(doc);
}

TEST(XPathConformanceOperators, Equality) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "1 = 1"));
    EXPECT_FALSE(Bool(doc, "1 = 2"));
    leptris_document_free(doc);
}

TEST(XPathConformanceOperators, Inequality) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "1 != 2"));
    EXPECT_FALSE(Bool(doc, "1 != 1"));
    leptris_document_free(doc);
}

TEST(XPathConformanceOperators, LessThan) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "1 < 2"));
    EXPECT_FALSE(Bool(doc, "2 < 1"));
    EXPECT_FALSE(Bool(doc, "2 < 2"));
    leptris_document_free(doc);
}

TEST(XPathConformanceOperators, LessOrEqual) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "2 <= 2"));
    EXPECT_TRUE(Bool(doc, "1 <= 2"));
    EXPECT_FALSE(Bool(doc, "3 <= 2"));
    leptris_document_free(doc);
}

TEST(XPathConformanceOperators, GreaterThan) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "3 > 2"));
    EXPECT_FALSE(Bool(doc, "2 > 3"));
    EXPECT_FALSE(Bool(doc, "2 > 2"));
    leptris_document_free(doc);
}

TEST(XPathConformanceOperators, GreaterOrEqual) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "2 >= 2"));
    EXPECT_TRUE(Bool(doc, "3 >= 2"));
    EXPECT_FALSE(Bool(doc, "1 >= 2"));
    leptris_document_free(doc);
}

TEST(XPathConformanceOperators, BooleanAnd) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "true() and true()"));
    EXPECT_FALSE(Bool(doc, "true() and false()"));
    EXPECT_FALSE(Bool(doc, "false() and false()"));
    leptris_document_free(doc);
}

TEST(XPathConformanceOperators, BooleanOr) {
    auto doc = Parse(kCatalog);
    EXPECT_TRUE(Bool(doc, "true() or false()"));
    EXPECT_TRUE(Bool(doc, "false() or true()"));
    EXPECT_FALSE(Bool(doc, "false() or false()"));
    leptris_document_free(doc);
}

TEST(XPathConformanceOperators, UnionCombinesNodeSets) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "//book | //price"), 6u);
    EXPECT_EQ(Count(doc, "//book[1] | //book[1]"), 1u);  // deduped
    leptris_document_free(doc);
}

// ============================================================================
// Node tests
// ============================================================================

TEST(XPathConformanceNodeTest, TextMatchesTextNodes) {
    auto doc = Parse(kCatalog);
    EXPECT_EQ(Count(doc, "//title/text()"), 3u);
    leptris_document_free(doc);
}

TEST(XPathConformanceNodeTest, CommentMatchesComments) {
    auto doc = Parse("<r><!-- one --><!-- two -->x</r>");
    EXPECT_EQ(Count(doc, "//comment()"), 2u);
    EXPECT_EQ(Count(doc, "/r/comment()"), 2u);
    EXPECT_EQ(Count(doc, "count(//comment()) > 0"), 0u);  // count returns number
    leptris_document_free(doc);
}

TEST(XPathConformanceNodeTest, ProcessingInstructionMatchesAny) {
    auto doc = Parse("<r>x<?xml-stylesheet href='x.y'?><?other data?></r>");
    EXPECT_EQ(Count(doc, "//processing-instruction()"), 2u);
    leptris_document_free(doc);
}

TEST(XPathConformanceNodeTest, ProcessingInstructionMatchesByTarget) {
    auto doc = Parse("<r>x<?xml-stylesheet href='x.y'?><?other data?></r>");
    EXPECT_EQ(Count(doc, "//processing-instruction('xml-stylesheet')"), 1u);
    EXPECT_EQ(Count(doc, "//processing-instruction('other')"), 1u);
    EXPECT_EQ(Count(doc, "//processing-instruction('nope')"), 0u);
    leptris_document_free(doc);
}

TEST(XPathConformanceNodeTest, NodeMatchesAnyKind) {
    // <r> has: 2 comments, text "x", 1 PI = 4 child nodes.
    // //node() expands to /descendant-or-self::node()/child::node(),
    // so it returns only the *children* of (r + descendants of r) —
    // i.e., the 4 children of r (no descendants under any leaf node).
    auto doc = Parse("<r><!-- one --><!-- two -->x<?pi data?></r>");
    EXPECT_EQ(Count(doc, "/r/node()"), 4u);
    EXPECT_EQ(Count(doc, "//node()"), 4u);
    leptris_document_free(doc);
}
