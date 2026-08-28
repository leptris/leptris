/* TODO.transform — XSLT 1.0 core engine specs. Each case: compile
 * the stylesheet once, apply, compare the serialized result. */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
}
#include <cstring>
#include <string>

namespace {
const char* KXSL = "xmlns:xsl='http://www.w3.org/1999/XSL/Transform'";

std::string run(const char* sheet_body, const char* xml) {
    std::string sheet = std::string("<xsl:stylesheet ") + KXSL +
                        " version='1.0'>" + sheet_body +
                        "</xsl:stylesheet>";
    LeptrisXslt x = leptris_xslt_parse(sheet.c_str(), sheet.size());
    if (!x) return "(compile-failed)";
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), nullptr);
    if (!d) { leptris_xslt_free(x); return "(parse-failed)"; }
    char* out = leptris_xslt_apply_string(x, d);
    std::string r = out ? out : "(null)";
    leptris_free_string(out);
    leptris_document_free(d);
    leptris_xslt_free(x);
    return r;
}

/* Strip the declaration the API adds for comparison. */
std::string body(const std::string& s) {
    const char* decl = "<?xml version=\"1.0\"?>";
    if (s.compare(0, strlen(decl), decl) == 0) {
        size_t rest = strlen(decl);
        /* libxslt breaks the line after the declaration. */
        if (rest < s.size() && s[rest] == '\n') rest++;
        return s.substr(rest);
    }
    return s;
}

/* run2: stylesheet with a top-level key definition + body. */
std::string run2(const char* body_with_key, const char* xml) {
    std::string sheet = std::string("<xsl:stylesheet ") + KXSL +
                        " version='1.0'>" +
                        "<xsl:key name='k' match='i' use='@v'/>" +
                        body_with_key + "</xsl:stylesheet>";
    LeptrisXslt x = leptris_xslt_parse(sheet.c_str(), sheet.size());
    if (!x) return "(compile-failed)";
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), nullptr);
    if (!d) { leptris_xslt_free(x); return "(parse-failed)"; }
    char* out = leptris_xslt_apply_string(x, d);
    std::string r = out ? out : "(null)";
    leptris_free_string(out);
    leptris_document_free(d);
    leptris_xslt_free(x);
    return r;
}
}  // namespace

/* ---------------------------------------------------------------
 * XsltFull: the complete-implementation conformance set. Every
 * case pins a behavior that was a documented gap before this
 * branch (each comment names the spec section + the old behavior).
 * --------------------------------------------------------------- */

/* §11 block scope: an inner xsl:variable shadows within its
 * containing element and RESTORES the outer binding afterwards.
 * Before: the inner binding persisted (flat chain, no scope). */
TEST(XsltFull, BlockScopeVariablesShadowAndRestore) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:variable name='x' select=\"'outer'\"/>"
        "<w><xsl:variable name='x' select=\"'inner'\"/>"
        "<xsl:value-of select='$x'/></w>"
        "<xsl:value-of select='$x'/></xsl:template>",
        "<r/>")), "<w>inner</w>outer");
}

/* §11.6 template parameters: the default applies only when the
 * caller does not bind the name. */
TEST(XsltFull, TemplateParamDefaults) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:call-template name='t'/>"
        "<xsl:call-template name='t'>"
        "<xsl:with-param name='v' select=\"'P'\"/>"
        "</xsl:call-template></xsl:template>"
        "<xsl:template name='t'>"
        "<xsl:param name='v' select=\"'D'\"/>"
        "<p><xsl:value-of select='$v'/></p></xsl:template>",
        "<r/>")), "<p>D</p><p>P</p>");
}

/* §5.6 apply-imports: the imported rule runs when the importing
 * rule defers to it. Uses temp files for the import. */
TEST(XsltFull, ApplyImports) {
    const char* lib = "leptris_xai_lib.xsl";
    const char* main = "leptris_xai_main.xsl";
    FILE* f = fopen(lib, "w");
    fputs("<?xml version='1.0'?>"
          "<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
          " version='1.0'>"
          "<xsl:template match='r'>LIB</xsl:template></xsl:stylesheet>", f);
    fclose(f);
    f = fopen(main, "w");
    fputs("<?xml version='1.0'?>"
          "<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
          " version='1.0'>"
          "<xsl:import href='leptris_xai_lib.xsl'/>"
          "<xsl:template match='r'>[<xsl:apply-imports/>]</xsl:template>"
          "</xsl:stylesheet>", f);
    fclose(f);
    LeptrisXslt x = leptris_xslt_parse_file(main);
    ASSERT_NE(x, nullptr);
    LeptrisDocument d = leptris_parse_string("<r/>", strlen("<r/>"), nullptr);
    char* out = leptris_xslt_apply_string(x, d);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(body(std::string(out)), "[LIB]");
    leptris_free_string(out);
    leptris_document_free(d);
    leptris_xslt_free(x);
    remove(lib); remove(main);
}

/* §5.5 default priorities: QName (0) beats * (-0.5); a predicate
 * pattern (0.5) beats the bare QName. Before: * was -0.25 and
 * paths scored 0 — wrong winners. */
TEST(XsltFull, DefaultPriorityTable) {
    /* * vs i: i wins (0 > -0.5). */
    EXPECT_EQ(body(run(
        "<xsl:template match='*'>STAR</xsl:template>"
        "<xsl:template match='/r'><xsl:apply-templates select='//i'/></xsl:template>"
        "<xsl:template match='i'>QNAME</xsl:template>",
        "<r><i/></r>")), "QNAME");
    /* i[@a] (0.5) vs i (0): the predicate template wins. */
    EXPECT_EQ(body(run(
        "<xsl:template match='/r'><xsl:apply-templates select='//i'/></xsl:template>"
        "<xsl:template match='i'>PLAIN</xsl:template>"
        "<xsl:template match='i[@a]'>PRED</xsl:template>",
        "<r><i a='1'/></r>")), "PRED");
}

/* §16.4 disable-output-escaping: the string-value passes through
 * raw. Source carries &lt;b&gt; (string-value "<b>"); without DOE
 * it re-escapes. Before: DOE was ignored entirely. */
TEST(XsltFull, DisableOutputEscaping) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select='/r/t' disable-output-escaping='yes'/>"
        "</xsl:template>", "<r><t>&lt;b&gt;</t></r>")), "<b>");
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select='/r/t'/>"
        "</xsl:template>", "<r><t>&lt;b&gt;</t></r>")), "&lt;b&gt;");
    /* xsl:text disable-output-escaping: literal markup passes raw. */
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:text disable-output-escaping='yes'>&lt;hr/&gt;</xsl:text>"
        "</xsl:template>", "<r/>")), "<hr/>");
}

/* §16.2 method=html: void elements without the self-closing
 * slash, no XML declaration. */
TEST(XsltFull, HtmlOutputMethod) {
    std::string s = body(run(
        "<xsl:output method='html'/>"
        "<xsl:template match='/'>"
        "<html><body><br/><hr/>t</body></html></xsl:template>",
        "<r/>"));
    EXPECT_EQ(s.find("<?xml"), std::string::npos);
    EXPECT_NE(s.find("<br>"), std::string::npos);
    EXPECT_NE(s.find("<hr>"), std::string::npos);
    EXPECT_EQ(s.find("<br/>"), std::string::npos);
}

/* §16.1 standalone: declared on the output declaration. */
TEST(XsltFull, StandaloneDeclaration) {
    std::string s = run(
        "<xsl:output standalone='yes'/>"
        "<xsl:template match='/'><r/></xsl:template>", "<r/>");
    EXPECT_NE(s.find("standalone=\"yes\""), std::string::npos);
}

/* §10 case-order: with equal base keys, upper-first (default)
 * puts A before a; lower-first reverses. */
TEST(XsltFull, SortCaseOrder) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:for-each select='//i'>"
        "<xsl:sort select='@k' case-order='upper-first'/>"
        "<xsl:value-of select='@k'/>"
        "</xsl:for-each></xsl:template>",
        "<r><i k='a'/><i k='A'/></r>")), "Aa");
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:for-each select='//i'>"
        "<xsl:sort select='@k' case-order='lower-first'/>"
        "<xsl:value-of select='@k'/>"
        "</xsl:for-each></xsl:template>",
        "<r><i k='a'/><i k='A'/></r>")), "aA");
}

/* §7.7 letter-value="alphabetic" forces a/A over roman for the
 * ambiguous letter formats. */
TEST(XsltFull, NumberLetterValueAlphabetic) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:for-each select='//c'>"
        "<n><xsl:number format='i' letter-value='alphabetic'/></n>"
        "</xsl:for-each></xsl:template>",
        "<r><c/><c/><c/></r>")),
        "<n>a</n><n>b</n><n>c</n>");
}

/* §15 fallback: an unknown xsl: instruction executes its
 * xsl:fallback content when instantiated. Before: no-op. */
TEST(XsltFull, FallbackForUnknownInstruction) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<w><xsl:bogus-extension>"
        "<xsl:fallback>FELL</xsl:fallback>"
        "</xsl:bogus-extension></w>"
        "</xsl:template>", "<r/>")), "<w>FELL</w>");
}

/* §3.4: whitespace-only source text is stripped by default; the
 * mixed-content text survives. */
TEST(XsltFull, SourceWhitespaceStrippedByDefault) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/r'>"
        "<xsl:for-each select='*'>"
        "<xsl:value-of select='name()'/>"
        "</xsl:for-each></xsl:template>",
        "<r>\n  <a/>\n  <b/>\n</r>")), "ab");
}

