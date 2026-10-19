// test/html/test_html_parse_errors.cpp — the #1771 parse-lint
// surface: LEPTRIS_PARSE_COLLECT_HTML_ERRORS records the §13.2.5
// parse-error token stream (code + line + column) on the document's
// diagnostic list. Opt-in: without the bit the list stays empty and
// the tree construction is unchanged.
#include <gtest/gtest.h>
#include <leptris.h>
#include <leptris/html.h>
#include <leptris/error.h>
#include <cstring>

namespace {

struct Diag {
    LeptrisDiagKind kind;
    std::string code;
    std::string message;
    int line, col;
};

std::vector<Diag> drain(LeptrisDocument doc) {
    std::vector<Diag> out;
    size_t n = leptris_document_parse_diag_count(doc);
    for (size_t i = 0; i < n; i++) {
        LeptrisDiagKind kind = LEPTRIS_DIAG_INVALID;
        char msg[256] = {0};
        char code[64] = {0};
        int line = 0, col = 0;
        EXPECT_TRUE(leptris_document_parse_diag(doc, i, &kind, msg,
                                                sizeof(msg)));
        EXPECT_TRUE(leptris_document_parse_diag_code(doc, i, code,
                                                     sizeof(code),
                                                     &line, &col));
        out.push_back({kind, code, msg, line, col});
    }
    return out;
}

}  // namespace

TEST(HtmlParseErrors, OffByDefaultListStaysEmpty) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml = "<a id=\"1\" id=\"2\">x</a>";
    LeptrisDocument d = leptris_parse_html_string(xml, strlen(xml), &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_document_parse_diag_count(d), 0u);
    leptris_document_free(d);
}

TEST(HtmlParseErrors, DuplicateAttributeCarriesSpecCode) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml = "<a id=\"1\" id=\"2\">x</a>";
    LeptrisDocument d = leptris_parse_html_string_flags(
        xml, strlen(xml), LEPTRIS_PARSE_COLLECT_HTML_ERRORS, &st);
    ASSERT_NE(d, nullptr);
    auto diags = drain(d);
    ASSERT_GE(diags.size(), 1u);
    bool found = false;
    for (auto& dd : diags)
        if (dd.code == "duplicate-attribute") found = true;
    EXPECT_TRUE(found);
    EXPECT_EQ(diags[0].kind, LEPTRIS_DIAG_RECOVER);
    leptris_document_free(d);
}

TEST(HtmlParseErrors, EofInTagCarriesSpecCode) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml = "<p>text<a href=\"";
    LeptrisDocument d = leptris_parse_html_string_flags(
        xml, strlen(xml), LEPTRIS_PARSE_COLLECT_HTML_ERRORS, &st);
    ASSERT_NE(d, nullptr);
    auto diags = drain(d);
    ASSERT_GE(diags.size(), 1u);
    EXPECT_EQ(diags[0].code, "eof-in-tag");
    EXPECT_EQ(diags[0].line, 1);
    EXPECT_EQ(diags[0].col, 8);
    leptris_document_free(d);
}

TEST(HtmlParseErrors, DoctypeOutsideInitialIgnored) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml = "<div><!DOCTYPE html></div>";
    LeptrisDocument d = leptris_parse_html_string_flags(
        xml, strlen(xml), LEPTRIS_PARSE_COLLECT_HTML_ERRORS, &st);
    ASSERT_NE(d, nullptr);
    auto diags = drain(d);
    ASSERT_GE(diags.size(), 1u);
    EXPECT_EQ(diags[0].code, "unexpected-doctype");
    leptris_document_free(d);
}

TEST(HtmlParseErrors, LineColumnTrackRealLines) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml = "<p>one</p>\n<p>two</p>\n<a id=\"1\" id=\"2\">x</a>";
    LeptrisDocument d = leptris_parse_html_string_flags(
        xml, strlen(xml), LEPTRIS_PARSE_COLLECT_HTML_ERRORS, &st);
    ASSERT_NE(d, nullptr);
    auto diags = drain(d);
    ASSERT_GE(diags.size(), 1u);
    EXPECT_EQ(diags[0].line, 3);
    leptris_document_free(d);
}
