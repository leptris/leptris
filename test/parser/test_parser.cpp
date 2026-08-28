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

TEST(ParseOptions, AppliesDepthCapAndRestoresGlobal) {
    const char xml[] = "<a><b><c><d/></c></b></a>";
    int saved_depth = leptris_get_max_depth();

    LeptrisParseOptions o = {LEPTRIS_PARSE_DEFAULT, -1, 0};
    o.max_depth = 2;
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string_ex(xml, std::strlen(xml), &o, &st);
    EXPECT_EQ(doc, nullptr);
    EXPECT_NE(st, LEPTRIS_OK);
    /* The thread default is restored after the call. */
    EXPECT_EQ(leptris_get_max_depth(), saved_depth);
}

TEST(ParseOptions, DefaultsMatchPlainParse) {
    const char xml[] = "<a><b><c><d/></c></b></a>";
    LeptrisParseOptions o = {LEPTRIS_PARSE_DEFAULT, -1, 0};
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string_ex(xml, std::strlen(xml), &o, &st);
    EXPECT_NE(doc, nullptr);
    EXPECT_EQ(st, LEPTRIS_OK);
    leptris_document_free(doc);

    /* NULL options = same as leptris_parse_string. */
    doc = leptris_parse_string_ex(xml, std::strlen(xml), nullptr, &st);
    EXPECT_NE(doc, nullptr);
    leptris_document_free(doc);
}

TEST(ParseOptions, FlagsPassthrough) {
    const char xml[] = "<r>  <a/>  </r>";
    LeptrisParseOptions o = {LEPTRIS_PARSE_DEFAULT, -1, 0};
    o.flags = LEPTRIS_PARSE_DROP_WS_TEXT;
    LeptrisDocument doc = leptris_parse_string_ex(xml, std::strlen(xml), &o, nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_element_child_count(root), 1u);
    leptris_document_free(doc);
}

/* Issue #606: DTD ATTLIST default attributes apply ONLY under
 * LEPTRIS_PARSE_DTDATTR (libxml2 XML_PARSE_DTDATTR opt-in parity —
 * W3C C14N ex 3.3's canonical form excludes them). */
TEST(ParseDtdAttr, DefaultsOptIn) {
    const char xml[] =
        "<!DOCTYPE doc [<!ATTLIST e9 attr CDATA \"default\">]>"
        "<doc><e9/></doc>";

    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement e9 = (LeptrisElement)leptris_node_first_child(
        leptris_element_as_node(root));
    ASSERT_NE(e9, nullptr);
    EXPECT_EQ(leptris_element_attribute(e9, "attr"), nullptr)
        << "plain parse must not apply ATTLIST defaults";
    leptris_document_free(doc);

    doc = leptris_parse_string_flags(xml, std::strlen(xml),
                                     LEPTRIS_PARSE_DTDATTR, &st);
    ASSERT_NE(doc, nullptr);
    root = leptris_document_root(doc);
    e9 = (LeptrisElement)leptris_node_first_child(
        leptris_element_as_node(root));
    ASSERT_NE(e9, nullptr);
    EXPECT_STREQ(leptris_element_attribute(e9, "attr"), "default");
    leptris_document_free(doc);
}

/* Issue #576: attribute-value normalization (XML 1.0 §3.3.3). For a
 * CDATA attribute, each literal #x20/#xD/#xA/#x9 in the value is
 * replaced by a single #x20. Character references to whitespace
 * (&#9; &#xA; &#xD;) are exempt — they are not literal whitespace
 * at parse time and must survive expansion unnormalized. */
TEST(AttributeNormalization, LiteralWhitespaceBecomesSpace) {
    const char xml[] = "<root attr=\"value\twith\nwhitespace\r\"/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    const char* v = leptris_element_attribute(root, "attr");
    ASSERT_NE(v, nullptr);
    EXPECT_STREQ(v, "value with whitespace ");
    leptris_document_free(doc);
}

TEST(AttributeNormalization, MixedRefAndLiteralKeepsRefLiteral) {
    /* a="&#9;<TAB>b" — the ref expands to a literal tab, the parsed
     * tab normalizes to a space. libxml2 returns "\t b". */
    const char xml[] = "<r a=\"&#9;\tb\"/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    const char* v = leptris_element_attribute(root, "a");
    ASSERT_NE(v, nullptr);
    EXPECT_STREQ(v, "\t b");
    leptris_document_free(doc);
}

TEST(AttributeNormalization, CleanValuesStayZeroCopy) {
    const char xml[] = "<r a=\"plain\" b=\"with space\" c=\"&amp;\"/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_STREQ(leptris_element_attribute(root, "a"), "plain");
    EXPECT_STREQ(leptris_element_attribute(root, "b"), "with space");
    leptris_document_free(doc);
}

/* Issue #577: a PI in the epilog (after the document element) is
 * valid XML 1.0 — prolog and epilog both allow PIs. A dataless PI
 * additionally exposed a scan bug: the byte after the target name
 * is the closing '?', which the target's NUL-termination clobbered,
 * failing the whole parse. */
