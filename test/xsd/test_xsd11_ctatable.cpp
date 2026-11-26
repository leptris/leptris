// test/xsd/test_xsd11_ctatable.cpp — #2184: the alternative-table
// compile constraints (the cta schema-error class in
// test/xsd/xsd11-dispositions/README.md). The type table's compile
// side must enforce what §3.12.4 dispatch already assumes:
//
//   - src-element.5 / §3.3.9.2 ordering: a test-less alternative is
//     the DEFAULT — it may only close the table, never sit mid-table.
//   - §3.12.2 XPath valid: an alternative test that does not parse
//     fails the SCHEMA (evaluation errors stay the §3.12.4 "false"
//     channel — Xsd11Cta.TestEvaluationErrorIsFalse).
//   - §3.3.6.1 cl.7 / cos-ct-type-substitutable: every alternative's
//     type derives from the declared type (or is xs:error, or the
//     declared type is the ur-type).
//   - cos-element-consistent: two same-name particles in one content
//     model must carry EQUAL type tables.
//   - particle restriction (cos-st-restricts): a complexContent
//     restriction's same-name particle must preserve the base
//     particle's type table.
//   - src-ct.1: a complexType carrying a simpleContent/complexContent
//     wrapper may not also declare attributes at the top level.
//   - §3.2.7.1 {inheritable}: the attribute's lexical form must be a
//     boolean, and a restriction may not change the base use's
//     inheritability.
//
// Fixtures ride the pinned xsd11-cases corpus (the suite's own
// schemaTest rows), oracle = the suite metadata.
#include <gtest/gtest.h>

#include <string>

#include "leptris.h"

namespace {

const char* kCases = LEPTRIS_XSD11_CASES_DIR;

std::string path(const char* name) {
    return std::string(kCases) + "/" + name;
}

/* Compile result: 1 = clean, 0 = rejected, -1 = no handle. */
int compile_of(const char* schema) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile_file(path(schema).c_str(), &st);
    if (!s) return -1;
    int ok = (st == LEPTRIS_OK);
    leptris_xsd_free(s);
    return ok ? 1 : 0;
}

}  // namespace

/* ---- type substitutability (§3.3.6.1 cl.7) ----------------------- */

/* s3_12si03: the declared type is xs:integer; the alternatives
 * xs:string and xs:decimal do not derive FROM it (decimal is the
 * ANCESTOR) — the schema must be rejected. */
TEST(Xsd11CtaTable, NonDerivingSimpleAlternativesRejected) {
    EXPECT_EQ(compile_of("ibmData/schema_invalid/S3_12/s3_12si03.xsd"), 0);
}

/* s3_12si02: the declared type is an inline complexType; the
 * alternatives are builtin simple types — no complex type derives
 * from a simple one. (The element also names an unknown type — the
 * alternative-table constraint is what must fire.) */
TEST(Xsd11CtaTable, SimpleAlternativesForComplexDeclaredRejected) {
    EXPECT_EQ(compile_of("ibmData/schema_invalid/S3_12/s3_12si02.xsd"), 0);
}

/* cta9008err: the alternatives are standalone inline complexTypes
 * (no derivation base) — not substitutable for the declared docType. */
TEST(Xsd11CtaTable, StandaloneInlineAlternativesRejected) {
    EXPECT_EQ(compile_of("saxonData/CTA/cta9008err.xsd"), 0);
}

/* The legal direction holds: a builtin alternative deriving from the
 * declared builtin (xs:nonNegativeInteger ⊂ xs:integer), and simple
 * alternatives against the ur-type (anyType) both compile. */
TEST(Xsd11CtaTable, DerivingAndUrTypeAlternativesCompile) {
    EXPECT_EQ(compile_of("ibmData/mixed/type-alternatives/test4.xsd"), 1);
    EXPECT_EQ(compile_of("ibmData/valid/S3_12/s3_12v11.xsd"), 1);
}

/* ---- XPath valid (§3.12.2) --------------------------------------- */

/* s3_12si04/si05/si06: syntactically-invalid alternative tests —
 * truncations, an uppercase AND, an xs1::double cast target — fail
 * the SCHEMA (evaluation errors, not syntax, are the false channel). */
