// test/xsd/test_xsd11_xitype.cpp — #2184: the DEFAULTED-xsi:type
// class (disposition row 18, saxon Complex). An attribute use
// referencing a BUILT-IN xsi: declaration —
// <xs:attribute ref="xsi:type" default="..."/> and friends — is
// legal schema syntax (§3.2.6.4: "It is legal for Attribute Uses
// that refer to xsi: attributes to specify default or fixed value
// constraints") and must compile clean; the four xsi declarations
// are present in every schema by definition (§3.2.7) and resolve
// without an import (§3.17.6.2 cl. 4.2.4, G.1.15 #5). At assessment
// the use has NO defaulting effect: the defaulted-attribute set
// excludes uses whose declaration is one of the four built-ins
// (§3.4.4.2 cl. 4 of the definition — "the attribute uses would
// have no effect"), so complex003/004 (xsi:type default) judge the
// instance against the DECLARED type. The use's occurrence
// constraint still bites (§3.4.4.2 cl. 3): use="required"
// (complex009, complex010) invalidates an element that omits the
// attribute. An EXPLICIT xsi:type must override the selected type
// definition (§3.3.4.3 cl. 4) — and per §3.3.4.2 a SIMPLE type is
// never substitutable for a COMPLEX selected type, so every
// complex005 instance naming xs:integer/xs:short against the
// anonymous complex type is invalid, with assessment falling back
// to the selected type (§3.3.4.3 Note).
//
// Fixtures: the pinned w3c/xsdtests corpus rows (saxonData/Complex/
// complex003-010), the exact suite shapes the disposition class pins.
#include <gtest/gtest.h>

#include <string>

#include "leptris.h"

namespace {

const char* kCorpus = LEPTRIS_XSD11_CASES_DIR;

std::string corpus_path(const char* name) {
    return std::string(kCorpus) + "/" + name;
}

/* Compile `schema`; 1 = clean schema, 0 = rejected. */
int schema_ok(const std::string& spath) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile_file(spath.c_str(), &st);
    if (!s) return 0;
    int ok = (st == LEPTRIS_OK);
    leptris_xsd_free(s);
    return ok ? 1 : 0;
}

int verdict_of(const std::string& spath,
               const std::string& ipath) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile_file(spath.c_str(), &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0) << spath << " must compile";
    EXPECT_EQ(st, LEPTRIS_OK) << spath << " compile status";
    if (!s || st != LEPTRIS_OK) {
        if (s) leptris_xsd_free(s);
        return -1;
    }
    LeptrisStatus pst = (LeptrisStatus)0;
    LeptrisDocument d = leptris_parse_file(ipath.c_str(), &pst);
    EXPECT_NE(d, (LeptrisDocument)0) << ipath << " must parse";
    if (!d) {
        leptris_xsd_free(s);
        return -1;
    }
    int v = leptris_xsd_validate(s, d);
    leptris_document_free(d);
    leptris_xsd_free(s);
    return v;
}

const char* kC = "saxonData/Complex/";

}  // namespace

/* ---- the schemas compile clean (the src-element.2 artifact) ------- */

/* complex003/004/005: xsi:type default/fixed uses resolve. */
TEST(Xsd11XsiTypeUse, TypeDefaultAndFixedSchemasCompile) {
    EXPECT_EQ(schema_ok(corpus_path(
                  std::string(kC).append("complex003.xsd").c_str())),
              1);
    EXPECT_EQ(schema_ok(corpus_path(
                  std::string(kC).append("complex004.xsd").c_str())),
              1);
    EXPECT_EQ(schema_ok(corpus_path(
                  std::string(kC).append("complex005.xsd").c_str())),
              1);
}

/* complex006/007: xsi:nil default/fixed uses resolve. */
TEST(Xsd11XsiTypeUse, NilDefaultAndFixedSchemasCompile) {
    EXPECT_EQ(schema_ok(corpus_path(
                  std::string(kC).append("complex006.xsd").c_str())),
              1);
    EXPECT_EQ(schema_ok(corpus_path(
                  std::string(kC).append("complex007.xsd").c_str())),
              1);
}

/* complex009/010: the use="required" forms resolve. */
TEST(Xsd11XsiTypeUse, RequiredSchemasCompile) {
    EXPECT_EQ(schema_ok(corpus_path(
                  std::string(kC).append("complex009.xsd").c_str())),
              1);
    EXPECT_EQ(schema_ok(corpus_path(
                  std::string(kC).append("complex010.xsd").c_str())),
              1);
}

/* ---- the default has NO effect (§3.4.4.2 cl. 4) ------------------- */

/* complex003: default="xs:date" is ignored — the instance is judged
 * against the declared decimal content type. A decimal lexical is
 * valid, a date lexical is not (the schema's own comment: "which is
 * ignored"). */
TEST(Xsd11XsiTypeUse, DefaultIgnoredJudgesDeclaredType) {
    const std::string xsd = corpus_path(
        std::string(kC).append("complex003.xsd").c_str());
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_EQ(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex003.v1.xml")
                                         .c_str())),
              1);
    EXPECT_NE(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex003.n1.xml")
                                         .c_str())),
              1);
}

