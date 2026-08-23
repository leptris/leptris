// test/parser/test_parser.cpp — Parser behavior specs.

#include <gtest/gtest.h>

#include "leptris.h"
#include "leptris/error.h"

#include <cstring>
#include <string>

namespace {

constexpr char kBasic[] = "<root><child>hello</child></root>";

TEST(ParserBasics, RejectsUnclosedRoot) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string("<a>unclosed", 11, &st);
    EXPECT_EQ(doc, nullptr);
    EXPECT_EQ(st, LEPTRIS_ERROR_PARSE);
}

TEST(ParserBasics, RejectsMismatchedTags) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string("<a><b></a></b>", 14, &st);
    EXPECT_EQ(doc, nullptr);
    EXPECT_EQ(st, LEPTRIS_ERROR_PARSE);
}

TEST(ParserBasics, RejectsUnterminatedAttribute) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string("<a attr=\"unterminated", 21, &st);
    EXPECT_EQ(doc, nullptr);
    EXPECT_EQ(st, LEPTRIS_ERROR_PARSE);
}

TEST(ParserBasics, PreservesEntitiesAsIs) {
    // The README documents that entity references are preserved as-is
    // (no expansion).  Unknown entities are accepted.
    const char xml[] = "<r>&foo; &amp; &lt;</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    leptris_document_free(doc);
}

TEST(ParserBasics, PreservesUtf8MultibyteContent) {
    const char xml[] = "<r>café ☃ ñ 漢字</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    leptris_document_free(doc);
}

TEST(ParserBasics, ParsesAllNodeTypes) {
    const char xml[] =
        "<?xml version='1.0'?>"
        "<!-- c --><r><?pi data?>text<![CDATA[raw]]><child>kid</child></r>";

    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(leptris_element_name(root), "r");

    leptris_document_free(doc);
}

// ---- Depth limit (TODO 07) -----------------------------------------------

// 256 is the default cap.  This test pins the contract.
constexpr int kDefaultMaxDepth = 256;

TEST(ParserDepthLimit, AcceptsNestingAtLimit) {
    std::string xml;
    for (int i = 0; i < kDefaultMaxDepth; i++) xml += "<a>";
    xml += 'x';
    for (int i = 0; i < kDefaultMaxDepth; i++) xml += "</a>";

    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr) << "depth " << kDefaultMaxDepth << " should succeed";
    EXPECT_EQ(st, LEPTRIS_OK);
    leptris_document_free(doc);
}

TEST(ParserDepthLimit, RejectsExcessiveNesting) {
    std::string xml;
    const int too_deep = kDefaultMaxDepth + 50;
    for (int i = 0; i < too_deep; i++) xml += "<a>";
    xml += 'x';
    for (int i = 0; i < too_deep; i++) xml += "</a>";

    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml.data(), xml.size(), &st);
    EXPECT_EQ(doc, nullptr) << "depth " << too_deep << " should be rejected";
    EXPECT_EQ(st, LEPTRIS_ERROR_PARSE);
}

TEST(ParserLeaks, XmlDeclarationDoesNotLeak) {
    /* Regression for TODO 44: XML declaration + stylesheet PI +
     * entity declarations must not leak intermediate buffers. */
    const char xml[] =
        "<?xml version='1.0' encoding='UTF-8'?>"
        "<?xml-stylesheet href='x.xsl' type='text/xsl'?>"
        "<r attr='val'>text</r>";

    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    leptris_document_free(doc);
    /* Under leaks --atExit --: 0 bytes leaked. */
}

TEST(ParserStrictMode, DocumentScoped) {
    // Set thread-default lenient.
    leptris_set_strict_mode(0);
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string("<r/>", 4, &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(leptris_document_get_strict(doc), 0);

    // Override per-document.
    EXPECT_EQ(leptris_document_set_strict(doc, 1), LEPTRIS_OK);
    EXPECT_EQ(leptris_document_get_strict(doc), 1);

    // Thread-default is unchanged.
    EXPECT_EQ(leptris_get_strict_mode(), 0);

    leptris_document_free(doc);
}

TEST(ParserStrictMode, TwoDocumentsIndependent) {
    LeptrisStatus st;
    LeptrisDocument a = leptris_parse_string("<r/>", 4, &st);
    LeptrisDocument b = leptris_parse_string("<r/>", 4, &st);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    leptris_document_set_strict(a, 1);
    EXPECT_EQ(leptris_document_get_strict(b), 0);  // b unaffected

    leptris_document_free(a);
    leptris_document_free(b);
}

// ---- Configurable depth limit (TODO 62) ---------------------------------

TEST(ParserConfigurableDepth, OverrideAllowsDeeperNesting) {
    leptris_set_max_depth(20);
    std::string xml;
    for (int i = 0; i < 18; i++) xml += "<a>";
    xml += 'x';
    for (int i = 0; i < 18; i++) xml += "</a>";

    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml.data(), xml.size(), &st);
    EXPECT_NE(doc, nullptr);
    if (doc) leptris_document_free(doc);
    leptris_set_max_depth(0);
}

TEST(ParserConfigurableDepth, LowerCapRejectsShallowerNesting) {
    leptris_set_max_depth(5);
    std::string xml;
    for (int i = 0; i < 10; i++) xml += "<a>";
    xml += 'x';
    for (int i = 0; i < 10; i++) xml += "</a>";

    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml.data(), xml.size(), &st);
    EXPECT_EQ(doc, nullptr);
    EXPECT_EQ(st, LEPTRIS_ERROR_PARSE);
    leptris_set_max_depth(0);
}

TEST(ParserConfigurableDepth, GetReturnsEffectiveValue) {
    leptris_set_max_depth(0);
    EXPECT_EQ(leptris_get_max_depth(), 256);
    leptris_set_max_depth(1024);
    EXPECT_EQ(leptris_get_max_depth(), 1024);
    leptris_set_max_depth(0);
}

}  // namespace

TEST(ParseErrorPosition, ReportsLineAndColumn) {
    const char xml[] = "<a>\n  <b>\n</c>\n</a>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    EXPECT_EQ(doc, nullptr);

    int line = -1, column = -1;
    leptris_last_error_position(&line, &column);
    EXPECT_EQ(line, 3);
    EXPECT_EQ(column, 5);
    EXPECT_NE(leptris_last_error(), nullptr);

    /* NULL out-pointers are tolerated. */
    leptris_last_error_position(nullptr, nullptr);
}

TEST(ParseErrorPosition, EofFailureIsOnLastLine) {
    const char xml[] = "<r>\n<a/>\n</r";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    EXPECT_EQ(doc, nullptr);

    int line = -1, column = -1;
    leptris_last_error_position(&line, &column);
    EXPECT_EQ(line, 3);
    EXPECT_EQ(column, 4);
}