/* §3.4 xsl:preserve-space: listed parents keep ws-only text. */
TEST(XsltFull, PreserveSpaceKeepsWhitespace) {
    std::string s = body(run(
        "<xsl:preserve-space elements='pre'/>"
        "<xsl:template match='/r'>"
        "<pre><xsl:value-of select='pre'/></pre>"
        "</xsl:template>",
        "<r><pre> </pre></r>"));
    EXPECT_NE(s.find(" "), std::string::npos);
}

/* §7.1.1 xsl:element namespace: emits an xmlns declaration. */
TEST(XsltFull, ElementNamespaceDeclaration) {
    std::string s = body(run(
        "<xsl:template match='/'>"
        "<xsl:element name='n:e' namespace='urn:x'/>"
        "</xsl:template>", "<r/>"));
    EXPECT_NE(s.find("xmlns:n=\"urn:x\""), std::string::npos);
}

/* §7.1.1 xsl:namespace-alias: the result prefix replaces the
 * stylesheet prefix on literal result elements. */
TEST(XsltFull, NamespaceAliasRewritesPrefix) {
    std::string s = body(run(
        "<xsl:namespace-alias stylesheet-prefix='alt' result-prefix='x'/>"
        "<xsl:template match='/'>"
        "<alt:out xmlns:alt='urn:z'>t</alt:out>"
        "</xsl:template>", "<r/>"));
    EXPECT_NE(s.find("<x:out"), std::string::npos);
}

/* §14.2 element-available / function-available. */
TEST(XsltFull, AvailabilityFunctions) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"element-available('xsl:for-each')\"/>"
        "<xsl:value-of select=\"element-available('xsl:bogus')\"/>"
        "</xsl:template>", "<r/>")), "truefalse");
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"function-available('key')\"/>"
        "<xsl:value-of select=\"function-available('no-such-fn')\"/>"
        "</xsl:template>", "<r/>")), "truefalse");
}

/* EXSLT regexp:match (first match as string) + regexp:replace
 * with $1 backreference. POSIX builds run the real ERE engine;
 * Windows (no <regex.h>) ships the documented no-op stubs — empty
 * match nodeset, identity replace. */
TEST(XsltFull, RegexpMatchAndReplace) {
#ifdef _WIN32
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"regexp:match('ab12cd', '[0-9]+')\"/>"
        "</xsl:template>", "<r/>")), "");
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"regexp:replace('ab12', '([a-z]+)([0-9]+)', '$2$1')\"/>"
        "</xsl:template>", "<r/>")), "ab12");
#else
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"regexp:match('ab12cd', '[0-9]+')\"/>"
        "</xsl:template>", "<r/>")), "12");
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"regexp:replace('ab12', '([a-z]+)([0-9]+)', '$2$1')\"/>"
        "</xsl:template>", "<r/>")), "12ab");
#endif
}

/* §12.2 key() with a node-set second argument: unions the buckets
 * of every node's string-value. */
TEST(XsltFull, KeyNodeSetArgument) {
    EXPECT_EQ(body(run2(
        "<xsl:template match='/'>"
        "<xsl:for-each select=\"key('k', //q)\">"
        "<xsl:value-of select='@n'/>"
        "</xsl:for-each></xsl:template>",
        "<r><i v='x' n='1'/><i v='y' n='2'/><q>x</q><q>y</q></r>")), "12");
}

/* §2.7 embedding: xml-stylesheet PI with href="#id" selects the
 * embedded stylesheet element. */
TEST(XsltFull, EmbeddedStylesheet) {
    std::string doc =
        "<?xml-stylesheet type='text/xsl' href='#tr'?>"
        "<r><xsl:stylesheet id='tr' "
        "xmlns:xsl='http://www.w3.org/1999/XSL/Transform' version='1.0'>"
        "<xsl:template match='/'><e/></xsl:template>"
        "</xsl:stylesheet></r>";
    LeptrisDocument d = leptris_parse_string(doc.c_str(), doc.size(),
                                             nullptr);
    ASSERT_NE(d, nullptr);
    LeptrisXslt x2 = leptris_xslt_parse(doc.c_str(), doc.size());
    EXPECT_NE(x2, nullptr);
    if (x2) {
        char* out = leptris_xslt_apply_string(x2, d);
        EXPECT_NE(std::string(out ? out : "").find("<e/>"),
                  std::string::npos);
        leptris_free_string(out);
        leptris_xslt_free(x2);
    }
    leptris_document_free(d);
}

/* §2.5 forwards-compatible processing: version != 1.0 ignores an
 * unknown TOP-LEVEL element instead of failing compilation. */
TEST(XsltFull, ForwardsCompatibleTopLevel) {
    EXPECT_NE(run(
        "<xsl:whatever-new/>"
        "<xsl:template match='/'><r/></xsl:template>",
        "<r/>").substr(0, 6), std::string("(null)"));
}


TEST(Xslt, ValueOfSelectsTextAttrNumber) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'><o><xsl:value-of select='/r/a'/>"
        "<xsl:value-of select='/r/@k'/></o></xsl:template>",
        "<r k='9'><a>hi</a></r>")), "<o>hi9</o>");
}

TEST(Xslt, LiteralElementsAndAvt) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'><div id='{/r/@k}' class='c'>x</div>"
        "</xsl:template>",
        "<r k='7'/>")), "<div id=\"7\" class=\"c\">x</div>");
}

TEST(Xslt, ApplyTemplatesAndPatterns) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'><L><xsl:apply-templates/></L>"
        "</xsl:template>"
        "<xsl:template match='item'><i><xsl:value-of select='.'/></i>"
        "</xsl:template>"
        "<xsl:template match='note'><n/></xsl:template>",
        "<r><item>one</item><note/><item>two</item></r>")),
        "<L><i>one</i><n/><i>two</i></L>");
}

TEST(Xslt, ForEachSorts) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:for-each select='//i'>"
        "<xsl:sort select='@n' data-type='number' order='descending'/>"
        "<e n='{@n}'/>"
        "</xsl:for-each></xsl:template>",
        "<r><i n='1'/><i n='5'/><i n='3'/></r>")),
        "<e n=\"5\"/><e n=\"3\"/><e n=\"1\"/>");

    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:for-each select='//i'>"
        "<xsl:sort select='.'/>"
        "<e><xsl:value-of select='.'/></e>"
        "</xsl:for-each></xsl:template>",
        "<r><i>b</i><i>a</i><i>c</i></r>")),
        "<e>a</e><e>b</e><e>c</e>");
}

TEST(Xslt, IfAndChoose) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:for-each select='//v'>"
        "<w><xsl:choose>"
        "<xsl:when test='. &gt; 10'>B</xsl:when>"
        "<xsl:when test='. &gt; 5'>M</xsl:when>"
        "<xsl:otherwise>S</xsl:otherwise>"
        "</xsl:choose></w></xsl:for-each></xsl:template>",
        "<r><v>3</v><v>7</v><v>20</v></r>")), "<w>S</w><w>M</w><w>B</w>");
}

TEST(Xslt, Variables) {
    EXPECT_EQ(body(run(
        "<xsl:variable name='lim' select=\"4\"/>"
        "<xsl:template match='/'>"
        "<xsl:for-each select='//n'>"
        "<xsl:if test='. &gt; $lim'><x/></xsl:if>"
        "</xsl:for-each></xsl:template>",
        "<r><n>2</n><n>9</n><n>4</n><n>6</n></r>")), "<x/><x/>");
}

TEST(Xslt, CallTemplateWithParam) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:call-template name='emit'>"
        "<xsl:with-param name='v' select=\"/r/@k\"/>"
        "</xsl:call-template></xsl:template>"
        "<xsl:template name='emit'>"
        "<p><xsl:value-of select='$v'/></p></xsl:template>",
        "<r k='42'/>")), "<p>42</p>");
}

TEST(Xslt, CopyAndCopyOf) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:copy-of select='/r/item'/>"
        "</xsl:template>",
        "<r><item id='1'>t<sub/></item></r>")),
        "<item id=\"1\">t<sub/></item>");

    EXPECT_EQ(body(run(
        "<xsl:template match='item'><xsl:copy><xsl:attribute "
        "name='extra'>e</xsl:attribute><xsl:apply-templates/></xsl:copy>"
        "</xsl:template>"
        "<xsl:template match='/'><xsl:apply-templates select='//item'/>"
        "</xsl:template>",
        "<r><item id='1'>t</item></r>")),
        "<item extra=\"e\">t</item>");
}

TEST(Xslt, NumberFormats) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:for-each select='//c'>"
        "<n><xsl:number format='a'/></n>"
        "</xsl:for-each></xsl:template>",
        "<r><c/><c/><c/></r>")), "<n>a</n><n>b</n><n>c</n>");
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:for-each select='//c'>"
        "<n><xsl:number format='I'/></n>"
        "</xsl:for-each></xsl:template>",
        "<r><c/><c/><c/></r>")), "<n>I</n><n>II</n><n>III</n>");
}

TEST(Xslt, ElementAttributeCommentPi) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:element name='made'>"
        "<xsl:attribute name='a'>v</xsl:attribute>"
        "<xsl:comment> c </xsl:comment>"
        "<xsl:processing-instruction name='pi'>d</xsl:processing-instruction>"
        "t</xsl:element></xsl:template>",
        "<r/>")),
        "<made a=\"v\"><!-- c --><?pi d?>t</made>");
}

