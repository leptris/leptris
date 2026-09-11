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
    /* WHATWG shape is <html><head/><body>...</body></html> (head
     * always present, possibly empty) — strip the EMPTY head with
     * the wrapper so content specs stay shape-agnostic. Non-empty
     * heads (HeadContentLift) keep the full form. */
    const char* open_wh = "<html><head/><body>";
    const char* open_w = "<html><body>";
    const char* close_w = "</body></html>";
    /* Document-level prolog comments (WHATWG initial mode) ride
     * ahead of the wrapper — keep them, strip around them. */
    size_t pl = 0;
    while (r.compare(pl, 4, "<!--") == 0) {
        size_t e = r.find("-->", pl + 4);
        if (e == std::string::npos) break;
        pl = e + 3;
    }
    for (const char* ow : {open_wh, open_w}) {
        if (r.compare(pl, std::strlen(ow), ow) == 0 &&
            r.size() >= pl + std::strlen(ow) + std::strlen(close_w) &&
            r.compare(r.size() - std::strlen(close_w),
                      std::strlen(close_w), close_w) == 0) {
            return r.substr(0, pl) +
                   r.substr(pl + std::strlen(ow),
                            r.size() - pl - std::strlen(ow) -
                                std::strlen(close_w));
        }
    }
    return r;
}
std::string Html4(const char* in) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_html4_string(in, std::strlen(in), &st);
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


/* #659 (html5lib corpus fallout): inputs that append NOTHING
 * (stray end tag only, a doctype-only document, a second doctype,
 * an empty string) must still parse to the empty Nokogiri shape —
 * "nothing parsed" is not an error in lenient HTML mode. */
TEST(HtmlParse, EmptyShapeInputsAreDocuments) {
    const char* cases[] = {
        "", "</menuitem>", "</b>", "<!DOCTYPE html><!DOCTYPE html>",
        "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\n"
        "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">",
    };
    for (const char* in : cases) {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument doc =
            leptris_parse_html_string(in, std::strlen(in), &st);
        EXPECT_NE(doc, nullptr) << "input: " << in;
        if (!doc) continue;
        LeptrisElement root = leptris_document_root(doc);
        ASSERT_NE(root, nullptr);
        EXPECT_STREQ(leptris_element_name(root), "html");
        leptris_document_free(doc);
    }
}

