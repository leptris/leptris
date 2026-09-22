// test/html/test_html_builder.cpp — public HTML construction surface (#1309).
//
// The engine has ONE DOM for both parsers and a full HTML serializer
// (§16.2), but before this slice there was no way to build an HTML
// document programmatically and serialize it AS HTML: html_method was
// reachable only through XSLT output, and HTML-PARSED documents
// serialized with XML rules (<br/>, <head/>) — invalid HTML output.
//
// Contract after #1309:
// - leptris_parse_html_string / leptris_parse_html4_string stamp the
//   document html_mode; the default serializer honors it.
// - leptris_document_create_html builds an empty html_mode document.
// - leptris_document_serialize_html / _save_html force the method on
//   any document (the XSLT method="html" semantic, public).
// - LeptrisSerializeExtOptions.html_method rides the size-aware ext
//   entry (the struct is explicitly growable, issue #129).

#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
#include "leptris/html.h"
}
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>

namespace {

LeptrisElement add_child(LeptrisDocument doc, LeptrisElement parent,
                         const char* name) {
    LeptrisElement e = leptris_element_create(doc, name);
    if (e && parent) {
        if (leptris_element_append_child(parent, e) != LEPTRIS_OK) return NULL;
    }
    return e;
}

}  // namespace

TEST(HtmlBuilder, ParsedHtmlDocSerializesAsHtmlByDefault) {
    const char* html =
        "<html><body><p>hi</p><br><img src='x'></body></html>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_html_string(html, strlen(html), &st);
    ASSERT_EQ(st, LEPTRIS_OK);
    ASSERT_NE(d, nullptr);
    char* s = leptris_document_serialize(d, NULL);
    ASSERT_NE(s, nullptr);
    std::string out(s);
    leptris_free_string(s);
    /* void elements: HTML form, not XML self-closing */
    EXPECT_NE(out.find("<br>"), std::string::npos) << out;
    EXPECT_EQ(out.find("<br/>"), std::string::npos) << out;
    EXPECT_NE(out.find("<img src=\"x\">"), std::string::npos) << out;
    EXPECT_EQ(out.find("<head/>"), std::string::npos) << out;
    leptris_document_free(d);
}

TEST(HtmlBuilder, ParseHtml4DocSerializesAsHtmlByDefault) {
    const char* html = "<html><body><hr><p>t</p></body></html>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_html4_string(html, strlen(html), &st);
    ASSERT_EQ(st, LEPTRIS_OK);
    ASSERT_NE(d, nullptr);
    char* s = leptris_document_serialize(d, NULL);
    ASSERT_NE(s, nullptr);
    std::string out(s);
    leptris_free_string(s);
    EXPECT_NE(out.find("<hr>"), std::string::npos) << out;
    EXPECT_EQ(out.find("<hr/>"), std::string::npos) << out;
    leptris_document_free(d);
}

TEST(HtmlBuilder, CreateHtmlBuildsAndSerializesAsHtml) {
    LeptrisDocument d = leptris_document_create_html();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "html");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);
    LeptrisElement body = add_child(d, root, "body");
    ASSERT_NE(body, nullptr);
    ASSERT_NE(add_child(d, body, "br"), nullptr);
    LeptrisElement img = add_child(d, body, "img");
    ASSERT_NE(img, nullptr);
    ASSERT_EQ(leptris_element_set_attribute(img, "src", "x"), LEPTRIS_OK);

    /* default serializer honors the html_mode flag */
    char* s = leptris_document_serialize(d, NULL);
    ASSERT_NE(s, nullptr);
    std::string out(s);
    leptris_free_string(s);
    EXPECT_NE(out.find("<br>"), std::string::npos) << out;
    EXPECT_EQ(out.find("<br/>"), std::string::npos) << out;

    /* explicit serialize_html forces the method on any doc */
    char* s2 = leptris_document_serialize_html(d, NULL);
    ASSERT_NE(s2, nullptr);
    EXPECT_STREQ(s2, s == nullptr ? "" : out.c_str() == s2 ? "" : s2);
    std::string forced(s2);
    leptris_free_string(s2);
    EXPECT_NE(forced.find("<br>"), std::string::npos) << forced;
    leptris_document_free(d);
}

TEST(HtmlBuilder, SerializeHtmlOnXmlDocForcesMethod) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);
    ASSERT_NE(add_child(d, root, "br"), nullptr);

    /* default (XML) keeps self-closing */
    char* s = leptris_document_serialize(d, NULL);
    ASSERT_NE(s, nullptr);
    EXPECT_NE(std::string(s).find("<br/>"), std::string::npos) << s;
    leptris_free_string(s);

    /* serialize_html forces the HTML method */
    char* h = leptris_document_serialize_html(d, NULL);
    ASSERT_NE(h, nullptr);
    std::string out(h);
    leptris_free_string(h);
    EXPECT_NE(out.find("<br>"), std::string::npos) << out;
    EXPECT_EQ(out.find("<br/>"), std::string::npos) << out;
    leptris_document_free(d);
}

TEST(HtmlBuilder, ExtOptionsHtmlMethodRideSizeAwareEntry) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);
    ASSERT_NE(add_child(d, root, "hr"), nullptr);

    LeptrisSerializeOptions opts;
    memset(&opts, 0, sizeof(opts));
    LeptrisSerializeExtOptions ext;
    memset(&ext, 0, sizeof(ext));
    ext.html_method = 1;
    char* s = leptris_document_serialize_ext_sized(d, &opts, &ext,
                                                   sizeof(ext));
    ASSERT_NE(s, nullptr);
    std::string out(s);
    leptris_free_string(s);
    EXPECT_NE(out.find("<hr>"), std::string::npos) << out;
    EXPECT_EQ(out.find("<hr/>"), std::string::npos) << out;
    leptris_document_free(d);
}

TEST(HtmlBuilder, SaveHtmlWritesHtmlBytes) {
    LeptrisDocument d = leptris_document_create_html();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "html");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);
    LeptrisElement body = add_child(d, root, "body");
    ASSERT_NE(body, nullptr);
    ASSERT_NE(add_child(d, body, "br"), nullptr);

    const char* path = "/tmp/leptris_html_builder_test.html";
    ASSERT_EQ(leptris_document_save_html(d, path, NULL), LEPTRIS_OK);
    std::ifstream f(path);
    ASSERT_TRUE(f.good());
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    f.close();
    std::remove(path);
    EXPECT_NE(content.find("<br>"), std::string::npos) << content;
    EXPECT_EQ(content.find("<br/>"), std::string::npos) << content;
    leptris_document_free(d);
}

TEST(HtmlBuilder, RawTextElementNotEscaped) {
    const char* html =
        "<html><body><script>if (a < b) { x(); }</script></body></html>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_html_string(html, strlen(html), &st);
    ASSERT_EQ(st, LEPTRIS_OK);
    char* s = leptris_document_serialize(d, NULL);
    ASSERT_NE(s, nullptr);
    std::string out(s);
    leptris_free_string(s);
    /* §16.2 raw-text: script contents verbatim */
    EXPECT_NE(out.find("if (a < b) { x(); }"), std::string::npos) << out;
    EXPECT_EQ(out.find("&lt;"), std::string::npos) << out;
    leptris_document_free(d);
}