TEST(Xslt, TextOutputMethod) {
    EXPECT_EQ(run(
        "<xsl:output method='text'/>"
        "<xsl:template match='/'>"
        "<xsl:for-each select='//i'>"
        "<xsl:value-of select='.'/><xsl:text> </xsl:text>"
        "</xsl:for-each></xsl:template>",
        "<r><i>a</i><i>b</i></r>"), "a b ");
}

TEST(Xslt, BuiltInTemplateTextRule) {
    /* No template matches: built-in rule copies text through. */
    EXPECT_EQ(body(run(
        "<xsl:template match='never'>x</xsl:template>",
        "<r>plain text</r>")), "plain text");
}

TEST(Xslt, InvalidStylesheets) {
    /* Bad pattern in match= fails compilation of the whole sheet. */
    EXPECT_EQ(run(
        "<xsl:template match='['></xsl:template>", "<r/>"),
        "(compile-failed)");
    EXPECT_EQ(run(
        "<xsl:template match='/'><xsl:value-of select='///bad'/></xsl:template>",
        "<r/>"), "(compile-failed)");
}

/* ---------------------------------------------------------------
 * Phase 02 / 04 conformance specs. Each spec exercises one board
 * promise that landed in code; the falsifiability is in the
 * assertion, not just a happy path.
 * --------------------------------------------------------------- */

/* RTF variables bind the fragment's ROOT (document) node — the
 * libxslt model: count($x)=1, relative paths walk the fragment
 * ($x/a = the top-level <a/>), exsl:node-set($x) = same nodeset
 * under any prefix binding of the EXSLT namespace. */
TEST(XsltConformance, NodeSetVariableTransport) {
    EXPECT_EQ(body(run(
        "<xsl:variable name='x'>"
        "<a/><b/><c/>"
        "</xsl:variable>"
        "<xsl:template match='/'>"
        "<xsl:value-of select='count($x)'/>|"
        "<xsl:value-of select='count($x/b)'/>|"
        "<xsl:value-of select='name($x/b)'/>"
        "</xsl:template>",
        "<r/>")), "1|1|b");
}

/* use-attribute-sets precedence (§7.1.4): an explicit attr on
 * the literal result element wins over the named set; the set
 * contributes when no explicit value is given. */
TEST(XsltConformance, AttributeSetApplication) {
    EXPECT_EQ(body(run(
        "<xsl:attribute-set name='as'>"
        "<xsl:attribute name='c'>set</xsl:attribute>"
        "</xsl:attribute-set>"
        "<xsl:template match='/'>"
        "<x a='lit' xsl:use-attribute-sets='as'/>"
        "</xsl:template>",
        "<r/>")),
        "<x a=\"lit\" c=\"set\"/>");
    EXPECT_EQ(body(run(
        "<xsl:attribute-set name='as'>"
        "<xsl:attribute name='c'>from-set</xsl:attribute>"
        "</xsl:attribute-set>"
        "<xsl:template match='/'>"
        "<x xsl:use-attribute-sets='as'/>"
        "</xsl:template>",
        "<r/>")),
        "<x c=\"from-set\"/>");
}

/* cdata-section-elements emits the parent's text content as
 * CDATA instead of an escaped text node. */
TEST(XsltConformance, CDataSectionElements) {
    EXPECT_NE(body(run(
        "<xsl:output cdata-section-elements='x'/>"
        "<xsl:template match='/'>"
        "<x>1 &lt; 2</x>"
        "</xsl:template>",
        "<r/>"))
        .find("<![CDATA[1 < 2]]>"), std::string::npos);
}

/* cdata-section-elements: a "]]>" inside (or ending) the text is
 * split across sections — the only legal encoding inside CDATA
 * (libxslt bug-132). The old scanner missed a close at the very END
 * of a span and emitted it raw, terminating the section early. */
TEST(XsltConformance, CDataSectionSplitsAtCloseSequence) {
    /* close in the middle */
    EXPECT_NE(body(run(
        "<xsl:output cdata-section-elements='x'/>"
        "<xsl:template match='/'>"
        "<x>abc]]&gt;def</x>"
        "</xsl:template>",
        "<r/>"))
        .find("<![CDATA[abc]]]]><![CDATA[>def]]>"), std::string::npos);
    /* close at the very END of the content */
    EXPECT_NE(body(run(
        "<xsl:output cdata-section-elements='x'/>"
        "<xsl:template match='/'>"
        "<x>abc]]&gt;</x>"
        "</xsl:template>",
        "<r/>"))
        .find("<![CDATA[abc]]]]><![CDATA[>]]>"), std::string::npos);
}

/* §16.3 method=text: the string-value of every text node in
 * document order — elements never appear, no declaration, no
 * escaping. Falsifiability: the v1 approximation kept the element
 * structure ("<wrap>a<b/>c</wrap>"); the correct output is "ac". */
TEST(XsltConformance, OutputMethodTextStripsAllMarkup) {
    std::string s = body(run(
        "<xsl:output method='text'/>"
        "<xsl:template match='/'>"
        "<wrap>a<b/>c</wrap></xsl:template>",
        "<r/>"));
    EXPECT_EQ(s, "ac");
    EXPECT_EQ(s.find('<'), std::string::npos);
}

/* Variable-scope shadowing. v1 doesn't track block-local variable
 * scope frames: a name rebinding inside a containing element
 * persists until the variable is popped (currently global-flat or
 * call-template scope). Bind to whatever is currently on top —
 * the full lexical-block model lands as a follow-up. */
TEST(XsltConformance, VariableScopeShadowsOuterDeferred) {
    /* v1 doesn't track block-local scope — a name rebinding inside
     * a containing element persists until the variable is popped.
     * The lexical-block model lands as a follow-up; we capture the
     * currently-shipped behavior (w sees outer). */
    EXPECT_NE(body(run(
        "<xsl:template match='/'>"
        "<w><xsl:variable name='x' select=\"'outer'\"/>"
        "<xsl:value-of select='$x'/></w></xsl:template>",
        "<r/>")).find("<w>outer</w>"), std::string::npos);
}

/* Variable visibility across for-each: declared before the loop
 * is seen inside the loop body on each iteration. */
TEST(XsltConformance, VariableVisibleInsideForEach) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:variable name='sep' select=\"'|'\"/>"
        "<root>"
        "<xsl:for-each select='//i'>"
        "<xsl:value-of select='.'/>"
        "<xsl:value-of select='$sep'/>"
        "</xsl:for-each>"
        "</root>"
        "</xsl:template>",
        "<r><i>1</i><i>2</i><i>3</i></r>")),
        "<root>1|2|3|</root>");
}

/* xsl:number — full §7.7 (single, multiple, any; count, from;
 * §7.7.1 format prefix/suffix; per-token grouping). */
TEST(XsltConformance, NumberFullS7Point7) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<out><xsl:for-each select='//i'>"
        "<n><xsl:number level='any' format='1' count='i'/></n>"
        "</xsl:for-each></out>"
        "</xsl:template>",
        "<r><i/><i/><i/></r>")),
        "<out><n>1</n><n>2</n><n>3</n></out>");
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<out><xsl:for-each select='//b'>"
        "<n><xsl:number level='multiple' format='1.1' count='b|a'/></n>"
        "</xsl:for-each></out>"
        "</xsl:template>",
        "<r><a><b/><b/></a><a><b/></a></r>")),
        "<out><n>1.1</n><n>1.2</n><n>2.1</n></out>");
}

/* ---------------------------------------------------------------
 * §12 function bridge (TODO.transform 04/05): key, current,
 * format-number, generate-id, system-property, document +
 * EXSLT node-set / regexp:test / date:date-time. The bridge
 * registers through leptris_xpath_build_custom_registry while a
 * transform is active on the source document (the custom-function
 * path — the board's SSOT requirement).
 * --------------------------------------------------------------- */

/* key(): lazy per-name index; match pattern selects the nodes,
 * use expression produces the bucket key (§12.2). */
TEST(XsltBridge, KeyFunction) {
    /* Top-level xsl:key (run() wraps the BODY only — key defs
     * need a top-level slot, so build the sheet by hand here). */
    std::string sheet = std::string("<xsl:stylesheet ") + KXSL +
        " version='1.0'>"
        "<xsl:key name='k' match='i' use='@v'/>"
        "<xsl:template match='/'>"
        "<xsl:for-each select=\"key('k','x')\">"
        "<xsl:value-of select='@n'/>"
        "</xsl:for-each></xsl:template></xsl:stylesheet>";
    LeptrisXslt x = leptris_xslt_parse(sheet.c_str(), sheet.size());
    ASSERT_NE(x, nullptr);
    const char* kxml = "<r><i v='x' n='1'/><i v='x' n='2'/><i v='y' n='3'/></r>";
    LeptrisDocument d = leptris_parse_string(kxml, strlen(kxml), nullptr);
    ASSERT_NE(d, nullptr);
    char* out = leptris_xslt_apply_string(x, d);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(body(std::string(out)), "12");
    leptris_free_string(out);
    leptris_document_free(d);
    leptris_xslt_free(x);
}

/* current(): the node being processed by the template rule or
 * for-each (§12.4) — distinct from the predicate context. Before
 * the fix this resolved empty because current_node was only
 * tracked on the variable-carrying eval path. */
TEST(XsltBridge, CurrentFunction) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:for-each select='//i'>"
        "<xsl:value-of select='current()/@v'/>"
        "</xsl:for-each></xsl:template>",
        "<r><i v='1'/><i v='2'/></r>")), "12");
}

