/* TODO.xslt-full/14 — HTML parsing mode (#659), Nokogiri-parity
 * core. Each spec parses lenient HTML and pins the resulting DOM
 * (serialized). Reference behaviors: libxml2 HTMLparser as exposed
 * by Nokogiri (no implied <tbody>, no synthesized <html>/<body>,
 * stray end tags pop-until-matched or are ignored). */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
}
#include <cstring>
#include <string>

namespace {

std::string Html(const char* in) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_html_string(in, std::strlen(in), &st);
    if (!doc) return "(parse-failed)";
    char* out = leptris_document_serialize(doc, nullptr);
    std::string r = out ? out : "(null)";
    leptris_free_string(out);
    leptris_document_free(doc);
    /* Strip the declaration for shape comparisons. */
    const char* decl = "<?xml version=\"1.0\"?>";
    if (r.compare(0, std::strlen(decl), decl) == 0) {
        size_t rest = std::strlen(decl);
        if (rest < r.size() && r[rest] == '\n') rest++;
        r = r.substr(rest);
    }
    /* The parser synthesizes the Nokogiri document shape
     * <html><body>...</body></html> (no empty <head> — Nokogiri
     * omits it without head content); the specs below pin the BODY
     * content (the tolerant-parsing behaviors under test). */
    const char* open_w = "<html><body>";
    const char* close_w = "</body></html>";
    if (r.compare(0, std::strlen(open_w), open_w) == 0 &&
        r.size() >= std::strlen(open_w) + std::strlen(close_w) &&
        r.compare(r.size() - std::strlen(close_w), std::strlen(close_w),
                  close_w) == 0) {
        return r.substr(std::strlen(open_w),
                        r.size() - std::strlen(open_w) -
                            std::strlen(close_w));
    }
    return r;
}