TEST(Xsd11CtaTable, InvalidTestSyntaxFailsSchema) {
    EXPECT_EQ(compile_of("ibmData/schema_invalid/S3_12/s3_12si04.xsd"), 0);
    EXPECT_EQ(compile_of("ibmData/schema_invalid/S3_12/s3_12si05.xsd"), 0);
    EXPECT_EQ(compile_of("ibmData/schema_invalid/S3_12/s3_12si06.xsd"), 0);
}

/* The cast-into-comparison shape the valid corpus uses
 * (`@length cast as xs:int > @width cast as xs:int`) must parse —
 * the schema stays legal. */
TEST(Xsd11CtaTable, CastComparisonTestParses) {
    EXPECT_EQ(
        compile_of("ibmData/mixed/type-alternatives/test3.xsd"), 1);
    EXPECT_EQ(
        compile_of("ibmData/mixed/type-alternatives/test5.xsd"), 1);
}

/* ---- src-element.5 ordering --------------------------------------- */

/* cta9001err: a test-less alternative mid-table (the suite comment:
 * "invalid, test attribute missing on non-final alternative") —
 * schema error. */
TEST(Xsd11CtaTable, TestlessMidTableAlternativeRejected) {
    EXPECT_EQ(compile_of("saxonData/CTA/cta9001err.xsd"), 0);
}

/* ---- cos-element-consistent (type tables) ------------------------- */

/* cta9009err: two same-name particles whose tables differ (a third
 * alternative on the second) — schema error. */
TEST(Xsd11CtaTable, SameNameDifferentTablesRejected) {
    EXPECT_EQ(compile_of("saxonData/CTA/cta9009err.xsd"), 0);
}

/* cta9010err: a tabled particle next to a same-name particle with no
 * table — the tables are not equal. */
TEST(Xsd11CtaTable, TabledVsUntabledSameNameRejected) {
    EXPECT_EQ(compile_of("saxonData/CTA/cta9010err.xsd"), 0);
}

/* cta0043: appendixType restricts chapType but NARROWS the stamp
 * particle's type table (dateTime where the base spells
 * dateTimeStamp) — the restriction must preserve the base table. */
TEST(Xsd11CtaTable, RestrictionNarrowingTypeTableRejected) {
    EXPECT_EQ(compile_of("saxonData/CTA/cta0043.xsd"), 0);
}

/* The legal restriction shape: treq restricting t restates its
 * particle identically (no table on either side) — compiles. */
TEST(Xsd11CtaTable, LegalRestrictionRestatingParticlesCompiles) {
    EXPECT_EQ(compile_of("saxonData/CTA/cta0002.xsd"), 1);
}

/* ---- src-ct.1 (wrapper + direct attribute children) --------------- */

/* s3_12si01: messageType declares simpleContent AND carries
 * xs:attribute / xs:anyAttribute at the complexType level — schema
 * error. */
TEST(Xsd11CtaTable, AttributesOutsideDerivationWrapperRejected) {
    EXPECT_EQ(compile_of("ibmData/schema_invalid/S3_12/s3_12si01.xsd"), 0);
}

/* ---- {inheritable} (§3.2.7.1) ------------------------------------- */

/* cta9006err/cta9007err: inheritable="" and inheritable="2" are not
 * boolean spellings — schema error. */
TEST(Xsd11CtaTable, MalformedInheritableLexicalRejected) {
    EXPECT_EQ(compile_of("saxonData/CTA/cta9006err.xsd"), 0);
    EXPECT_EQ(compile_of("saxonData/CTA/cta9007err.xsd"), 0);
}

/* cta9004err/cta9005err: the restriction flips the base use's
 * inheritability (true→false and unset→true) — schema error. */
TEST(Xsd11CtaTable, RestrictionInheritabilityFlipRejected) {
    EXPECT_EQ(compile_of("saxonData/CTA/cta9004err.xsd"), 0);
    EXPECT_EQ(compile_of("saxonData/CTA/cta9005err.xsd"), 0);
}

/* The inheritable VALID shape (s3_12v11's attribute carries
 * inheritable="true" with no restriction in play) stays legal. */
TEST(Xsd11CtaTable, LegalInheritableAttributeCompiles) {
    EXPECT_EQ(compile_of("ibmData/valid/S3_12/s3_12v11.xsd"), 1);
}