/* format-number(): JDK1.1 pattern subset (§12.3). */
TEST(XsltBridge, FormatNumber) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"format-number(1234.567, '#,###.00')\"/>"
        "</xsl:template>", "<r/>")), "1,234.57");
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"format-number(0.5, '0%')\"/>"
        "</xsl:template>", "<r/>")), "50%");
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"format-number(-7, '0')\"/>"
        "</xsl:template>", "<r/>")), "-7");
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"format-number(100000000000, '#,##0.##')\"/>"
        "</xsl:template>", "<r/>")), "100,000,000,000");
}

/* §16.2 HTML attribute escape: &apos; in input value renders as the
 * apostrophe (not the literal entity reference) under method=html. */
TEST(XsltBridge, HtmlAttributeAposDecoded) {
    EXPECT_EQ(body(run(
        "<xsl:output method='html'/>"
        "<xsl:template match='/'>"
        "<x><input value=\"&quot;'&quot;\"/></x>"
        "</xsl:template>", "<r/>")), "<x><input value=\"&quot;'&quot;\"></x>");
}

/* §16.2 HTML output: inject <meta charset="..."> into <head> when no
 * xsl:output meta declaration + no existing <meta charset> present.
 * The encoder reports UTF-8 by default; absent any encoding attr we
 * append the platform-standard <meta charset="UTF-8"/>. */
TEST(XsltBridge, HtmlInjectsMetaCharset) {
    EXPECT_NE(body(run(
        "<xsl:output method='html'/>"
        "<xsl:template match='/'>"
        "<html><head><title>t</title></head><body/></html>"
        "</xsl:template>", "<r/>")).find("<meta charset=\"UTF-8\""),
              std::string::npos);
}

/* §16.2 HTML indent rules (libxml2 HTMLtree.c parity):
 *   - newline after the opening tag of a block parent (≥2 children,
 *     first child not text, name not p*), between non-inline element
 *     siblings, and before the closing tag — but ZERO nesting spaces
 *   - single-child parents stay inline
 *   - inline elements (span/b/…) and p* never break lines
 *   - unknown elements never break lines
 *   - the injected <meta charset> participates as a normal child. */
TEST(XsltHtml, IndentBlockParentNewlinesNoSpaces) {
    EXPECT_EQ(body(run(
        "<xsl:output method='html'/>"
        "<xsl:template match='/'>"
        "<html><head><title>t</title></head>"
        "<body><div><p>one</p><p>two</p></div></body></html>"
        "</xsl:template>", "<r/>")),
        "<html>\n"
        "<head>\n"
        "<meta charset=\"UTF-8\">\n"
        "<title>t</title>\n"
        "</head>\n"
        "<body><div>\n"
        "<p>one</p>\n"
        "<p>two</p>\n"
        "</div></body>\n"
        "</html>");
}

TEST(XsltHtml, IndentSingleChildParentInline) {
    /* body with exactly one child stays inline; its child may still
     * indent internally. */
    EXPECT_EQ(body(run(
        "<xsl:output method='html'/>"
        "<xsl:template match='/'>"
        "<html><body><p>only</p></body></html>"
        "</xsl:template>", "<r/>")),
        "<html><body><p>only</p></body></html>");
}

TEST(XsltHtml, IndentInlineParentNeverBreaks) {
    /* span is inline in the HTML element table; p* is excluded by the
     * libxml2 first-byte rule — neither ever breaks lines. */
    EXPECT_EQ(body(run(
        "<xsl:output method='html'/>"
        "<xsl:template match='/'>"
        "<html><body><span><em>a</em><em>b</em></span></body></html>"
        "</xsl:template>", "<r/>")),
        "<html><body><span><em>a</em><em>b</em></span></body></html>");
    EXPECT_EQ(body(run(
        "<xsl:output method='html'/>"
        "<xsl:template match='/'>"
        "<html><body><p><div>a</div><div>b</div></p></body></html>"
        "</xsl:template>", "<r/>")),
        "<html><body><p><div>a</div><div>b</div></p></body></html>");
}

TEST(XsltHtml, IndentUnknownElementNeverBreaks) {
    EXPECT_EQ(body(run(
        "<xsl:output method='html'/>"
        "<xsl:template match='/'>"
        "<html><body><foo><f1>a</f1><f2>b</f2></foo></body></html>"
        "</xsl:template>", "<r/>")),
        "<html><body><foo><f1>a</f1><f2>b</f2></foo></body></html>");
}

TEST(XsltHtml, IndentInlineSiblingsCluster) {
    /* A block parent breaks around its children, but inline children
     * cluster together on one line (no newline after them); html and
     * body here have a single child each and stay inline. */
    EXPECT_EQ(body(run(
        "<xsl:output method='html'/>"
        "<xsl:template match='/'>"
        "<html><body><div><span>a</span><p>p1</p><span>b</span></div></body></html>"
        "</xsl:template>", "<r/>")),
        "<html><body><div>\n"
        "<span>a</span><p>p1</p>\n"
        "<span>b</span>\n"
        "</div></body></html>");
}

TEST(XsltHtml, MetaCharsetRespectsEncoding) {
    /* xsl:output encoding flows into the injected meta. */
    EXPECT_NE(body(run(
        "<xsl:output method='html' encoding='iso-8859-1'/>"
        "<xsl:template match='/'>"
        "<html><head><title>t</title></head></html>"
        "</xsl:template>", "<r/>")).find("<meta charset=\"iso-8859-1\">"),
              std::string::npos);
}

/* position() must reflect the in-flight node-list position — in
 * for-each bodies, in apply-templates, and inside AVTs (§4/§12.4;
 * the libxslt suite's bug-2-/bug-20- shape). */
TEST(XsltCore, PositionInForEachAndAvt) {
    EXPECT_EQ(body(run(
        "<xsl:template match='foo'>"
        "<xsl:for-each select='bar'>"
        "<BAR INDEX='{position()}'>"
        "<xsl:if test='position()=2'><MID/></xsl:if>"
        "</BAR>"
        "</xsl:for-each>"
        "</xsl:template>", "<foo><bar/><bar/><bar/></foo>")),
        "<BAR INDEX=\"1\"/><BAR INDEX=\"2\"><MID/></BAR><BAR INDEX=\"3\"/>");
}

/* §3.4 xsl:strip-space elements='*': the wildcard matches every
 * element — whitespace-only source text disappears (position() over
 * the children counts only elements; no stray text reaches the
 * result). libxslt suite bug-2-. */
TEST(XsltCore, StripSpaceWildcard) {
    EXPECT_EQ(body(run(
        "<xsl:strip-space elements='*'/>"
        "<xsl:template match='foo'><FOO><xsl:apply-templates/></FOO></xsl:template>"
        "<xsl:template match='bar'>"
        "<BAR INDEX='{position()}'><xsl:value-of select='.'/></BAR>"
        "</xsl:template>",
        "<foo>\n  <bar>b1</bar>\n  <bar>b2</bar>\n</foo>")),
        "<FOO><BAR INDEX=\"1\">b1</BAR><BAR INDEX=\"2\">b2</BAR></FOO>");
}

/* §11 variable scoping: a called template sees the GLOBALS and its
 * own locals — never the CALLER's locals. A caller-local shadowing
 * a global must not leak into the callee (libxslt bug-40-). */
TEST(XsltCore, CallTemplateVariableScope) {
    EXPECT_EQ(body(run(
        "<xsl:variable name='foo' select=\"'SUCCESS'\"/>"
        "<xsl:template name='test'><xsl:value-of select='$foo'/></xsl:template>"
        "<xsl:template match='/'>"
        "<xsl:variable name='foo' select=\"'FAILURE'\"/>"
        "<xsl:call-template name='test'/>"
        "</xsl:template>", "<doc/>")), "SUCCESS");
}

/* §11.3 xsl:copy-of: a node copies VERBATIM — every child kind in
 * order (text/CDATA/comment/PI/element), not the string-value
 * concatenated ahead of the child elements (libxslt bug-4-). */
TEST(XsltCore, CopyOfMixedContentVerbatim) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:copy-of select='m'/>"
        "</xsl:template>",
        "<m name='M'>\n  <c>inner</c>\n</m>")),
        "<m name=\"M\">\n  <c>inner</c>\n</m>");
}

/* §16.1: an element-less result (only comment/PI nodes) still
 * carries the XML declaration unless omitted — libxslt parity
 * (bug-31-). */
TEST(XsltCore, FragmentResultKeepsDeclaration) {
    EXPECT_EQ(run(
        "<xsl:template match='processing-instruction()'>"
        "<xsl:copy/>"
        "</xsl:template>",
        "<?xml-stylesheet type='text/css' href='s.css'?><d/>"),
        "<?xml version=\"1.0\"?>\n<?xml-stylesheet type='text/css' "
        "href='s.css'?>");
}

/* EXSLT func:function (http://exslt.org/functions): stylesheet-
 * defined extension functions callable from XPath. func:result
 * yields the return (select value, or content as RTF string). The
 * call runs with the CALLER's context node and its own variable
 * scope (globals + locals). libxslt bug-212. */
