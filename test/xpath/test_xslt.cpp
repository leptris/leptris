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
    if (s.compare(0, strlen(decl), decl) == 0) return s.substr(strlen(decl));
    return s;
}
}  // namespace

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
        "<item id=\"1\" extra=\"e\">t</item>");
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