/* complex004: default="xs:integer" likewise never substitutes —
 * "abcd" is invalid as a decimal, "123.456" is valid (twice:
 * n2's filename says ".n" but the suite metadata pins it valid,
 * "valid, despite the filename"). */
TEST(Xsd11XsiTypeUse, DefaultIgnoredComplex004) {
    const std::string xsd = corpus_path(
        std::string(kC).append("complex004.xsd").c_str());
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_NE(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex004.n1.xml")
                                         .c_str())),
              1);
    EXPECT_EQ(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex004.n2.xml")
                                         .c_str())),
              1);
    EXPECT_EQ(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex004.v1.xml")
                                         .c_str())),
              1);
}

/* complex006: default="true" on xsi:nil is ignored — `<root/>` with
 * no xsi:nil is judged nilled-FALSE and fails the decimal lexical;
 * the explicit forms keep the §3.3.4.3 cl. 3 rules (content on a
 * nilled element is invalid; nil="false" with empty content fails
 * the lexical; an empty nilled element is valid). */
TEST(Xsd11XsiTypeUse, NilDefaultIgnored) {
    const std::string xsd = corpus_path(
        std::string(kC).append("complex006.xsd").c_str());
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_NE(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex006.n1.xml")
                                         .c_str())),
              1);
    EXPECT_NE(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex006.n2.xml")
                                         .c_str())),
              1);
    EXPECT_NE(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex006.n3.xml")
                                         .c_str())),
              1);
    EXPECT_EQ(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex006.v1.xml")
                                         .c_str())),
              1);
    EXPECT_EQ(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex006.v2.xml")
                                         .c_str())),
              1);
}

/* complex007: fixed="true" on xsi:nil behaves as complex006 for
 * these instances (the absent attribute gets no constraint
 * check; the present forms are judged by the nil rules). */
TEST(Xsd11XsiTypeUse, NilFixedForms) {
    const std::string xsd = corpus_path(
        std::string(kC).append("complex007.xsd").c_str());
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_NE(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex006.n1.xml")
                                         .c_str())),
              1);
    EXPECT_NE(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex006.n2.xml")
                                         .c_str())),
              1);
    EXPECT_NE(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex006.n3.xml")
                                         .c_str())),
              1);
    EXPECT_EQ(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex006.v1.xml")
                                         .c_str())),
              1);
    EXPECT_EQ(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex006.v2.xml")
                                         .c_str())),
              1);
}

/* ---- the use's occurrence constraint bites (§3.4.4.2 cl. 3) ------- */

/* complex009: xsi:type use="required" — omitting the attribute is
 * invalid; naming R (a restriction of B) dispatches the content
 * model to R (three e's fail R's 0..1 bound) and naming B admits
 * four-and-four. */
TEST(Xsd11XsiTypeUse, RequiredTypeUse) {
    const std::string xsd = corpus_path(
        std::string(kC).append("complex009.xsd").c_str());
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_NE(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex009.n1.xml")
                                         .c_str())),
              1);
    EXPECT_NE(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex009.n2.xml")
                                         .c_str())),
              1);
    EXPECT_EQ(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex009.v1.xml")
                                         .c_str())),
              1);
    EXPECT_EQ(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex009.v2.xml")
                                         .c_str())),
              1);
}

/* complex010: xsi:noNamespaceSchemaLocation use="required". */
TEST(Xsd11XsiTypeUse, RequiredNoNsSchemaLocationUse) {
    const std::string xsd = corpus_path(
        std::string(kC).append("complex010.xsd").c_str());
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_NE(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex010.n1.xml")
                                         .c_str())),
              1);
    EXPECT_EQ(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex010.v1.xml")
                                         .c_str())),
              1);
}

/* ---- an explicit xsi:type must override (§3.3.4.3 cl. 4) ---------- */

/* complex005: the named types are SIMPLE, the selected type
 * definition is the anonymous COMPLEX type — §3.3.4.2 has no
 * simple-for-complex substitutability arm, so the override fails
 * and every instance carrying xsi:type is invalid, including the
 * one whose content would validate as the named type (v1:
 * xs:integer "1234"). The attribute-free instance stays valid. */
TEST(Xsd11XsiTypeUse, ExplicitSimpleOverComplexFailsOverride) {
    const std::string xsd = corpus_path(
        std::string(kC).append("complex005.xsd").c_str());
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_NE(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex005.n1.xml")
                                         .c_str())),
              1);
    EXPECT_NE(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex005.n2.xml")
                                         .c_str())),
              1);
    EXPECT_NE(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex005.v1.xml")
                                         .c_str())),
              1);
    EXPECT_EQ(verdict_of(xsd,
                         corpus_path(std::string(kC)
                                         .append("complex005.v2.xml")
                                         .c_str())),
              1);
}