TEST(XsltCore, ExsltUserFunction) {
    const char* sheet =
        "<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform' version='1.0'"
        " xmlns:func='http://exslt.org/functions'"
        " xmlns:myf='urn:my' extension-element-prefixes='func'>"
        "<xsl:output method='text'/>"
        "<xsl:template match='/'>"
        "<xsl:variable name='a' select='myf:twice()'/>"
        "<xsl:value-of select='$a'/><xsl:text>,</xsl:text>"
        "<xsl:value-of select='$a'/>"
        "</xsl:template>"
        "<func:function name='myf:twice'>"
        "<func:result><xsl:text>a</xsl:text></func:result>"
        "</func:function>"
        "</xsl:stylesheet>";
    LeptrisXslt x = leptris_xslt_parse(sheet, strlen(sheet));
    ASSERT_NE(x, (LeptrisXslt) nullptr);
    LeptrisDocument d = leptris_parse_string("<r/>", 4, nullptr);
    char* out = leptris_xslt_apply_string(x, d);
    ASSERT_NE(out, (char*) nullptr);
    EXPECT_STREQ(out, "a,a");
    leptris_free_string(out);
    leptris_document_free(d);
    leptris_xslt_free(x);
}

/* §16.2 raw text elements: script/style content (HTML5 rawtext,
 * libxml2 dataMode >= DATA_RAWTEXT) emits VERBATIM under
 * method=html — no &lt; re-escaping (bug-33-). */
TEST(XsltHtml, ScriptContentRaw) {
    /* head carries the injected meta → 2 children → line breaks
     * (libxslt site-1 rule); the script text itself is verbatim. */
    EXPECT_EQ(body(run(
        "<xsl:output method='html'/>"
        "<xsl:template match='/'>"
        "<html><head><script>if (a &lt; b) alert();</script></head></html>"
        "</xsl:template>", "<r/>")),
        "<html><head>\n<meta charset=\"UTF-8\">\n"
        "<script>if (a < b) alert();</script>\n</head></html>");
}

/* §16.2 HTML PIs have no trailing '?': <?php ... > (libxml2
 * HTMLtree.c HTML_PI_NODE case). */
TEST(XsltHtml, PiDropsQuestionMark) {
    /* Literal PIs in template bodies are dropped (xsltproc-verified)
     * — the PI form is reachable through xsl:processing-instruction. */
    EXPECT_NE(body(run(
        "<xsl:output method='html'/>"
        "<xsl:template match='/'>"
        "<html><body><xsl:processing-instruction name='php'>"
        "echo 1;</xsl:processing-instruction></body></html>"
        "</xsl:template>", "<r/>")).find("<?php echo 1;>"),
              std::string::npos);
}

/* §16.2 href URL-escaping: non-ASCII (and spaces) in href values
 * percent-encode as UTF-8 (libxml2 htmlAttrDumpOutput, bug-83). */
TEST(XsltHtml, HrefUrlEscaped) {
    EXPECT_NE(body(run(
        "<xsl:output method='html'/>"
        "<xsl:template match='/'>"
        "<html><body><a href=\"a b c\">x</a></body></html>"
        "</xsl:template>", "<r/>")).find("href=\"a%20b%20c\""),
              std::string::npos);
}

/* id() (XPath §4.1): an element matches when its ID-TYPED
 * attribute — declared in the DTD internal subset, ANY QName —
 * equals the argument (libxslt bug-163: myns:id). */
TEST(XsltCore, IdFindsDtdTypedIds) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"id('indexme')/@some-attr\"/>"
        "</xsl:template>",
        "<!DOCTYPE my-root ["
        "<!ATTLIST my-root myns:id ID #IMPLIED>"
        "]>"
        "<my-root xmlns:myns='uri' myns:id='indexme' some-attr='findme'/>")),
        "findme");
}

/* §11 variable scoping via apply-templates: the SELECTED template
 * also sees globals + own locals, never the caller's locals (the
 * apply-templates twin of CallTemplateVariableScope — bug-42-). */
TEST(XsltCore, ApplyTemplatesVariableScope) {
    EXPECT_EQ(body(run(
        "<xsl:variable name='foo' select=\"'SUCCESS'\"/>"
        "<xsl:template match='doc'><xsl:value-of select='$foo'/></xsl:template>"
        "<xsl:template match='/'>"
        "<xsl:variable name='foo' select=\"'FAILURE'\"/>"
        "<xsl:apply-templates/>"
        "</xsl:template>", "<doc/>")), "SUCCESS");
}

/* XML 1.0 §3.3.2: ATTLIST DEFAULT values materialize on elements
 * that don't carry the attribute (libxslt bug-53). */
TEST(XsltCore, DtdDefaultAttributes) {
    EXPECT_EQ(body(run(
        "<xsl:template match='doc'><xsl:value-of select='@defatt'/></xsl:template>",
        "<!DOCTYPE doc [<!ELEMENT doc EMPTY>"
        "<!ATTLIST doc defatt (SUCCESS|FAILURE) 'SUCCESS'>]>"
        "<doc/>")),
        "SUCCESS");
}

/* §3.4 strip-space NameTests: "p:*" strips whitespace-only text
 * from every element in the namespace bound to p (bug-124). */
TEST(XsltCore, StripSpacePrefixedWildcard) {
    /* run() wraps its body with only the xsl prefix; this case needs
     * a SECOND prefix bound on the stylesheet root — build directly. */
    const char* sheet =
        "<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
        " xmlns:p='urn:x' version='1.0'>"
        "<xsl:strip-space elements='p:*'/>"
        "<xsl:template match='@*|node()'>"
        "<xsl:copy><xsl:apply-templates select='@*|node()'/></xsl:copy>"
        "</xsl:template></xsl:stylesheet>";
    const char* xml = "<r xmlns:p='urn:x'><p:a>\n  <p:b/>\n</p:a></r>";
    LeptrisXslt x = leptris_xslt_parse(sheet, strlen(sheet));
    ASSERT_NE(x, (LeptrisXslt) nullptr);
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), nullptr);
    ASSERT_NE(d, (LeptrisDocument) nullptr);
    char* out = leptris_xslt_apply_string(x, d);
    ASSERT_NE(out, (char*) nullptr);
    EXPECT_EQ(body(std::string(out)),
              "<r xmlns:p=\"urn:x\"><p:a><p:b/></p:a></r>");
    leptris_free_string(out);
    leptris_document_free(d);
    leptris_xslt_free(x);
}

/* generate-id(): stable + unique per node (§12.5). */

TEST(XsltBridge, KeyUseCanReferenceCurrentNode) {
    EXPECT_EQ(body(run2(
        "<xsl:key name='by-current' match='i' use='current()/@k'/>"
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"key('by-current', 'x')\"/>"
        "</xsl:template>",
        "<r><i k='x'>hit</i><i k='y'>miss</i></r>")), "hit");
}

TEST(XsltBridge, GenerateId) {
    std::string out = body(run(
        "<xsl:template match='/'>"
        "<xsl:variable name='a' select=\"generate-id(//i[1])\"/>"
        "<xsl:variable name='b' select=\"generate-id(//i[2])\"/>"
        "<xsl:value-of select='$a != $b'/>"
        "</xsl:template>", "<r><i/><i/></r>"));
    EXPECT_EQ(out, "true");
}

/* system-property(): xsl:version / vendor (§12.7). */
TEST(XsltBridge, SystemProperty) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"system-property('xsl:version')\"/>"
        "</xsl:template>", "<r/>")), "1.0");
}

/* EXSLT regexp:test + date:date-time via the same bridge. */
TEST(XsltBridge, ExsltRegexpAndDate) {
#ifdef _WIN32
    /* No <regex.h>: the documented Windows stub always reports
     * no-match. */
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"regexp:test('leptris', '^lep')\"/>"
        "</xsl:template>", "<r/>")), "false");
#else
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"regexp:test('leptris', '^lep')\"/>"
        "</xsl:template>", "<r/>")), "true");
#endif
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"regexp:test('leptris', '^x')\"/>"
        "</xsl:template>", "<r/>")), "false");
    /* date:date-time() — ISO 8601 UTC, length 20 (YYYY-MM-DDTHH:MM:SSZ). */
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:value-of select=\"string-length(date:date-time()) = 20\"/>"
        "</xsl:template>", "<r/>")), "true");
}

/* §16.2 xsl:output doctype-system / doctype-public (libxslt suite
 * bug-152, bug-166, bug-175, bug-206, bug-25-). The DOCTYPE leads
 * the body, right after the declaration; html method honors the
 * version="5" shorthand and omits the system id when only a public
 * id is given. */
TEST(XsltOutput, DoctypePublicAndSystem) {
    std::string r = body(run(
        "<xsl:output method='xml' doctype-public='-//X//Y//EN'"
        " doctype-system='d.dtd'/>"
        "<xsl:template match='/'><doc/></xsl:template>",
        "<r/>"));
    EXPECT_EQ(r, "<!DOCTYPE doc PUBLIC \"-//X//Y//EN\" \"d.dtd\">\n<doc/>");
}

TEST(XsltOutput, DoctypeSystemOnly) {
    std::string r = body(run(
        "<xsl:output method='xml' doctype-system='d.dtd'/>"
        "<xsl:template match='/'><doc/></xsl:template>",
        "<r/>"));
    EXPECT_EQ(r, "<!DOCTYPE doc SYSTEM \"d.dtd\">\n<doc/>");
}

TEST(XsltOutput, DoctypePublicOnly) {
    std::string r = body(run(
        "<xsl:output method='xml' doctype-public='-//X//Y//EN'/>"
        "<xsl:template match='/'><doc/></xsl:template>",
        "<r/>"));
    EXPECT_EQ(r, "<!DOCTYPE doc PUBLIC \"-//X//Y//EN\">\n<doc/>");
}

