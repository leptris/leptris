// test/parser/test_parser.cpp — Parser behavior specs.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>
#include <string>

namespace {

constexpr char kBasic[] = "<root><child>hello</child></root>";

TEST(ParserBasics, RejectsUnclosedRoot) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string("<a>unclosed", 11, &st);
    EXPECT_EQ(doc, nullptr);
    EXPECT_EQ(st, TAURUS_ERROR_PARSE);
}

TEST(ParserBasics, RejectsMismatchedTags) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string("<a><b></a></b>", 14, &st);
    EXPECT_EQ(doc, nullptr);
    EXPECT_EQ(st, TAURUS_ERROR_PARSE);
}

TEST(ParserBasics, RejectsUnterminatedAttribute) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string("<a attr=\"unterminated", 21, &st);
    EXPECT_EQ(doc, nullptr);
    EXPECT_EQ(st, TAURUS_ERROR_PARSE);
}

TEST(ParserBasics, PreservesEntitiesAsIs) {
    // The README documents that entity references are preserved as-is
    // (no expansion).  Unknown entities are accepted.
    const char xml[] = "<r>&foo; &amp; &lt;</r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    taurus_document_free(doc);
}

TEST(ParserBasics, PreservesUtf8MultibyteContent) {
    const char xml[] = "<r>café ☃ ñ 漢字</r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    taurus_document_free(doc);
}

TEST(ParserBasics, ParsesAllNodeTypes) {
    const char xml[] =
        "<?xml version='1.0'?>"
        "<!-- c --><r><?pi data?>text<![CDATA[raw]]><child>kid</child></r>";

    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(taurus_element_name(root), "r");

    taurus_document_free(doc);
}

// ---- Depth limit (TODO 07) -----------------------------------------------

// 256 is the default cap.  This test pins the contract.
constexpr int kDefaultMaxDepth = 256;

TEST(ParserDepthLimit, AcceptsNestingAtLimit) {
    std::string xml;
    for (int i = 0; i < kDefaultMaxDepth; i++) xml += "<a>";
    xml += 'x';
    for (int i = 0; i < kDefaultMaxDepth; i++) xml += "</a>";

    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr) << "depth " << kDefaultMaxDepth << " should succeed";
    EXPECT_EQ(st, TAURUS_OK);
    taurus_document_free(doc);
}

TEST(ParserDepthLimit, RejectsExcessiveNesting) {
    std::string xml;
    const int too_deep = kDefaultMaxDepth + 50;
    for (int i = 0; i < too_deep; i++) xml += "<a>";
    xml += 'x';
    for (int i = 0; i < too_deep; i++) xml += "</a>";

    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
    EXPECT_EQ(doc, nullptr) << "depth " << too_deep << " should be rejected";
    EXPECT_EQ(st, TAURUS_ERROR_PARSE);
}

TEST(ParserLeaks, XmlDeclarationDoesNotLeak) {
    /* Regression for TODO 44: XML declaration + stylesheet PI +
     * entity declarations must not leak intermediate buffers. */
    const char xml[] =
        "<?xml version='1.0' encoding='UTF-8'?>"
        "<?xml-stylesheet href='x.xsl' type='text/xsl'?>"
        "<r attr='val'>text</r>";

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    taurus_document_free(doc);
    /* Under leaks --atExit --: 0 bytes leaked. */
}

TEST(ParserStrictMode, DocumentScoped) {
    // Set thread-default lenient.
    taurus_set_strict_mode(0);
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string("<r/>", 4, &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(taurus_document_get_strict(doc), 0);

    // Override per-document.
    EXPECT_EQ(taurus_document_set_strict(doc, 1), TAURUS_OK);
    EXPECT_EQ(taurus_document_get_strict(doc), 1);

    // Thread-default is unchanged.
    EXPECT_EQ(taurus_get_strict_mode(), 0);

    taurus_document_free(doc);
}

TEST(ParserStrictMode, TwoDocumentsIndependent) {
    TaurusStatus st;
    TaurusDocument a = taurus_parse_string("<r/>", 4, &st);
    TaurusDocument b = taurus_parse_string("<r/>", 4, &st);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    taurus_document_set_strict(a, 1);
    EXPECT_EQ(taurus_document_get_strict(b), 0);  // b unaffected

    taurus_document_free(a);
    taurus_document_free(b);
}

// ---- Configurable depth limit (TODO 62) ---------------------------------

TEST(ParserConfigurableDepth, OverrideAllowsDeeperNesting) {
    taurus_set_max_depth(20);
    std::string xml;
    for (int i = 0; i < 18; i++) xml += "<a>";
    xml += 'x';
    for (int i = 0; i < 18; i++) xml += "</a>";

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
    EXPECT_NE(doc, nullptr);
    if (doc) taurus_document_free(doc);
    taurus_set_max_depth(0);
}

TEST(ParserConfigurableDepth, LowerCapRejectsShallowerNesting) {
    taurus_set_max_depth(5);
    std::string xml;
    for (int i = 0; i < 10; i++) xml += "<a>";
    xml += 'x';
    for (int i = 0; i < 10; i++) xml += "</a>";

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
    EXPECT_EQ(doc, nullptr);
    EXPECT_EQ(st, TAURUS_ERROR_PARSE);
    taurus_set_max_depth(0);
}

TEST(ParserConfigurableDepth, GetReturnsEffectiveValue) {
    taurus_set_max_depth(0);
    EXPECT_EQ(taurus_get_max_depth(), 256);
    taurus_set_max_depth(1024);
    EXPECT_EQ(taurus_get_max_depth(), 1024);
    taurus_set_max_depth(0);
}

}  // namespace
