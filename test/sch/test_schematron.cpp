/* Lane 16: native Schematron (2025-edition subset) — schema IR +
 * SVRL-producing evaluator on the engine's XPath. Falsifiability:
 * validity verdicts AND the SVRL shapes (failed-assert @test,
 * svrl:text) are asserted; violation paths (bad schema, unknown
 * queryBinding) must fail loudly. */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
}
#include <cstring>
#include <string>

namespace {

const char kSchema[] =
    "<schema xmlns='http://purl.oclc.org/dsdl/schematron'"
    " queryBinding='xslt'>"
    "<pattern id='p1'>"
    "<rule context='item'>"
    "<assert test='@price > 0' role='fatal'>price must be"
    " positive</assert>"
    "<report test='@price > 100'>price is high</report>"
    "</rule>"
    "<rule context='catalog'>"
    "<assert test='count(item) > 0'>catalog needs items</assert>"
    "</rule>"
    "</pattern>"
    "</schema>";

LeptrisDocument P(const char* xml) {
    LeptrisStatus st;
    return leptris_parse_string(xml, strlen(xml), &st);
}

std::string S(const char* s) { return s ? s : ""; }

std::string Serial(LeptrisDocument d) {
    char* out = leptris_document_serialize(d, nullptr);
    std::string r = out ? out : "";
    leptris_free_string(out);
    return r;
}

}  // namespace

TEST(Schematron, ValidDocument) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisSchematron sch = leptris_schematron_parse(
        kSchema, strlen(kSchema), &st);
    ASSERT_NE(sch, nullptr) << S(leptris_schematron_error(sch));
    LeptrisDocument doc = P(
        "<catalog><item price='5'>a</item>"
        "<item price='120'>b</item></catalog>");
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(leptris_schematron_valid(sch, doc), 1);
    LeptrisDocument svrl = leptris_schematron_validate(sch, doc);
    ASSERT_NE(svrl, nullptr);
    std::string text = Serial(svrl);
    EXPECT_NE(text.find("schematron-output"), std::string::npos);
    /* no failed asserts; the report fired for price=120 */
    EXPECT_EQ(text.find("failed-assert"), std::string::npos);
    EXPECT_NE(text.find("successful-report"), std::string::npos);
    EXPECT_NE(text.find("price is high"), std::string::npos);
    leptris_document_free(svrl);
    leptris_document_free(doc);
    leptris_schematron_free(sch);
}

TEST(Schematron, FailedAssertCarriesTestAndText) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisSchematron sch = leptris_schematron_parse(
        kSchema, strlen(kSchema), &st);
    ASSERT_NE(sch, nullptr);
    LeptrisDocument doc = P(
        "<catalog><item price='-3'>a</item></catalog>");
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(leptris_schematron_valid(sch, doc), 0);
    LeptrisDocument svrl = leptris_schematron_validate(sch, doc);
    ASSERT_NE(svrl, nullptr);
    std::string text = Serial(svrl);
    EXPECT_NE(text.find("failed-assert"), std::string::npos);
    EXPECT_NE(text.find("@price &gt; 0"), std::string::npos);
    EXPECT_NE(text.find("price must be positive"),
              std::string::npos);
    leptris_document_free(svrl);
    leptris_document_free(doc);
    leptris_schematron_free(sch);
}

TEST(Schematron, ContextSelectsEveryMatch) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisSchematron sch = leptris_schematron_parse(
        kSchema, strlen(kSchema), &st);
    ASSERT_NE(sch, nullptr);
    /* two bad items -> two failed asserts */
    LeptrisDocument doc = P(
        "<catalog><item price='-1'>a</item>"
        "<item price='-2'>b</item></catalog>");
    ASSERT_NE(doc, nullptr);
    LeptrisDocument svrl = leptris_schematron_validate(sch, doc);
    ASSERT_NE(svrl, nullptr);
    std::string text = Serial(svrl);
    /* count occurrences of the open tag */
    size_t n = 0, pos = 0;
    while ((pos = text.find("<svrl:failed-assert", pos)) !=
           std::string::npos) {
        n++;
        pos += 5;
    }
    EXPECT_EQ(n, 2u);
    leptris_document_free(svrl);
    leptris_document_free(doc);
    leptris_schematron_free(sch);
}

TEST(Schematron, SecondPatternRuleEvaluates) {
    const char* sch2 =
        "<schema xmlns='http://purl.oclc.org/dsdl/schematron'"
        " queryBinding='xslt'>"
        "<pattern><rule context='catalog'>"
        "<assert test='count(item) > 0'>needs items</assert>"
        "</rule></pattern></schema>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisSchematron sch =
        leptris_schematron_parse(sch2, strlen(sch2), &st);
    ASSERT_NE(sch, nullptr);
    LeptrisDocument empty = P("<catalog/>");
    ASSERT_NE(empty, nullptr);
    EXPECT_EQ(leptris_schematron_valid(sch, empty), 0);
    leptris_document_free(empty);
    leptris_schematron_free(sch);
}