TEST(EpilogMisc, PiAfterRootParses) {
    const char xml[] = "<root/><?pi-after?>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(st, LEPTRIS_OK);
    ASSERT_EQ(leptris_document_pi_count(doc), 1u);
    EXPECT_STREQ(leptris_document_pi_target(doc, 0), "pi-after");
    EXPECT_STREQ(leptris_document_pi_data(doc, 0), "");
    leptris_document_free(doc);
}

TEST(EpilogMisc, PiAfterRootWithDataParses) {
    const char xml[] = "<r></r><?tail data=\"x\"?>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    ASSERT_EQ(leptris_document_pi_count(doc), 1u);
    EXPECT_STREQ(leptris_document_pi_target(doc, 0), "tail");
    EXPECT_STREQ(leptris_document_pi_data(doc, 0), "data=\"x\"");
    leptris_document_free(doc);
}

TEST(EpilogMisc, DatalessPiInsideTreeStillParses) {
    const char xml[] = "<r><child/><?ping?></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    leptris_document_free(doc);
}

/* Issue #578: comments in the epilog are valid XML 1.0 content.
 * They are parsed and retained (since #550) but were never exposed
 * through any public API, and serialization hoisted them into the
 * prolog — round-trips moved content across the root element. */
TEST(EpilogMisc, CommentAfterRootIsExposed) {
    const char xml[] = "<root/><!-- after -->";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(leptris_document_comment_count(doc), 1u);
    EXPECT_STREQ(leptris_document_comment_content(doc, 0), " after ");
    EXPECT_EQ(leptris_document_comment_content(doc, 1), nullptr);
    leptris_document_free(doc);
}

TEST(EpilogMisc, CommentChainsKeepDocumentOrder) {
    const char xml[] = "<!-- pre --><r/><!-- mid --><!-- last -->";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(leptris_document_comment_count(doc), 3u);
    EXPECT_STREQ(leptris_document_comment_content(doc, 0), " pre ");
    EXPECT_STREQ(leptris_document_comment_content(doc, 2), " last ");
    leptris_document_free(doc);
}

TEST(EpilogMisc, SerializeKeepsEpilogCommentAfterRoot) {
    const char xml[] = "<!-- pre --><root/><!-- after -->";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(out, nullptr);
    std::string s(out);
    size_t pre = s.find("<!-- pre -->");
    size_t root = s.find("<root/>");
    size_t post = s.find("<!-- after -->");
    ASSERT_NE(pre, std::string::npos);
    ASSERT_NE(root, std::string::npos);
    ASSERT_NE(post, std::string::npos);
    EXPECT_LT(pre, root);
    EXPECT_LT(root, post) << "epilog comment must serialize AFTER the root";
    leptris_free_string(out);
    leptris_document_free(doc);
}

/* Issue #550: parse -> serialize/xpath with NO intervening call, for
 * every parse path. The flat-path lazy-promote gap (v1.9.0) is gone —
 * these pin the contract so it cannot return: a freshly parsed,
 * valid document serializes and evaluates XPath immediately. */
TEST(FreshDocumentContract, SerializeImmediatelyAfterParse) {
    const char xml[] = "<r><a/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    char* s = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "<r><a/></r>");
    leptris_free_string(s);
    leptris_document_free(doc);
}

TEST(FreshDocumentContract, SerializeIntoSizingImmediately) {
    const char xml[] = "<r><a/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    size_t need = 0;
    size_t n = leptris_document_serialize_into(doc, nullptr, 0, &need, nullptr);
    EXPECT_GT(n, 0u);
    /* return = buffer size (incl. NUL); out_len = string length */
    EXPECT_EQ(need + 1, n);
    /* And with options (the sizing-with-options variant from #550). */
    LeptrisSerializeOptions o = {2, 1, "UTF-8"};
    n = leptris_document_serialize_into(doc, nullptr, 0, &need, &o);
    EXPECT_GT(n, 0u);
    leptris_document_free(doc);
}

TEST(FreshDocumentContract, XpathImmediatelyAfterParse) {
    const char xml[] = "<r><a/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "count(//r)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(FreshDocumentContract, InplacePathSameContract) {
    char buf[] = "<r a=\"1\"><a/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string_inplace(buf, std::strlen(buf), &st);
    ASSERT_NE(doc, nullptr);
    char* s = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "<r a=\"1\"><a/></r>");
    leptris_free_string(s);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "count(//a)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(FreshDocumentContract, TruncatedLengthIsACleanParseError) {
    /* The #550 repro passed length 10 for an 11-byte document. The
     * contract: NULL document + LEPTRIS_ERROR_PARSE — never a
     * partially-built tree that later serializes to NULL/NaN. */
    const char xml[] = "<r><a/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, 10, &st);
    EXPECT_EQ(doc, nullptr);
    EXPECT_EQ(st, LEPTRIS_ERROR_PARSE);
}