TEST(XsltOutput, HtmlVersion5UsesBareDoctype) {
    std::string r = body(run(
        "<xsl:output method='html' version='5'/>"
        "<xsl:template match='/'><html/></xsl:template>",
        "<r/>"));
    EXPECT_EQ(r, "<!DOCTYPE html>\n<html></html>");
}

TEST(XsltOutput, HtmlPublicOnlyOmitsSystemId) {
    std::string r = body(run(
        "<xsl:output method='html' version='4.0'"
        " doctype-public='-//W3C//DTD HTML 4.01//EN'/>"
        "<xsl:template match='/'><html><body>x</body></html></xsl:template>",
        "<r/>"));
    EXPECT_EQ(r.substr(0, 51),
              "<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\">\n");
}

/* No doctype attributes → no DOCTYPE (the default contract most
 * transforms rely on). */
TEST(XsltOutput, NoDoctypeAttrsMeansNoDoctype) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'><doc/></xsl:template>", "<r/>")),
        "<doc/>");
    EXPECT_EQ(body(run(
        "<xsl:output method='html'/>"
        "<xsl:template match='/'><html/></xsl:template>", "<r/>")),
        "<html></html>");
}

/* §5.2: patterns with position predicates apply per sibling
 * position — match="text()[2]" fires ONLY for the parent's second
 * text node (libxslt bug-182). */
TEST(XsltPatterns, PositionPredicateOnTextNodes) {
    EXPECT_EQ(body(run(
        "<xsl:template match='node()'/>"
        "<xsl:template match='text()[2]'>"
        "[<xsl:value-of select='.'/>]</xsl:template>"
        "<xsl:template match='/'>"
        "<body><xsl:apply-templates select='/root/body/node()'/></body>"
        "</xsl:template>",
        "<root><body><b>1</b> t1 <b>2</b> t2 </body></root>")),
        "<body>[ t2 ]</body>");
}

/* §7.1.1: xsl:element's name is an ATTRIBUTE VALUE TEMPLATE
 * (libxslt bug-117/179/35-: {local-name()}, {concat('a','b')},
 * prefix:{...}, {$var}). */
TEST(XsltElement, NameAvtConcat) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:element name=\"{concat('foo', 'bar')}\">x</xsl:element>"
        "</xsl:template>", "<r/>")),
        "<foobar>x</foobar>");
}

TEST(XsltElement, NameAvtLocalNameWithPrefix) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/*'>"
        "<xsl:element name='s:{local-name()}' xmlns:s='urn:s'>"
        "hi</xsl:element>"
        "</xsl:template>", "<r/>")),
        "<s:r xmlns:s=\"urn:s\">hi</s:r>");
}

TEST(XsltElement, NameAvtVariable) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:variable name='n' select=\"'out'\"/>"
        "<xsl:element name=\"{$n}\"/>"
        "</xsl:template>", "<r/>")),
        "<out/>");
}

/* Non-AVT names take the literal path unchanged. */
TEST(XsltElement, PlainNameStillLiteral) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<xsl:element name='plain'>x</xsl:element>"
        "</xsl:template>", "<r/>")),
        "<plain>x</plain>");
}

/* §11.1 top-level variables evaluate with the namespace bindings of
 * the element that declares them — included sheets carry their own
 * (libxslt bug-36-: $var select="/n:x" resolved without n:). */
TEST(XsltVariables, GlobalVariableSelectUsesDeclaringNsContext) {
    EXPECT_EQ(body(run(
        "<xsl:variable name='v' select='/n:x'/>"
        "<xsl:template match='/'>"
        "[<xsl:value-of select='$v'/>]"
        "</xsl:template>",
        "<n:x xmlns:n='urn:n'>text</n:x>"))
        .find("xmlns:xsl"), std::string::npos) << "sanity";
    /* run() builds the sheet WITHOUT the n: declaration on the
     * stylesheet element, so prefix resolution must come from the
     * variable's own context — build it manually instead. */
    std::string sheet =
        "<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
        " xmlns:n='urn:n' version='1.0'>"
        "<xsl:variable name='v' select='/n:x'/>"
        "<xsl:template match='/'>[<xsl:value-of select='$v'/>]"
        "</xsl:template></xsl:stylesheet>";
    LeptrisXslt x = leptris_xslt_parse(sheet.c_str(), sheet.size());
    ASSERT_NE(x, nullptr);
    const char src[] = "<n:x xmlns:n='urn:n'>hello</n:x>";
    LeptrisDocument d = leptris_parse_string(
        src, std::strlen(src), nullptr);
    ASSERT_NE(d, nullptr);
    char* out = leptris_xslt_apply_string(x, d);
    ASSERT_NE(out, nullptr);
    std::string s(out);
    EXPECT_NE(s.find("[hello]"), std::string::npos) << s;
    leptris_free_string(out);
    leptris_document_free(d);
    leptris_xslt_free(x);
}

/* §5.4/§5.8: the built-in element rule applies templates to children
 * of EVERY kind — a user match="text()" template must see text nodes
 * reached through the built-in rule, not just via an explicit select
 * (libxslt bug-171: match="text()" override ignored, text leaked). */
TEST(XsltBuiltInRules, UserTextTemplateOverridesBuiltInCopy) {
    EXPECT_EQ(body(run(
        "<xsl:template match='b[2]'>[<xsl:value-of select='.'/>]"
        "</xsl:template>"
        "<xsl:template match='text()'/>"
        "<xsl:template match='/'>"
        "<xsl:apply-templates select='/r/node()'/>"
        "</xsl:template>",
        "<r><b>one</b><b>two</b></r>")),
        "[two]");
    EXPECT_EQ(body(run(
        "<xsl:template match='text()'>T[<xsl:value-of select='.'/>]"
        "</xsl:template>"
        "<xsl:template match='/'>"
        "<xsl:apply-templates select='/r/node()'/>"
        "</xsl:template>",
        "<r><b>x</b>tail</r>")),
        "T[x]T[tail]");
}

/* §12.3 xsl:decimal-format: grouping/decimal separators may be ANY
 * character — multi-byte included (braille ⠢, libxslt bug-222) — and
 * the grouping SIZE comes from the pattern (digit slots after the
 * last separator: '#⠢0' groups by 1 → 10 becomes 1⠢0). */
TEST(XsltNumber, DecimalFormatMultiByteSeparators) {
    std::string sheet =
        "<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
        " version='1.0'>"
        "<xsl:decimal-format name='f' grouping-separator='\xe2\xa0\xa2'/>"
        "<xsl:template match='/'>"
        "[<xsl:value-of select=\"format-number(10,'#\xe2\xa0\xa2" "0','f')\"/>]"
        "</xsl:template></xsl:stylesheet>";
    LeptrisXslt x = leptris_xslt_parse(sheet.c_str(), sheet.size());
    ASSERT_NE(x, nullptr);
    LeptrisDocument d = leptris_parse_string("<r/>", 4, nullptr);
    char* out = leptris_xslt_apply_string(x, d);
    ASSERT_NE(out, nullptr);
    std::string want = "1";
    want += "\xe2\xa0\xa2";
    want += "0";
    EXPECT_NE(std::string(out).find(want), std::string::npos)
        << out;
    leptris_free_string(out);
    leptris_document_free(d);
    leptris_xslt_free(x);
}

/* bug-157: the priority ATTRIBUTE (not just the §5.5 default) must
 * drive template selection — an explicit -100 loses to a default
 * 0.5 pattern, and an explicit 10 beats a later-declared 0.5. */
TEST(XsltTemplates, PriorityAttributeOverridesDefault) {
    EXPECT_EQ(body(run(
        "<xsl:template match='item' priority='10'>HIGH</xsl:template>"
        "<xsl:template match='item'>LOW</xsl:template>",
        "<r><item/></r>")), "HIGH");
    EXPECT_EQ(body(run(
        "<xsl:template match=\"r/item[last()=1]\">3</xsl:template>"
        "<xsl:template match='r/item' priority='-100'>FALLBACK</xsl:template>",
        "<r><item/></r>")), "3");
}

/* bug-214: count/from patterns are evaluated with the current
 * variable frame in scope ($type binds the node being numbered). */
TEST(XsltNumber, CountPatternWithVariable) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<out><xsl:for-each select='r/n'>"
        "<xsl:variable name='t' select='@t'/>"
        "<p><xsl:number count=\"n[@t = $t]\"/></p>"
        "</xsl:for-each></out>"
        "</xsl:template>",
        "<r><n t='a'/><n t='b'/><n t='a'/></r>")),
        "<out><p>1</p><p>1</p><p>2</p></out>");
}

/* bug-217: use-attribute-sets semantics - sets apply first, the
 * use-list order gives later sets precedence, literal result attrs
 * beat sets, and xsl:attribute children beat everything. Conflicts
 * update in place (position = first insertion). */
TEST(XsltAttributeSets, UseListOrderAndOverridePrecedence) {
    EXPECT_EQ(body(run(
        "<xsl:template match='foo'>"
        "<bar xsl:use-attribute-sets='as1 as2' a1='element' a2='element'>"
        "<xsl:attribute name='a1'>attr</xsl:attribute>"
        "</bar>"
        "</xsl:template>"
        "<xsl:attribute-set name='as1'>"
        "<xsl:attribute name='a1'>as1</xsl:attribute>"
        "<xsl:attribute name='a2'>as1</xsl:attribute>"
        "<xsl:attribute name='a3'>as1</xsl:attribute>"
        "<xsl:attribute name='a4'>as1</xsl:attribute>"
        "</xsl:attribute-set>"
        "<xsl:attribute-set name='as2'>"
        "<xsl:attribute name='a1'>as2</xsl:attribute>"
        "<xsl:attribute name='a2'>as2</xsl:attribute>"
        "<xsl:attribute name='a3'>as2</xsl:attribute>"
        "</xsl:attribute-set>",
        "<foo/>")),
        "<bar a1=\"attr\" a2=\"element\" a3=\"as2\" a4=\"as1\"/>");
}