TEST(HtmlParse, SynthesizesNokogiriDocumentShape) {
    /* Nokogiri/libxml2 shape is the html4 entry's contract; the
     * WHATWG entry always has a head (StructuralHeadBodyTags). */
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<p>x</p>";
    LeptrisDocument doc = leptris_parse_html4_string(in, std::strlen(in), &st);
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

TEST(HtmlParse, LeadingCommentsBelongToTheDocument) {
    /* WHATWG initial/before-html: a comment token inserts as a
     * child of the DOCUMENT, before the (implied) <html> element
     * (html5lib tests6/ html5test-com ground truth). Text first
     * starts body content — later comments stay in the flow. The
     * html4 entry keeps the libxml2/Nokogiri shape (comment rides
     * inside the synthesized body). */
    LeptrisStatus st = LEPTRIS_OK;

    const char w1[] = "<!-- lead --><p>hi</p>";
    LeptrisDocument d1 = leptris_parse_html_string(w1, std::strlen(w1), &st);
    ASSERT_NE(d1, nullptr);
    char* o1 = leptris_document_serialize(d1, nullptr);
    ASSERT_NE(o1, nullptr);
    EXPECT_STREQ(o1,
        "<!-- lead --><html><head/><body><p>hi</p></body></html>");
    leptris_free_string(o1);
    leptris_document_free(d1);

    const char w2[] = "<!-- a --><!-- b --><p>x</p>";
    LeptrisDocument d2 = leptris_parse_html_string(w2, std::strlen(w2), &st);
    ASSERT_NE(d2, nullptr);
    char* o2 = leptris_document_serialize(d2, nullptr);
    ASSERT_NE(o2, nullptr);
    EXPECT_STREQ(o2,
        "<!-- a --><!-- b --><html><head/><body><p>x</p></body>"
        "</html>");
    leptris_free_string(o2);
    leptris_document_free(d2);

    /* Non-whitespace text opens body content; the comment that
     * follows it is in-body. */
    const char w3[] = "hi<!-- c -->";
    LeptrisDocument d3 = leptris_parse_html_string(w3, std::strlen(w3), &st);
    ASSERT_NE(d3, nullptr);
    char* o3 = leptris_document_serialize(d3, nullptr);
    ASSERT_NE(o3, nullptr);
    EXPECT_STREQ(o3, "<html><head/><body>hi<!-- c --></body></html>");
    leptris_free_string(o3);
    leptris_document_free(d3);

    const char n1[] = "<!-- lead --><p>hi</p>";
    LeptrisDocument d4 = leptris_parse_html4_string(n1, std::strlen(n1), &st);
    ASSERT_NE(d4, nullptr);
    char* o4 = leptris_document_serialize(d4, nullptr);
    ASSERT_NE(o4, nullptr);
    EXPECT_STREQ(o4, "<html><body><!-- lead --><p>hi</p></body></html>");
    leptris_free_string(o4);
    leptris_document_free(d4);
}

TEST(HtmlParse, ExplicitHtmlElementIsHonored) {
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<html><body><p>x</p></body></html>";
    LeptrisDocument doc = leptris_parse_html4_string(in, std::strlen(in), &st);
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
    /* WHATWG implies tbody; the html4 entry keeps it bare. */
    EXPECT_EQ(Html("<table><tr><td>a<td>b<tr><td>c</table>"),
              "<table><tbody><tr><td>a</td><td>b</td></tr>"
              "<tr><td>c</td></tr></tbody></table>");
    EXPECT_EQ(Html4("<table><tr><td>a<td>b<tr><td>c</table>"),
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
    LeptrisDocument doc = leptris_parse_html4_string(in, std::strlen(in), &st);
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
    /* Uppercase close still ends raw text (html4 entry: the
     * leading-script-in-body libxml2 shape this spec pins). */
    LeptrisStatus st2 = LEPTRIS_OK;
    LeptrisDocument d2 = leptris_parse_html4_string(
        "<script>1<2</SCRIPT>after",
        sizeof("<script>1<2</SCRIPT>after") - 1, &st2);
    ASSERT_NE(d2, nullptr);
    LeptrisXPathResult r3 =
        leptris_xpath_eval(d2, nullptr, "string(/html/body/script)");
    ASSERT_NE(r3, nullptr);
    char* sv3 = leptris_xpath_result_string(r3);
    EXPECT_STREQ(sv3 ? sv3 : "", "1<2");
    leptris_free_string(sv3);
    leptris_xpath_result_free(r3);
    leptris_document_free(d2);
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
    /* libxml2 shape (stray </i> ignored, no clone) — the WHATWG
     * entry keeps the adopted empty <i> (adoption agency). */
    EXPECT_EQ(Html4("<b><i>x</b></i>"), "<b><i>x</i></b>");
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
    LeptrisElement root = leptris_document_root(d);
    ASSERT_NE(root, nullptr);
    /* WHATWG shape is html>[head, body] — find body by name. */
    LeptrisElement body = (LeptrisElement)leptris_node_first_child(
        (LeptrisNodeRef)root);
    while (body && strcmp(leptris_element_name(body), "body") != 0)
        body = (LeptrisElement)leptris_node_next_sibling(
            (LeptrisNodeRef)body);
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


/* #659: head-content lift — a contiguous leading run of
 * title/meta/link/base elements moves into a synthesized <head>
 * (libxml2 shape); after body content starts, nothing lifts; no
 * empty head is synthesized. */
TEST(HtmlParse, HeadContentLift) {
    const char* h1 = "<title>t</title><p>x</p>";
    LeptrisDocument d = leptris_parse_html_string(h1, strlen(h1), NULL);
    ASSERT_NE(d, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(
        d, NULL, "name(/html/*[1])");
    ASSERT_NE(r, nullptr);
    char* sv = leptris_xpath_result_string(r);
    EXPECT_STREQ(sv ? sv : "", "head");
    leptris_free_string(sv);
    leptris_xpath_result_free(r);
    r = leptris_xpath_eval(d, NULL, "name(/html/head/*[1])");
    ASSERT_NE(r, nullptr);
    sv = leptris_xpath_result_string(r);
    EXPECT_STREQ(sv ? sv : "", "title");
    leptris_free_string(sv);
    leptris_xpath_result_free(r);
    r = leptris_xpath_eval(d, NULL, "name(/html/body/*[1])");
    ASSERT_NE(r, nullptr);
    sv = leptris_xpath_result_string(r);
    EXPECT_STREQ(sv ? sv : "", "p");
    leptris_free_string(sv);
    leptris_xpath_result_free(r);
    leptris_document_free(d);

    /* title after body content does NOT lift — it stays in body.
     * (The WHATWG entry now always has a head, possibly empty.) */
    const char* h2 = "<p>x</p><title>after</title>";
    d = leptris_parse_html_string(h2, strlen(h2), NULL);
    ASSERT_NE(d, nullptr);
    r = leptris_xpath_eval(d, NULL, "count(/html/head/*)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 0.0);
    leptris_xpath_result_free(r);
    r = leptris_xpath_eval(d, NULL, "count(/html/body/title)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);
    leptris_document_free(d);

    /* meta+link+base prefix lifts; no head without head elements */
    const char* h3 = "<meta charset='utf-8'><link rel='s'><b>b</b>";
    d = leptris_parse_html_string(h3, strlen(h3), NULL);
    ASSERT_NE(d, nullptr);
    r = leptris_xpath_eval(d, NULL, "count(/html/head/*)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 2.0);
    leptris_xpath_result_free(r);
    leptris_document_free(d);
}


/* #659 characterization (Nokogiri/libxml2 ground truth, probed):
 * <template> is an ORDINARY element with its children in place
 * (libxml2 predates the WHATWG inert-fragment model), and
 * misnesting closes the formatting element at the outer end tag
 * with the stray end tag dropped - "natural nesting + stray-pop".
 * These specs lock the parity in so later slices cannot regress
 * it while chasing the html5lib corpus. */
TEST(HtmlParse, TemplateAndMisnestingLibxml2Shape) {
    const char* h1 = "<template><b>x</b>text</template><p>after</p>";
    LeptrisDocument d = leptris_parse_html4_string(h1, strlen(h1), NULL);
    ASSERT_NE(d, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(
        d, NULL, "count(/html/body/template/b)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);
    r = leptris_xpath_eval(d, NULL, "string(/html/body/template)");
    ASSERT_NE(r, nullptr);
    char* sv = leptris_xpath_result_string(r);
    EXPECT_STREQ(sv ? sv : "", "xtext");
    leptris_free_string(sv);
    leptris_xpath_result_free(r);
    leptris_document_free(d);

    const char* h2 = "<b>1<i>2</b>3</i>";
    d = leptris_parse_html4_string(h2, strlen(h2), NULL);
    ASSERT_NE(d, nullptr);
    r = leptris_xpath_eval(d, NULL, "count(/html/body/b/i)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);
    r = leptris_xpath_eval(d, NULL, "count(/html/body/i)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 0.0);
    leptris_xpath_result_free(r);
    r = leptris_xpath_eval(d, NULL, "string(/html/body)");
    ASSERT_NE(r, nullptr);
    sv = leptris_xpath_result_string(r);
    EXPECT_STREQ(sv ? sv : "", "123");
    leptris_free_string(sv);
    leptris_xpath_result_free(r);
    leptris_document_free(d);
}

/* #659 two-mode split: leptris_parse_html_string is the WHATWG
 * engine (leading script/style lift into the implied head —
 * tests16's 189-case shape); leptris_parse_html4_string keeps the
 * libxml2/Nokogiri compat shape (they stay in body). Both floors
 * pin this: html5lib 285 / parity 372. */
TEST(HtmlTwoModes, LeadingScriptPlacement) {
    LeptrisStatus st = LEPTRIS_OK;
    const char html[] = "<!DOCTYPE html><script>x</script><p>t</p>";
    LeptrisDocument d5 = leptris_parse_html_string(
        html, sizeof(html) - 1, &st);
    ASSERT_NE(d5, nullptr);
    LeptrisElement html5 = leptris_document_root(d5);
    ASSERT_NE(html5, nullptr);
    LeptrisElement head5 = leptris_element_first_child_any(html5);
    ASSERT_NE(head5, nullptr);
    EXPECT_STREQ(leptris_element_name(head5), "head");
    EXPECT_EQ(leptris_element_child_count(head5), 1u);

    LeptrisDocument d4 = leptris_parse_html4_string(
        html, sizeof(html) - 1, &st);
    ASSERT_NE(d4, nullptr);
    LeptrisElement html4 = leptris_document_root(d4);
    ASSERT_NE(html4, nullptr);
    /* libxml2: no head content run -> no synthesized <head> (the
     * no-empty-head rule); script stays first in body. */
    LeptrisElement body4 = leptris_element_last_child(html4, NULL);
    ASSERT_NE(body4, nullptr);
    EXPECT_STREQ(leptris_element_name(body4), "body");
    LeptrisElement first4 = leptris_element_first_child_any(body4);
    ASSERT_NE(first4, nullptr);
    EXPECT_STREQ(leptris_element_name(first4), "script");

    leptris_document_free(d5);
    leptris_document_free(d4);
}

/* #659 foster parenting (WHATWG entry only): text and non-table
 * elements arriving in table context insert BEFORE the table in
 * its parent; table-structure elements stay inside; whitespace
 * stays in the table; the html4 entry keeps the libxml2 shape. */
TEST(HtmlTwoModes, FosterParenting) {
    LeptrisStatus st = LEPTRIS_OK;
    const char html[] = "<table>x<tr><td>c</td></tr></table>";
    LeptrisDocument d5 = leptris_parse_html_string(
        html, sizeof(html) - 1, &st);
    ASSERT_NE(d5, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(
        d5, nullptr, "string(/html/body/text())");
    ASSERT_NE(r, nullptr);
    char* sv = leptris_xpath_result_string(r);
    EXPECT_STREQ(sv ? sv : "", "x");
    leptris_free_string(sv);
    leptris_xpath_result_free(r);
    r = leptris_xpath_eval(d5, nullptr,
                           "count(/html/body/table/tbody/tr/td)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);
    leptris_document_free(d5);

    LeptrisDocument d4 = leptris_parse_html4_string(
        html, sizeof(html) - 1, &st);
    ASSERT_NE(d4, nullptr);
    LeptrisXPathResult r4 = leptris_xpath_eval(
        d4, nullptr, "string(/html/body/table/text())");
    ASSERT_NE(r4, nullptr);
    char* sv4 = leptris_xpath_result_string(r4);
    EXPECT_STREQ(sv4 ? sv4 : "", "x");
    leptris_free_string(sv4);
    leptris_xpath_result_free(r4);
    leptris_document_free(d4);
}

/* #659 adoption agency (WHATWG entry only): a formatting element
 * closed out of order keeps its scope for later content — the
 * inner open formatting elements are cloned at the new insertion
 * point (<b>1<i>2</b>3</i> -> <b>1<i>2</i></b><i>3</i>). The html4
 * entry keeps libxml2's pop-away shape. */
TEST(HtmlTwoModes, AdoptionAgency) {
    EXPECT_EQ(Html("<b>1<i>2</b>3</i>"), "<b>1<i>2</i></b><i>3</i>");
    EXPECT_EQ(Html4("<b>1<i>2</b>3</i>"), "<b>1<i>2</i></b>3");
}

/* #659 AA step 5: the clone carries the original's attributes. */
TEST(HtmlTwoModes, AdoptionAgencyCloneKeepsAttributes) {
    EXPECT_EQ(Html(R"(<a href="h">1<i>2</a>3</i>)"),
              R"(<a href="h">1<i>2</i></a><i>3</i>)");
}

/* #659: the HTML modes record the DOCTYPE (name + legacy PUBLIC/
 * SYSTEM ids) on the document like the XML path — the corpus
 * comparator reads it via leptris_document_internal_subset. */
TEST(HtmlTwoModes, DoctypeIsRecorded) {
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<!doctype html><p>x";
    LeptrisDocument doc = leptris_parse_html_string(in, sizeof(in) - 1, &st);
    ASSERT_NE(doc, nullptr);
    LeptrisDoctype dt = leptris_document_internal_subset(doc);
    ASSERT_NE(dt, nullptr);
    EXPECT_STREQ(leptris_doctype_get_root_name(dt), "html");
    leptris_document_free(doc);

    const char in2[] =
        "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\""
        " \"http://www.w3.org/TR/html4/strict.dtd\"><p>x";
    LeptrisDocument d2 = leptris_parse_html_string(in2, sizeof(in2) - 1, &st);
    ASSERT_NE(d2, nullptr);
    LeptrisDoctype dt2 = leptris_document_internal_subset(d2);
    ASSERT_NE(dt2, nullptr);
    EXPECT_STREQ(leptris_doctype_get_root_name(dt2), "html");
    EXPECT_STREQ(leptris_doctype_get_public_id(dt2),
                 "-//W3C//DTD HTML 4.01//EN");
    EXPECT_STREQ(leptris_doctype_get_system_id(dt2),
                 "http://www.w3.org/TR/html4/strict.dtd");
    leptris_document_free(d2);

    /* No doctype -> none recorded; a second doctype does not
     * replace the first (WHATWG ignores stray doctypes in body). */
    const char in3[] = "<p>x";
    LeptrisDocument d3 = leptris_parse_html_string(in3, sizeof(in3) - 1, &st);
    ASSERT_NE(d3, nullptr);
    EXPECT_EQ(leptris_document_internal_subset(d3), nullptr);
    leptris_document_free(d3);

    const char in4[] = "<!doctype html><p>x<!doctype html2>";
    LeptrisDocument d4 = leptris_parse_html_string(in4, sizeof(in4) - 1, &st);
    ASSERT_NE(d4, nullptr);
    LeptrisDoctype dt4 = leptris_document_internal_subset(d4);
    ASSERT_NE(dt4, nullptr);
    EXPECT_STREQ(leptris_doctype_get_root_name(dt4), "html");
    leptris_document_free(d4);
}

/* #659: WHATWG insertion modes — every document is html>[head,
 * body] whatever the bare structural tags look like; the html4
 * entry keeps libxml2's shape (no empty head). */
TEST(HtmlTwoModes, StructuralHeadBodyTags) {
    /* All these WHATWG-entry shapes must be html>[head, body]. */
    const char* cases[] = {
        "<html>", "<head>", "<body>", "<html><head>",
        "<html><head></head>", "<html><head></head><body>",
        "<html><body></html>", "<head></html>",
        "<html><head></body></html>",
    };
    for (const char* c : cases) {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_html_string(c, strlen(c), &st);
        ASSERT_NE(d, nullptr) << c;
        LeptrisElement root = leptris_document_root(d);
        ASSERT_NE(root, nullptr) << c;
        EXPECT_STREQ(leptris_element_name(root), "html") << c;
        LeptrisElement first =
            (LeptrisElement)leptris_node_first_child((LeptrisNodeRef)root);
        LeptrisElement second = first
            ? (LeptrisElement)leptris_node_next_sibling((LeptrisNodeRef)first)
            : nullptr;
        ASSERT_NE(first, nullptr) << c;
        ASSERT_NE(second, nullptr) << c;
        EXPECT_STREQ(leptris_element_name(first), "head") << c;
        EXPECT_STREQ(leptris_element_name(second), "body") << c;
        EXPECT_EQ(leptris_node_next_sibling((LeptrisNodeRef)second),
                  nullptr) << c;
        leptris_document_free(d);
    }
}

TEST(HtmlTwoModes, StructuralTagsKeepContentPlacement) {
    /* Bare head/body tags with content: title lifts to head, the
     * rest stays in body — no nested head/body elements. */
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<head><title>t</title></head><body><p>x</p>";
    LeptrisDocument d = leptris_parse_html_string(in, sizeof(in) - 1, &st);
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_document_root(d);
    LeptrisElement head =
        (LeptrisElement)leptris_node_first_child((LeptrisNodeRef)root);
    LeptrisElement body =
        (LeptrisElement)leptris_node_next_sibling((LeptrisNodeRef)head);
    ASSERT_NE(head, nullptr);
    ASSERT_NE(body, nullptr);
    EXPECT_STREQ(leptris_element_name(head), "head");
    EXPECT_STREQ(leptris_element_name(body), "body");
    EXPECT_STREQ(leptris_element_name(
                     (LeptrisElement)leptris_node_first_child(
                         (LeptrisNodeRef)head)),
                 "title");
    EXPECT_STREQ(leptris_element_name(
                     (LeptrisElement)leptris_node_first_child(
                         (LeptrisNodeRef)body)),
                 "p");
    leptris_document_free(d);
}

/* #659: <template> placement — leading (before any structural
 * <body>) is a HEAD element; after a structural <body> or inside
 * content it stays in place; inside an explicit <html> the same
 * head/body split applies. Comparator flattens the WHATWG content
 * marker, so children sit directly under <template>. */
TEST(HtmlTwoModes, TemplatePlacement) {
    auto first_name = [](LeptrisDocument d, const char* path) {
        LeptrisXPathResult r = leptris_xpath_eval(d, nullptr, path);
        if (!r) return std::string("(null)");
        char* s = leptris_xpath_result_string(r);
        std::string out = s ? s : "";
        leptris_free_string(s);
        leptris_xpath_result_free(r);
        return out;
    };
    struct {
        const char* in;
        const char* head_tpl;   /* name(/html/head/child1) or "" */
        const char* body_tpl;   /* name(/html/body/child1) or "" */
    } cases[] = {
        {"<body><template>Hello</template>", "", "template"},
        {"<template>Hello</template>", "template", ""},
        {"<html><template>Hello</template>", "template", ""},
        {"<div><template></div>Hello", "", "div"},
    };
    for (const auto& c : cases) {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d =
            leptris_parse_html_string(c.in, strlen(c.in), &st);
        ASSERT_NE(d, nullptr) << c.in;
        if (c.head_tpl[0]) {
            EXPECT_EQ(first_name(d, "name(/html/head/*[1])"),
                      c.head_tpl) << c.in;
        } else {
            EXPECT_EQ(first_name(d, "count(/html/head/*)"), "0") << c.in;
        }
        if (c.body_tpl[0]) {
            EXPECT_EQ(first_name(d, "name(/html/body/*[1])"),
                      c.body_tpl) << c.in;
        }
        leptris_document_free(d);
    }
}


/* ---- #659 foreign content (WHATWG 12.2.6.5) ----
 *
 * svg/math roots and their descendants carry the SVG / MathML
 * namespace URI (exposed via namespace-uri()), element names get
 * the SVG camelCase adjustment (foreignObject, viewBox), HTML
 * integration points resume HTML rules, breakout tags pop the
 * foreign scope, CDATA in foreign content is TEXT, and a select
 * swallows foreign start tags. The html4 entry keeps everything
 * plain-HTML (libxml2 knows no foreign content). */

static std::string XQ(LeptrisDocument d, const char* path) {
    LeptrisXPathResult r = leptris_xpath_eval(d, nullptr, path);
    if (!r) return std::string("(null)");
    char* s = leptris_xpath_result_string(r);
    std::string out = s ? s : "";
    leptris_free_string(s);
    leptris_xpath_result_free(r);
    return out;
}

static LeptrisDocument ForeignDoc(const char* in) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_html_string(in, strlen(in), &st);
    return d;
}

TEST(HtmlForeign, SvgRootAndChildrenCarryNamespace) {
    LeptrisDocument d = ForeignDoc(
        "<svg viewBox=\"0 0 1 1\"><circle/></svg>");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(XQ(d, "namespace-uri(/html/body/*[name(.)=\"svg\"])"),
              "http://www.w3.org/2000/svg");
    EXPECT_EQ(XQ(d, "count(/html/body/*[name(.)=\"svg\"]/*[namespace-uri(.)="
                    "'http://www.w3.org/2000/svg'])"),
              "1");
    /* Self-closed circle: no children, svg has exactly it. */
    EXPECT_EQ(XQ(d, "count(/html/body/*[name(.)=\"svg\"]/*[name(.)=\"circle\"]/*)"), "0");
    /* Attribute-name case adjustment (viewBox, not viewbox). */
    EXPECT_EQ(XQ(d, "string(/html/body/*[name(.)=\"svg\"]/@viewBox)"), "0 0 1 1");
    leptris_document_free(d);
}

TEST(HtmlForeign, SvgCamelCaseNameAdjustment) {
    LeptrisDocument d = ForeignDoc(
        "<svg><foreignObject>x</foreignObject></svg>");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(XQ(d, "name(/html/body/*[name(.)=\"svg\"]/*[1])"), "foreignObject");
    EXPECT_EQ(XQ(d, "string(/html/body/*[name(.)=\"svg\"]/*[name(.)=\"foreignObject\"])"), "x");
    leptris_document_free(d);
}

TEST(HtmlForeign, MathmlNamespaceAndTextIntegration) {
    LeptrisDocument d = ForeignDoc("<math><mi><b>x</b></mi></math>");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(XQ(d, "namespace-uri(/html/body/*[name(.)=\"math\"])"),
              "http://www.w3.org/1998/Math/MathML");
    /* mi is a MathML text integration point: <b> is HTML inside. */
    EXPECT_EQ(XQ(d, "namespace-uri(/html/body/*[name(.)=\"math\"]/*[name(.)=\"mi\"]/*[name(.)=\"b\"])"), "");
    EXPECT_EQ(XQ(d, "string(/html/body/*[name(.)=\"math\"]/*[name(.)=\"mi\"]/*[name(.)=\"b\"])"), "x");
    leptris_document_free(d);
}

TEST(HtmlForeign, ForeignObjectIsHtmlIntegrationPoint) {
    LeptrisDocument d = ForeignDoc(
        "<svg><foreignObject><div>a</div></foreignObject></svg>");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(XQ(d, "name(/html/body/*[name(.)=\"svg\"]/*[1])"), "foreignObject");
    EXPECT_EQ(XQ(d, "string(/html/body/*[name(.)=\"svg\"]/*[name(.)=\"foreignObject\"]/*[name(.)=\"div\"])"), "a");
    EXPECT_EQ(XQ(d, "namespace-uri(/html/body/*[name(.)=\"svg\"]/*[name(.)=\"foreignObject\"]/*[name(.)=\"div\"])"),
              "");
    leptris_document_free(d);
}

TEST(HtmlForeign, BreakoutTagPopsForeignScope) {
    LeptrisDocument d = ForeignDoc("<svg><p>x</p></svg>");
    ASSERT_NE(d, nullptr);
    /* <p> is a breakout tag: svg scope popped, p is an HTML
     * sibling AFTER the (now childless) svg. */
    EXPECT_EQ(XQ(d, "count(/html/body/*)"), "2");
    EXPECT_EQ(XQ(d, "count(/html/body/*[name(.)=\"svg\"]/*)"), "0");
    EXPECT_EQ(XQ(d, "string(/html/body/p)"), "x");
    leptris_document_free(d);
}

TEST(HtmlForeign, ForeignEndTagScopesByMatch) {
    LeptrisDocument d = ForeignDoc("<svg><g>a</g>b</svg>");
    ASSERT_NE(d, nullptr);
    /* </g> closes g; text b stays INSIDE svg (</svg> closes it). */
    EXPECT_EQ(XQ(d, "string(/html/body/*[name(.)=\"svg\"]/*[name(.)=\"g\"])"), "a");
    EXPECT_EQ(XQ(d, "string(/html/body/*[name(.)=\"svg\"])"), "ab");
    EXPECT_EQ(XQ(d, "count(/html/body/*)"), "1");
    leptris_document_free(d);
}

TEST(HtmlForeign, CdataInForeignContentIsText) {
    LeptrisDocument d = ForeignDoc("<svg><![CDATA[x<y]]></svg>");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(XQ(d, "string(/html/body/*[name(.)=\"svg\"]/text())"), "x<y");
    leptris_document_free(d);
}

TEST(HtmlForeign, SelectSwallowsForeignStartTag) {
    LeptrisDocument d = ForeignDoc("<select><svg></svg></select>");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(XQ(d, "count(/html/body/select/*)"), "0");
    leptris_document_free(d);
}

TEST(HtmlForeign, Html4EntryStaysPlainHtml) {
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<svg><g>a</g></svg>";
    LeptrisDocument d = leptris_parse_html4_string(in, sizeof(in) - 1, &st);
    ASSERT_NE(d, nullptr);
    /* libxml2/Nokogiri shape: plain lowercase names, no ns. */
    EXPECT_EQ(XQ(d, "string(/html/body/*[name(.)=\"svg\"]/*[name(.)=\"g\"])"), "a");
    EXPECT_EQ(XQ(d, "namespace-uri(/html/body/*[name(.)=\"svg\"])"), "");
    EXPECT_EQ(XQ(d, "namespace-uri(/html/body/*[name(.)=\"svg\"]/*[name(.)=\"g\"])"), "");
    leptris_document_free(d);
}


/* ---- #659 in-table insertion modes (WHATWG only) ----
 *
 * WHATWG 12.2.6.4: cells/rows/cols arriving where a wrapper is
 * missing get it synthesized — tr under table implies tbody;
 * td/th under table implies tbody>tr; col under table implies
 * colgroup. The html4 entry keeps libxml2/Nokogiri's
 * no-implied-tbody shape (the committed parity reference). */
TEST(HtmlTwoModes, WhatwgImpliesTbodyForBareRows) {
    EXPECT_EQ(Html("<table><tr><td>a</table>"),
              "<table><tbody><tr><td>a</td></tr></tbody></table>");
    EXPECT_EQ(Html4("<table><tr><td>a</table>"),
              "<table><tr><td>a</td></tr></table>");
}

TEST(HtmlTwoModes, WhatwgImpliesRowForBareCells) {
    EXPECT_EQ(Html("<table><td>x<td>y</table>"),
              "<table><tbody><tr><td>x</td><td>y</td>"
              "</tr></tbody></table>");
    EXPECT_EQ(Html4("<table><td>x<td>y</table>"),
              "<table><td>x</td><td>y</td></table>");
}

TEST(HtmlTwoModes, WhatwgImpliesColgroupForBareCols) {
    EXPECT_EQ(Html("<table><col></table>"),
              "<table><colgroup><col/></colgroup></table>");
    EXPECT_EQ(Html4("<table><col></table>"),
              "<table><col/></table>");
}

TEST(HtmlTwoModes, WhatwgSecondRowReusesTbody) {
    EXPECT_EQ(Html("<table><tr><td>a<tr><td>b</table>"),
              "<table><tbody><tr><td>a</td></tr>"
              "<tr><td>b</td></tr></tbody></table>");
}

TEST(HtmlTwoModes, WhatwgExplicitTbodyNotDuplicated) {
    EXPECT_EQ(Html("<table><tbody><tr><td>a</table>"),
              "<table><tbody><tr><td>a</td></tr></tbody></table>");
}


/* ---- #659 tests19 insertion-mode edges (WHATWG only) ---- */

/* Heading END tags pop through the NEAREST heading (any of
 * h1-h6), not just the same name — </h1> with an open h3 above
 * pops to the h3's position; content after lands in the h3's
 * parent. html4 keeps exact-name matching. */
TEST(HtmlTwoModes, HeadingEndTagPopsNearestHeading) {
    EXPECT_EQ(Html("<h1><div><h3><span></h1>foo"),
              "<h1><div><h3><span/></h3>foo</div></h1>");
    EXPECT_EQ(Html("<h3><li>abc</h2>foo"),
              "<h3><li>abc</li></h3>foo");
    EXPECT_EQ(Html4("<h1><div><h3><span></h1>foo"),
              "<h1><div><h3><span/></h3></div></h1>foo");
}

/* Ruby annotation structure: rb/rt/rp close an open p like any
 * block; rt/rp close an open rb/rt/rp so annotations become
 * siblings; rb closes rb. */
TEST(HtmlTwoModes, RubyAnnotationsNestAsSiblings) {
    EXPECT_EQ(Html("<ruby>a<rb>b<rt></ruby>"),
              "<ruby>a<rb>b</rb><rt/></ruby>");
    EXPECT_EQ(Html("<ruby><p><rp>x"),
              "<ruby><p/><rp>x</rp></ruby>");
    EXPECT_EQ(Html("<ruby><rb>a<rb>b</ruby>"),
              "<ruby><rb>a</rb><rb>b</rb></ruby>");
}

/* plaintext closes p and consumes the rest of the input as raw
 * text (RAWTEXT to EOF). */
TEST(HtmlTwoModes, PlaintextClosesPAndEatsRest) {
    EXPECT_EQ(Html("<p><plaintext><b>x"),
              "<p/><plaintext>&lt;b&gt;x</plaintext>");
}

/* html5lib's tree serialization writes comments as
 * "<!-- data -->" with wrapping spaces — the comparator strips
 * that convention (our DOM keeps the data verbatim). */
TEST(HtmlTwoModes, CommentInteriorWhitespacePreserved) {
    EXPECT_EQ(Html("<table>abc<!--foo-->"),
              "abc<table><!--foo--></table>");
}


/* ---- #659 frameset mode + structural-tag attributes (WHATWG) ---- */

/* A <frameset> arriving before any body content REPLACES the
 * would-be body: html > [head, frameset]. Content after is
 * frameset content (<frame> is void). html4 keeps libxml2's
 * ordinary-element shape. */
TEST(HtmlTwoModes, FramesetReplacesEmptyBody) {
    /* The frameset is html's SECOND child — no body exists. */
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<frameset><frame src=a>";
    LeptrisDocument d = leptris_parse_html_string(in, sizeof(in) - 1, &st);
    ASSERT_NE(d, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(
        d, nullptr, "count(/html/body)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 0.0);
    leptris_xpath_result_free(r);
    r = leptris_xpath_eval(d, nullptr,
                           "name(/html/*[2])");
    ASSERT_NE(r, nullptr);
    char* ns_ = leptris_xpath_result_string(r);
    EXPECT_STREQ(ns_ ? ns_ : "", "frameset");
    leptris_free_string(ns_);
    leptris_xpath_result_free(r);
    leptris_document_free(d);
    /* html4 entry: ordinary elements inside a body. */
    LeptrisDocument d4 = leptris_parse_html4_string(in, sizeof(in) - 1, &st);
    ASSERT_NE(d4, nullptr);
    /* html4: ordinary elements inside the body (html > body). */
    LeptrisXPathResult r4 = leptris_xpath_eval(
        d4, nullptr, "name(/html/*[1])");
    ASSERT_NE(r4, nullptr);
    char* n4 = leptris_xpath_result_string(r4);
    EXPECT_STREQ(n4 ? n4 : "", "body");
    leptris_free_string(n4);
    leptris_xpath_result_free(r4);
    leptris_document_free(d4);
}

/* <frameset> AFTER body content is ignored (body wins). */
TEST(HtmlTwoModes, FramesetAfterContentIsIgnored) {
    EXPECT_EQ(Html("<p>x<frameset></frameset>"),
              "<p>x</p>");
}

/* Structural <head> start tags keep their ATTRIBUTES on the
 * synthesized head. */
TEST(HtmlTwoModes, StructuralHeadKeepsAttributes) {
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<head profile=\"p1\"><title>t";
    LeptrisDocument d = leptris_parse_html_string(in, sizeof(in) - 1, &st);
    ASSERT_NE(d, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(
        d, nullptr, "string(/html/head/@profile)");
    ASSERT_NE(r, nullptr);
    char* s2 = leptris_xpath_result_string(r);
    EXPECT_STREQ(s2 ? s2 : "", "p1");
    leptris_free_string(s2);
    leptris_xpath_result_free(r);
    leptris_document_free(d);
}

TEST(HtmlTwoModes, StructuralBodyAttributesLandOnBody) {
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<body bgcolor=\"red\" onload='f()'><p>x";
    LeptrisDocument d = leptris_parse_html_string(in, sizeof(in) - 1, &st);
    ASSERT_NE(d, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(
        d, nullptr, "string(/html/body/@bgcolor)");
    ASSERT_NE(r, nullptr);
    char* s = leptris_xpath_result_string(r);
    EXPECT_STREQ(s ? s : "", "red");
    leptris_free_string(s);
    leptris_xpath_result_free(r);
    leptris_document_free(d);
}


/* ---- #659 "in head noscript" (scripting off) ----
 *
 * <noscript> opened in head phase: comments and head content
 * stay inside; the first body-ish token pops it (reprocessed at
 * body level); <html> attrs merge onto the html element;
 * </br> means <br>; <noframes> is RAWTEXT. */
TEST(HtmlTwoModes, NoscriptInHeadPopsOnBodyTag) {
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<head><noscript><p>x</noscript>";
    LeptrisDocument d = leptris_parse_html_string(in, sizeof(in) - 1, &st);
    ASSERT_NE(d, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(
        d, nullptr, "name(/html/head/*[1])");
    ASSERT_NE(r, nullptr);
    char* s = leptris_xpath_result_string(r);
    EXPECT_STREQ(s ? s : "", "noscript");
    leptris_free_string(s);
    leptris_xpath_result_free(r);
    r = leptris_xpath_eval(d, nullptr,
                           "count(/html/head/noscript/*)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 0.0);
    leptris_xpath_result_free(r);
    r = leptris_xpath_eval(d, nullptr, "string(/html/body/p)");
    ASSERT_NE(r, nullptr);
    s = leptris_xpath_result_string(r);
    EXPECT_STREQ(s ? s : "", "x");
    leptris_free_string(s);
    leptris_xpath_result_free(r);
    leptris_document_free(d);
}

TEST(HtmlTwoModes, NoscriptInHeadKeepsComments) {
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<head><noscript><!--foo--></noscript>";
    LeptrisDocument d = leptris_parse_html_string(in, sizeof(in) - 1, &st);
    ASSERT_NE(d, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(
        d, nullptr, "count(/html/head/noscript/comment())");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);
    leptris_document_free(d);
}

TEST(HtmlTwoModes, NoscriptHtmlAttrsMerge) {
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<head><noscript><html class=foo></noscript>";
    LeptrisDocument d = leptris_parse_html_string(in, sizeof(in) - 1, &st);
    ASSERT_NE(d, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(
        d, nullptr, "string(/html/@class)");
    ASSERT_NE(r, nullptr);
    char* s = leptris_xpath_result_string(r);
    EXPECT_STREQ(s ? s : "", "foo");
    leptris_free_string(s);
    leptris_xpath_result_free(r);
    leptris_document_free(d);
}

TEST(HtmlTwoModes, EndBrMeansBrStart) {
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<div>a</br>";
    LeptrisDocument d = leptris_parse_html_string(in, sizeof(in) - 1, &st);
    ASSERT_NE(d, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(
        d, nullptr, "count(/html/body/div/br)");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);
    leptris_document_free(d);
}

TEST(HtmlTwoModes, NoframesIsRawText) {
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<noframes>XXX</noscript></noframes>";
    LeptrisDocument d = leptris_parse_html_string(in, sizeof(in) - 1, &st);
    ASSERT_NE(d, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(
        d, nullptr, "string(/html/head/noframes)");
    ASSERT_NE(r, nullptr);
    char* s = leptris_xpath_result_string(r);
    EXPECT_STREQ(s ? s : "", "XXX</noscript>");
    leptris_free_string(s);
    leptris_xpath_result_free(r);
    leptris_document_free(d);
}


/* ---- #659 character-reference decoding (WHATWG 12.2.5.72-78) ----
 *
 * Legacy references decode WITHOUT the semicolon (longest
 * prefix); in attributes a missing ';' followed by '=' or an
 * alphanumeric stays literal; numeric references decode with or
 * without the ';'. */
TEST(HtmlParse, LegacyEntityDecodesWithoutSemicolon) {
    EXPECT_EQ(Html("FOO&gtBAR"), "FOO&gt;BAR");
    EXPECT_EQ(Html("FOO&amp x"), "FOO&amp; x");
}

TEST(HtmlParse, LongestPrefixMatch) {
    /* "notit;" is not an entity; the longest match "not" wins. */
    EXPECT_EQ(Html("I&apos;m &notit; I tell you"),
              "I'm \xC2\xACit; I tell you");
}

TEST(HtmlParse, AttrEntityLiteralGuard) {
    LeptrisStatus st = LEPTRIS_OK;
    const char in[] = "<div bar=\"ZZ&pound_id=23\" b2=\"ZZ&pound=23\""
                      " b3=\"ZZ&gt YY\">";
    LeptrisDocument d = leptris_parse_html_string(in, sizeof(in) - 1, &st);
    ASSERT_NE(d, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval(
        d, nullptr, "string(/html/body/div/@bar)");
    ASSERT_NE(r, nullptr);
    char* s = leptris_xpath_result_string(r);
    EXPECT_STREQ(s ? s : "", "ZZ\xC2\xA3_id=23");
    leptris_free_string(s);
    leptris_xpath_result_free(r);
    r = leptris_xpath_eval(d, nullptr, "string(/html/body/div/@b2)");
    ASSERT_NE(r, nullptr);
    s = leptris_xpath_result_string(r);
    EXPECT_STREQ(s ? s : "", "ZZ&pound=23");
    leptris_free_string(s);
    leptris_xpath_result_free(r);
    r = leptris_xpath_eval(d, nullptr, "string(/html/body/div/@b3)");
    ASSERT_NE(r, nullptr);
    s = leptris_xpath_result_string(r);
    EXPECT_STREQ(s ? s : "", "ZZ> YY");
    leptris_free_string(s);
    leptris_xpath_result_free(r);
    leptris_document_free(d);
}

TEST(HtmlParse, NumericRefDecodesWithoutSemicolon) {
    EXPECT_EQ(Html("FOO&#41BAR"), "FOO)BAR");
    EXPECT_EQ(Html("FOO&#x41BAR"), "FOO\xE4\x86\xBA" "R");
}


/* ---- #659 comment tokenizer edges (WHATWG 12.2.5.x) ---- */
TEST(HtmlParse, CommentCloseForms) {
    EXPECT_EQ(Html("FOO<!-- BAR --!>BAZ"), "FOO<!-- BAR -->BAZ");
    EXPECT_EQ(Html("FOO<!-- BAR -- <QUX> -- MUX --!>BAZ"),
              "FOO<!-- BAR -- <QUX> -- MUX -->BAZ");
    /* EOF inside a comment: data runs to the end, verbatim. */
    EXPECT_EQ(Html("FOO<!-- BAR --!"), "FOO<!-- BAR --!-->");
}

TEST(HtmlParse, EmptyCommentForms) {
    EXPECT_EQ(Html("FOO<!--->BAZ"), "FOO<!---->BAZ");
    EXPECT_EQ(Html("FOO<!-->BAZ"), "FOO<!---->BAZ");
}