TEST(HtmlParse, SynthesizesNokogiriDocumentShape) {
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<p>x</p>";
    LeptrisDocument doc = leptris_parse_html_string(in, std::strlen(in), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(leptris_element_name(root), "html");
    /* Nokogiri shape: html > body only (no empty head). */
    EXPECT_EQ(leptris_element_child_count(root), 1u);
    LeptrisElement body = (LeptrisElement)leptris_node_first_child(
        (LeptrisNodeRef)root);
    ASSERT_NE(body, nullptr);
    EXPECT_STREQ(leptris_element_name(body), "body");
    char* out = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_TRUE(std::strstr(out, "<html><body><p>x</p></body></html>") !=
                nullptr);
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(HtmlParse, ExplicitHtmlElementIsHonored) {
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<html><body><p>x</p></body></html>";
    LeptrisDocument doc = leptris_parse_html_string(in, std::strlen(in), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(leptris_element_name(root), "html");
    EXPECT_EQ(leptris_element_child_count(root), 1u);
    leptris_document_free(doc);
}

TEST(HtmlParse, VoidElementsNeverNest) {
    EXPECT_EQ(Html("<br><img src='x.png'><hr>"),
              "<br/><img src=\"x.png\"/><hr/>");
    /* A following element is a SIBLING of the void element. */
    EXPECT_EQ(Html("<br><p>hi</p>"), "<br/><p>hi</p>");
}

TEST(HtmlParse, ImpliedEndTagsForListsAndCells) {
    EXPECT_EQ(Html("<ul><li>one<li>two</ul>"),
              "<ul><li>one</li><li>two</li></ul>");
    EXPECT_EQ(Html("<table><tr><td>a<td>b<tr><td>c</table>"),
              "<table><tr><td>a</td><td>b</td></tr>"
              "<tr><td>c</td></tr></table>");
    /* <p> closes on block-level starts. */
    EXPECT_EQ(Html("<p>one<p>two"),
              "<p>one</p><p>two</p>");
    EXPECT_EQ(Html("<p>text<div>block</div>"),
              "<p>text</p><div>block</div>");
    EXPECT_EQ(Html("<select><option>a<option>b</select>"),
              "<select><option>a</option><option>b</option></select>");
    EXPECT_EQ(Html("<dl><dt>t<dd>d</dl>"),
              "<dl><dt>t</dt><dd>d</dd></dl>");
}

TEST(HtmlParse, NamesAndAttributesLowercased) {
    EXPECT_EQ(Html("<DIV CLASS='Big'>x</DIV>"),
              "<div class=\"Big\">x</div>");
}

TEST(HtmlParse, MinimizedAndUnquotedAttributes) {
    /* Boolean attribute: value is the EMPTY string (the
     * html5lib/Nokogiri DOM stores checked=""). */
    EXPECT_EQ(Html("<input type=checkbox checked>"),
              "<input type=\"checkbox\" checked=\"\"/>");
    /* Unquoted values end at whitespace (HTML5 §13.2.5.43): the
     * remainder is a NEW minimized attribute, exactly as libxml2. */
    EXPECT_EQ(Html("<a href=/x>y</a>"),
              "<a href=\"/x\">y</a>");
    EXPECT_EQ(Html("<a href=/x y.html>t</a>"),
              "<a href=\"/x\" y.html=\"\">t</a>");
}

TEST(HtmlParse, ScriptAndStyleAreRawText) {
    /* Raw text: the DOM text keeps < and > verbatim (assert on the
     * text content — XML-method serialization escaping is
     * orthogonal; the html method emits it raw). */
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<script>if (a < b) { x(); }</script>"
                      "<style>p > b { color: red }</style>";
    LeptrisDocument doc = leptris_parse_html_string(in, std::strlen(in), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//script");
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(leptris_xpath_result_count(r), 1u);
    LeptrisElement sc = (LeptrisElement)leptris_xpath_result_get(r, 0);
    ASSERT_NE(sc, nullptr);
    EXPECT_STREQ(leptris_element_text(sc), "if (a < b) { x(); }");
    leptris_xpath_result_free(r);
    LeptrisXPathResult r2 = leptris_xpath_eval(doc, nullptr, "//style");
    ASSERT_NE(r2, nullptr);
    LeptrisElement stl = (LeptrisElement)leptris_xpath_result_get(r2, 0);
    ASSERT_NE(stl, nullptr);
    EXPECT_STREQ(leptris_element_text(stl), "p > b { color: red }");
    leptris_xpath_result_free(r2);
    leptris_document_free(doc);
    /* Uppercase close still ends raw text. */
    EXPECT_EQ(Html("<script>1<2</SCRIPT>after"),
              "<script>1&lt;2</script>after");
}

TEST(HtmlParse, EntitiesDecodeInTextAndValues) {
    /* &nbsp; &amp; &copy; numeric — text and attribute values. */
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<p title='a &amp; b'>x &nbsp;&copy; &#65;</p>";
    LeptrisDocument doc = leptris_parse_html_string(in, std::strlen(in), &st);
    ASSERT_NE(doc, nullptr);
    /* The wrapper puts <p> under html/body; XPath reaches it. */
    LeptrisXPathResult pr = leptris_xpath_eval(doc, nullptr, "//p");
    ASSERT_NE(pr, nullptr);
    ASSERT_EQ(leptris_xpath_result_count(pr), 1u);
    LeptrisElement root = (LeptrisElement)leptris_xpath_result_get(pr, 0);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(leptris_element_name(root), "p");
    EXPECT_STREQ(leptris_element_attribute(root, "title"), "a & b");
    leptris_xpath_result_free(pr);
    /* nbsp serializes as the raw UTF-8 byte; copy as U+00A9. */
    char* out = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_TRUE(std::strstr(out, "x \xC2\xA0\xC2\xA9 A") != nullptr);
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(HtmlParse, CommentsAndDoctypeSurvive) {
    EXPECT_EQ(Html("<!-- note --><p>x</p>"),
              "<!-- note --><p>x</p>");
    /* Legacy doctype strings are accepted verbatim. */
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] =
        "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" "
        "\"http://www.w3.org/TR/html4/strict.dtd\"><html/>";
    LeptrisDocument doc = leptris_parse_html_string(in, std::strlen(in), &st);
    EXPECT_NE(doc, nullptr);   /* never a hard failure */
    if (doc) leptris_document_free(doc);
}

TEST(HtmlParse, StrayEndTagsAreIgnoredOrPop) {
    EXPECT_EQ(Html("<b><i>x</b></i>"), "<b><i>x</i></b>");
    EXPECT_EQ(Html("</p>x"), "x");
    EXPECT_EQ(Html("<ul><li>a</ul></li>"), "<ul><li>a</li></ul>");
}

TEST(HtmlParse, UnclosedElementsCloseAtEof) {
    EXPECT_EQ(Html("<div><span>x"), "<div><span>x</span></div>");
}

TEST(HtmlParse, NakedTextAndLtInTextSurvive) {
    EXPECT_EQ(Html("a < b & c"), "a &lt; b &amp; c");
}

TEST(HtmlParse, CaseInsensitiveCloseMatches) {
    EXPECT_EQ(Html("<P>x</p>"), "<p>x</p>");
}

}  // namespace


/* #659 slice 2: processing-instruction-ish bogus constructs.
 * libxml2/Nokogiri ground truth: <?target data?> becomes a PI
 * node whose data INCLUDES the trailing '?'; <!...> non-comment
 * (DOCTYPE, <![CDATA[) is dropped to first '>' and does not
 * perturb the text. */
TEST(HtmlParse, ProcessingInstructionAndBogus) {
    LeptrisDocument d = leptris_parse_html_string(
        "<div>a<?foo bar?>b</div>",
        strlen("<div>a<?foo bar?>b</div>"), NULL);
    ASSERT_NE(d, nullptr);
    LeptrisElement div = leptris_document_root(d) ? nullptr : nullptr;
    LeptrisElement root = leptris_document_root(d);
    ASSERT_NE(root, nullptr);
    LeptrisElement body = leptris_element_first_child_any(root);
    ASSERT_NE(body, nullptr);
    LeptrisElement dv = leptris_element_first_child_any(body);
    ASSERT_NE(dv, nullptr);
    /* children: text "a", PI(foo, "bar?"), text "b" */
    LeptrisNodeRef c1 = leptris_node_first_child(
        leptris_element_as_node(dv));
    ASSERT_NE(c1, nullptr);
    EXPECT_EQ(leptris_node_get_type(c1), LEPTRIS_NODE_TYPE_TEXT);
    LeptrisNodeRef c2 = leptris_node_next_sibling(c1);
    ASSERT_NE(c2, nullptr);
    EXPECT_EQ(leptris_node_get_type(c2), LEPTRIS_NODE_TYPE_PI);
    const char* nm = leptris_pi_node_get_target(c2);
    EXPECT_STREQ(nm ? nm : "", "foo");
    const char* pd = leptris_pi_node_get_data(c2);
    EXPECT_STREQ(pd ? pd : "", "bar?");
    LeptrisNodeRef c3 = leptris_node_next_sibling(c2);
    ASSERT_NE(c3, nullptr);
    EXPECT_EQ(leptris_node_get_type(c3), LEPTRIS_NODE_TYPE_TEXT);
    leptris_document_free(d);

    /* bogus <!...> drops to '>' without touching the text */
    LeptrisDocument d2 = leptris_parse_html_string(
        "<p>a<![CDATA[x]]>b</p>",
        strlen("<p>a<![CDATA[x]]>b</p>"), NULL);
    ASSERT_NE(d2, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(
        d2, NULL, "string(//p)");
    ASSERT_NE(r, nullptr);
    char* sv = leptris_xpath_result_string(r);
    EXPECT_STREQ(sv ? sv : "", "ab");
    leptris_free_string(sv);
    leptris_xpath_result_free(r);
    leptris_document_free(d2);
}