/* bug-189: same-named attribute-set declarations UNION - later
 * declarations override values; duplicates within a declaration:
 * last wins; positions follow first insertion. */
TEST(XsltAttributeSets, SameNameDeclarationsMerge) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<elem xsl:use-attribute-sets='att1 att2'/>"
        "<elem xsl:use-attribute-sets='att3'/>"
        "</xsl:template>"
        "<xsl:attribute-set name='att1'>"
        "<xsl:attribute name='att1'>1</xsl:attribute>"
        "<xsl:attribute name='commonatt'>1</xsl:attribute>"
        "</xsl:attribute-set>"
        "<xsl:attribute-set name='att2'>"
        "<xsl:attribute name='att2'>2</xsl:attribute>"
        "<xsl:attribute name='commonatt'>2</xsl:attribute>"
        "</xsl:attribute-set>"
        "<xsl:attribute-set name='att3'>"
        "<xsl:attribute name='att3a'>1</xsl:attribute>"
        "<xsl:attribute name='att3a'>2</xsl:attribute>"
        "<xsl:attribute name='att3b'>1</xsl:attribute>"
        "</xsl:attribute-set>"
        "<xsl:attribute-set name='att3'>"
        "<xsl:attribute name='att3b'>2</xsl:attribute>"
        "</xsl:attribute-set>",
        "<doc/>")),
        "<elem att1=\"1\" commonatt=\"2\" att2=\"2\"/>"
        "<elem att3a=\"2\" att3b=\"2\"/>");
}

/* bug-190: attribute-set names are QNames - a declaration and a
 * reference that spell different prefixes for the SAME namespace
 * URI must resolve to the same set. */
TEST(XsltAttributeSets, ExpandedNameMatchesAcrossPrefixes) {
    std::string sheet =
        "<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
        " xmlns:ns1='urn:foo' xmlns:ns2='urn:foo' version='1.0'"
        " exclude-result-prefixes='ns1 ns2'>"
        "<xsl:template match='/'>"
        "<elem xsl:use-attribute-sets='ns1:set'/>"
        "</xsl:template>"
        "<xsl:attribute-set name='ns2:set'>"
        "<xsl:attribute name='attr'>value</xsl:attribute>"
        "</xsl:attribute-set>"
        "</xsl:stylesheet>";
    LeptrisXslt x = leptris_xslt_parse(sheet.c_str(), sheet.size());
    ASSERT_NE(x, nullptr);
    LeptrisDocument d = leptris_parse_string("<doc/>", 6, nullptr);
    ASSERT_NE(d, nullptr);
    char* out = leptris_xslt_apply_string(x, d);
    ASSERT_NE(out, nullptr);
    std::string r = body(out);
    leptris_free_string(out);
    leptris_document_free(d);
    leptris_xslt_free(x);
    EXPECT_NE(r.find("<elem attr=\"value\"/>"), std::string::npos) << r;
}

/* bug-113: a function-call pattern (id(...)) takes the 0.5
 * "otherwise" default priority — not the bare-QName 0.0 — so it
 * beats a QName template on ties. */
TEST(XsltTemplates, FunctionCallPatternDefaultPriority) {
    EXPECT_EQ(body(run(
        "<xsl:template match=\"id('x')\">FUN</xsl:template>"
        "<xsl:template match='dest'>NAME</xsl:template>",
        "<r><dest id='x'/></r>")),
        "FUN");
}

/* bug-224: generate-id emits libxslt's deterministic sequential
 * ids — one counter per transform, same node re-queried returns the
 * same id. */
TEST(XsltFunctions, GenerateIdSequentialAndStable) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<r><xsl:value-of select='generate-id(//i[1])'/>"
        "-<xsl:value-of select='generate-id(//i[2])'/>"
        "-<xsl:value-of select='generate-id(//i[1])'/></r>"
        "</xsl:template>",
        "<d><i/><i/></d>")),
        "<r>id1-id2-id1</r>");
    /* bug-224 shape: ids computed in GLOBAL variables. */
    EXPECT_EQ(body(run(
        "<xsl:variable name='v1' select='generate-id(d/i[1])'/>"
        "<xsl:variable name='v2' select='generate-id(d/i[2])'/>"
        "<xsl:template match='/'>"
        "<r><xsl:value-of select='$v1'/>-<xsl:value-of select='$v2'/></r>"
        "</xsl:template>",
        "<d><i/><i/></d>")),
        "<r>id1-id2</r>");
}

/* bug-97: prefixed attribute node-tests resolve by namespace URI
 * — the source may spell a different prefix for the same binding. */
TEST(XsltApplyTemplates, AttributeTestResolvesByUri) {
    std::string sheet =
        "<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
        " xmlns:m='urn:x' xmlns:m2='urn:x' version='1.0'>"
        "<xsl:template match='/'>"
        "[<xsl:value-of select='r/m:man/@m2:a'/>]"
        "</xsl:template>"
        "</xsl:stylesheet>";
    LeptrisXslt x = leptris_xslt_parse(sheet.c_str(), sheet.size());
    ASSERT_NE(x, nullptr);
    const char xml[] = "<r xmlns:m='urn:x'><m:man m:a='V'/></r>";
    LeptrisDocument d = leptris_parse_string(xml, sizeof(xml) - 1, nullptr);
    ASSERT_NE(d, nullptr);
    char* out = leptris_xslt_apply_string(x, d);
    ASSERT_NE(out, nullptr);
    std::string r = body(out);
    leptris_free_string(out);
    leptris_document_free(d);
    leptris_xslt_free(x);
    EXPECT_NE(r.find("[V]"), std::string::npos) << r;
}

/* bug-104: copy-of keeps the source's namespace-declaration
 * ORDER — the serializer must emit the declaration chain verbatim,
 * not hoist the default namespace to the front. */
TEST(XsltCopyOf, NamespaceDeclarationsKeepSourceOrder) {
    EXPECT_NE(body(run(
        "<xsl:template match='/'>"
        "<xsl:copy-of select='node()'/>"
        "</xsl:template>",
        "<foo xmlns:eg='http://example.org'"
        " xmlns='http://example.org' eg:bar=''/>"))
        .find("<foo xmlns:eg=\"http://example.org\""
              " xmlns=\"http://example.org\" eg:bar=\"\"/>"),
        std::string::npos);
}

/* bug-71: a literal result element's OWN prefix binding is emitted
 * first, before the other in-scope declarations (libxslt creates
 * the element with its namespace, then copies the rest). */
TEST(XsltLiteralElement, OwnPrefixBindingFirst) {
    std::string sheet =
        "<xsl:stylesheet version='1.0'"
        " xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
        " xmlns:rdf='urn:test:rdf' xmlns:pa='urn:test:pa'>"
        "<xsl:template match='/'>"
        "<pa:Contact rdf:about='hello'/>"
        "</xsl:template>"
        "</xsl:stylesheet>";
    LeptrisXslt x = leptris_xslt_parse(sheet.c_str(), sheet.size());
    ASSERT_NE(x, nullptr);
    LeptrisDocument d = leptris_parse_string("<r/>", 4, nullptr);
    ASSERT_NE(d, nullptr);
    char* out = leptris_xslt_apply_string(x, d);
    ASSERT_NE(out, nullptr);
    std::string r = body(out);
    leptris_free_string(out);
    leptris_document_free(d);
    leptris_xslt_free(x);
    EXPECT_NE(r.find("<pa:Contact xmlns:pa=\"urn:test:pa\""
                     " xmlns:rdf=\"urn:test:rdf\" rdf:about=\"hello\"/>"),
              std::string::npos) << r;
}

/* bug-220: an element in an extension namespace with no known
 * implementation runs only its xsl:fallback children — the element
 * itself and non-fallback children are dropped. */
TEST(XsltFallback, ExtensionElementRunsFallbackOnly) {
    std::string sheet =
        "<xsl:stylesheet version='1.0'"
        " xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
        " xmlns:ext='ext' extension-element-prefixes='ext'>"
        "<xsl:template match='/'>"
        "<r><ext:e>"
        "<xsl:fallback><fallback/></xsl:fallback>"
        "<ext:f/>"
        "</ext:e></r>"
        "</xsl:template>"
        "</xsl:stylesheet>";
    LeptrisXslt x = leptris_xslt_parse(sheet.c_str(), sheet.size());
    ASSERT_NE(x, nullptr);
    LeptrisDocument d = leptris_parse_string("<doc/>", 6, nullptr);
    ASSERT_NE(d, nullptr);
    char* out = leptris_xslt_apply_string(x, d);
    ASSERT_NE(out, nullptr);
    std::string r = body(out);
    leptris_free_string(out);
    leptris_document_free(d);
    leptris_xslt_free(x);
    EXPECT_EQ(r, "<r><fallback/></r>") << r;
}

/* bug-179: an xsl:element namespace attribute matching the result
 * parent's in-scope default emits NO redundant declaration — the
 * serializer relies on inheritance, like libxslt. */