TEST(Schematron, LoudRefusals) {
    LeptrisStatus st = LEPTRIS_OK;
    /* not well-formed */
    EXPECT_EQ(leptris_schematron_parse("<schema", 7, &st), nullptr);
    EXPECT_NE(st, LEPTRIS_OK);
    st = LEPTRIS_OK;
    /* unknown query binding */
    const char* bad =
        "<schema xmlns='http://purl.oclc.org/dsdl/schematron'"
        " queryBinding='xpath2'/><nope";
    LeptrisSchematron s2 = leptris_schematron_parse(
        "<schema xmlns='http://purl.oclc.org/dsdl/schematron'"
        " queryBinding='xpath2'/>",
        70, &st);
    EXPECT_EQ(s2, nullptr);
    EXPECT_NE(st, LEPTRIS_OK);
    st = LEPTRIS_OK;
    /* NULL arg */
    EXPECT_EQ(leptris_schematron_parse(nullptr, 0, &st), nullptr);
    EXPECT_NE(st, LEPTRIS_OK);
}

TEST(Schematron, ParseFileEntry) {
    /* Build-dir-relative: /tmp does not exist on Windows. */
    FILE* fp = fopen("leptris_sch_test.tmp", "wb");
    ASSERT_NE(fp, nullptr);
    fwrite(kSchema, 1, strlen(kSchema), fp);
    fclose(fp);
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisSchematron sch =
        leptris_schematron_parse_file("leptris_sch_test.tmp",
                                      &st);
    EXPECT_NE(sch, nullptr);
    if (sch) leptris_schematron_free(sch);
}


/* Lane 16 phases 3+4: abstract patterns with params, let
 * variables (schema/pattern/rule scoping), phase selection,
 * diagnostics and properties (IDs ride the SVRL). */
TEST(Schematron, AbstractPatternInstantiation) {
    const char* sch =
        "<schema xmlns='http://purl.oclc.org/dsdl/schematron'"
        " queryBinding='xslt'>"
        "<pattern is-abstract='true' id='open-attr'>"
        "<rule context='$element'>"
        "<assert test='@$attribute'>missing @$attribute</assert>"
        "</rule></pattern>"
        "<pattern is-a='open-attr'>"
        "<param name='element' value='a'/>"
        "<param name='attribute' value='id'/>"
        "</pattern></schema>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisSchematron s = leptris_schematron_parse(sch, strlen(sch), &st);
    ASSERT_NE(s, nullptr);
    LeptrisDocument bad = P("<root><a/></root>");
    ASSERT_NE(bad, nullptr);
    EXPECT_EQ(leptris_schematron_valid(s, bad), 0);
    LeptrisDocument good = P("<root><a id='1'/></root>");
    ASSERT_NE(good, nullptr);
    EXPECT_EQ(leptris_schematron_valid(s, good), 1);
    leptris_document_free(bad);
    leptris_document_free(good);
    leptris_schematron_free(s);
}

TEST(Schematron, LetVariables) {
    const char* sch =
        "<schema xmlns='http://purl.oclc.org/dsdl/schematron'"
        " queryBinding='xslt'>"
        "<let name='max' value='10'/>"
        "<pattern><rule context='item'>"
        "<let name='name' value='string(@n)'/>"
        "<assert test='@n &lt;= $max'>too big</assert>"
        "<report test='$name = &quot;x&quot;'>named x</report>"
        "</rule></pattern></schema>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisSchematron s = leptris_schematron_parse(sch, strlen(sch), &st);
    ASSERT_NE(s, nullptr);
    LeptrisDocument bad = P("<root><item n='11'/></root>");
    ASSERT_NE(bad, nullptr);
    EXPECT_EQ(leptris_schematron_valid(s, bad), 0);
    LeptrisDocument ok = P("<root><item n='5' /></root>");
    ASSERT_NE(ok, nullptr);
    EXPECT_EQ(leptris_schematron_valid(s, ok), 1);
    leptris_document_free(bad);
    leptris_document_free(ok);
    leptris_schematron_free(s);
}

TEST(Schematron, PhaseSelection) {
    const char* sch =
        "<schema xmlns='http://purl.oclc.org/dsdl/schematron'"
        " queryBinding='xslt'>"
        "<phase id='quick'><active pattern='p1'/></phase>"
        "<pattern id='p1'><rule context='a'>"
        "<assert test='@v'>a needs v</assert></rule></pattern>"
        "<pattern id='p2'><rule context='b'>"
        "<assert test='@w'>b needs w</assert></rule></pattern>"
        "</schema>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisSchematron s = leptris_schematron_parse(sch, strlen(sch), &st);
    ASSERT_NE(s, nullptr);
    LeptrisDocument doc = P("<root><a/><b/></root>");
    ASSERT_NE(doc, nullptr);
    /* default (no phase): all patterns -> both fire -> invalid */
    EXPECT_EQ(leptris_schematron_valid(s, doc), 0);
    /* quick phase: only p1 fires; b's failure is out of scope ->
     * still invalid because a lacks v; with a fixed a and broken
     * b, the phase must flip validity. */
    LeptrisDocument doc2 = P("<root><a v='1'/><b/></root>");
    ASSERT_NE(doc2, nullptr);
    EXPECT_EQ(leptris_schematron_valid(s, doc2), 0);
    LeptrisSchematron sq = leptris_schematron_parse_phase(
        sch, strlen(sch), "quick", &st);
    ASSERT_NE(sq, nullptr);
    EXPECT_EQ(leptris_schematron_valid(sq, doc2), 1);
    leptris_schematron_free(sq);
    leptris_document_free(doc);
    leptris_document_free(doc2);
    leptris_schematron_free(s);
}
