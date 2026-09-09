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
    FILE* fp = fopen("/tmp/leptris_sch_test.sch", "wb");
    ASSERT_NE(fp, nullptr);
    fwrite(kSchema, 1, strlen(kSchema), fp);
    fclose(fp);
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisSchematron sch =
        leptris_schematron_parse_file("/tmp/leptris_sch_test.sch",
                                      &st);
    EXPECT_NE(sch, nullptr);
    if (sch) leptris_schematron_free(sch);
}