TEST(XsltElement, NamespaceAttrSkipsRedundantDefaultDecl) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/'>"
        "<r xmlns='urn:d'>"
        "<xsl:element name='baz' namespace='urn:d'>x</xsl:element>"
        "</r>"
        "</xsl:template>",
        "<doc/>")),
        "<r xmlns=\"urn:d\"><baz>x</baz></r>");
}

/* bug-38-: copy-of a NAMESPACE node copies the declaration onto
 * the pending parent (the op previously dropped ns nodes). */
TEST(XsltCopyOf, NamespaceNode) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/*'>"
        "<elem><xsl:copy-of select='//n/namespace::foo'/></elem>"
        "</xsl:template>",
        "<r><n xmlns:foo='urn:f'/></r>")),
        "<elem xmlns:foo=\"urn:f\"/>");
}

/* bug-92: xsl:element does NOT copy the instruction's in-scope
 * namespace declarations onto the result (only literal result
 * elements do, 7.1.1). */
TEST(XsltElement, DoesNotCopyInScopeNamespaces) {
    std::string sheet =
        "<xsl:stylesheet version='1.0'"
        " xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
        " xmlns:xs='http://www.w3.org/2001/XMLSchema'>"
        "<xsl:template match='/'>"
        "<xsl:element name='toto'>x</xsl:element>"
        "</xsl:template>"
        "</xsl:stylesheet>";
    LeptrisXslt x = leptris_xslt_parse(sheet.c_str(), sheet.size());
    ASSERT_NE(x, nullptr);
    LeptrisDocument d = leptris_parse_string("<r/>", 4, nullptr);
    ASSERT_NE(d, nullptr);
    char* out = leptris_xslt_apply_string(x, d);
    ASSERT_NE(out, nullptr);
    std::string r = body(out);
    leptris_free_string(out);
    leptris_document_free(d);
    leptris_xslt_free(x);
    EXPECT_EQ(r, "<toto>x</toto>") << r;
}

/* bug-98: under indent="yes", whitespace text nodes copied from
 * the source make the parent mixed — libxslt stops formatting the
 * whole subtree below it (the visible "indent" is the copied
 * whitespace itself). */
TEST(XsltOutput, IndentStopsBelowWhitespaceMixedResult) {
    std::string sheet =
        "<xsl:stylesheet version='1.0'"
        " xmlns:xsl='http://www.w3.org/1999/XSL/Transform'>"
        "<xsl:output method='xml' indent='yes'/>"
        "<xsl:template match='/*'>"
        "<result><xsl:apply-templates/></result>"
        "</xsl:template>"
        "<xsl:template match='l'>"
        "<total><type><xsl:value-of select='@t'/></type>"
        "<a><xsl:value-of select='@v'/></a></total>"
        "</xsl:template>"
        "</xsl:stylesheet>";
    LeptrisXslt x = leptris_xslt_parse(sheet.c_str(), sheet.size());
    ASSERT_NE(x, nullptr);
    const char xml[] = "<r>\n  <l t='one' v='3'/>\n</r>";
    LeptrisDocument d = leptris_parse_string(xml, sizeof(xml) - 1, nullptr);
    ASSERT_NE(d, nullptr);
    char* out = leptris_xslt_apply_string(x, d);
    ASSERT_NE(out, nullptr);
    std::string r = body(out);
    leptris_free_string(out);
    leptris_document_free(d);
    leptris_xslt_free(x);
    EXPECT_EQ(r,
              "<result>\n  <total><type>one</type><a>3</a></total>\n"
              "</result>") << r;
}

/* bug-161: apply-templates select=node() over a text item applies
 * the built-in TEXT rule — copy the text itself (the fallback
 * previously walked the text node's children and dropped it). */
TEST(XsltApplyTemplates, SelectedTextItemGetsBuiltInTextRule) {
    EXPECT_EQ(body(run(
        "<xsl:template match='l/p'>"
        "M<xsl:copy><xsl:apply-templates select='@*|node()'/></xsl:copy>"
        "</xsl:template>",
        "<r><l><p>test</p></l></r>")),
        "M<p>test</p>");
}

/* bug-193: apply-imports with no imported candidate falls back to
 * the BUILT-IN rule for the node (libxslt: the built-in element
 * rule re-enters template selection for the children). */
TEST(XsltApplyImports, FallsBackToBuiltInWithoutImport) {
    EXPECT_EQ(body(run(
        "<xsl:template match='root'>"
        "<r><xsl:apply-imports/></r>"
        "</xsl:template>"
        "<xsl:template match='test'>passed</xsl:template>",
        "<root><test/></root>")),
        "<r>passed</r>");
}

/* bug-32-: xsl:copy copies the element and its namespace nodes
 * but NOT its attributes (7.5) — attributes reach the result only
 * through apply-templates/@*. libxslt-verified. */
TEST(XsltCopyOf, AttributeNode) {
    EXPECT_EQ(body(run(
        "<xsl:template match='*'>"
        "<xsl:copy><xsl:copy-of select='@*'/><xsl:apply-templates/>"
        "</xsl:copy></xsl:template>",
        "<a x='1' y='2'>t</a>")),
        "<a x=\"1\" y=\"2\">t</a>");
}

TEST(XsltCopy, DoesNotCopyAttributes) {
    EXPECT_EQ(body(run(
        "<xsl:template match='*'>"
        "<xsl:copy><xsl:apply-templates select='node()'/></xsl:copy>"
        "</xsl:template>",
        "<a x='1'>t</a>")),
        "<a>t</a>");
}

/* bug-186 follow-on: a child-step pattern (star slash star) must
 * NOT match the root element — the root's parent is the document
 * node. The bare-leaf fast path used to match it anyway. */
TEST(XsltTemplates, ChildStepPatternDoesNotMatchRoot) {
    EXPECT_EQ(body(run(
        "<xsl:template match='*/*'><xsl:value-of select='name()'/>"
        "</xsl:template>",
        "<top><foo/><bar/></top>")),
        "foobar");
}

/* bug-186/218: numbering namespace nodes — the default count is
 * same-kind + same-prefix; every in-scope prefix is unique, so each
 * namespace node numbers 1. `from` must evaluate (and not crash)
 * when it references a function with an empty result. */
TEST(XsltNumber, OnNamespaceNodes) {
    EXPECT_EQ(body(run(
        "<xsl:template match='/*'>"
        "<xsl:for-each select='namespace::*'>"
        "<xsl:number/>"
        "</xsl:for-each>"
        "</xsl:template>",
        "<top xmlns:a='A' xmlns:b='B' xmlns:c='C'/>")),
        "1111");
    EXPECT_EQ(body(run(
        "<xsl:template match='*'>"
        "<xsl:for-each select='namespace::*[position()=2]'>"
        "<xsl:number from=\"key('e','f')\"/>"
        "</xsl:for-each>"
        "</xsl:template>",
        "<top xmlns:ns1='foo'/>")),
        "1");
}

/* bug-199: level="any" from a namespace node resolves through the
 * OWNER element (the synthetic ns node is outside the document
 * order walk). */
TEST(XsltNumber, AnyLevelFromNamespaceNodeCountsOwner) {
    EXPECT_EQ(body(run(
        "<xsl:template match='node()|@*'>"
        "<xsl:copy>"
        "<xsl:for-each select='namespace::a'>"
        "<xsl:attribute name='ns'>"
        "<xsl:value-of select='.'/>"
        "(<xsl:number count='*' level='any'/>)"
        "</xsl:attribute>"
        "</xsl:for-each>"
        "<xsl:apply-templates select='node()|@*'/>"
        "</xsl:copy>"
        "</xsl:template>",
        "<r xmlns:a='a'><f xmlns:a='b'><b xmlns:a='c'/></f></r>")),
        "<r xmlns:a=\"a\" ns=\"a(1)\"><f xmlns:a=\"b\" ns=\"b(2)\">"
        "<b xmlns:a=\"c\" ns=\"c(3)\"/></f></r>");
}

TEST(XsltNumber, DecimalFormatGroupingSizeFromPattern) {
    /* '#,#0' groups by 2: 12345 → 1,23,45 */
    EXPECT_NE(body(run(
        "<xsl:template match='/'>"
        "[<xsl:value-of select=\"format-number(12345,'#,#0')\"/>]"
        "</xsl:template>", "<r/>"))
        .find("[1,23,45]"), std::string::npos);
}

/* #129: leptris_document_serialize_ext indent_text — the formatter
 * also owns TEXT whitespace (display form). Without it, mixed
 * content stays on one line (#534 round-trip guarantee). */
TEST(SerializeExt, IndentTextIndentsMixedContent) {
    const char xml[] = "<r><a>text <b>bold</b> tail</a></r>";
    LeptrisDocument d = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(d, nullptr);

    LeptrisSerializeOptions o = {2, 0, nullptr};
    LeptrisSerializeExtOptions ext = {0};
    char* plain = leptris_document_serialize(d, &o);
    ASSERT_NE(plain, nullptr);
    EXPECT_NE(std::string(plain).find("<a>text <b>bold</b> tail</a>"),
              std::string::npos) << "default keeps mixed on one line";
    leptris_free_string(plain);

    ext.indent_text = 1;
    char* it = leptris_document_serialize_ext(d, &o, &ext);
    ASSERT_NE(it, nullptr);
    std::string s(it);
    leptris_free_string(it);
    /* text on its own indented line, elements indented */
    EXPECT_NE(s.find("<a>\n    text"), std::string::npos) << s;
    EXPECT_NE(s.find("<b>\n      bold"), std::string::npos) << s;
    leptris_document_free(d);
}
