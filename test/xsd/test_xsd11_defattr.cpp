// test/xsd/test_xsd11_defattr.cpp — #2182: default attribute groups
// (XSD 1.1 §3.4.2.5). xs:schema/@defaultAttributes names an
// attributeGroup whose uses become defaults for every complexType
// written in THAT schema document, appended after the type's own
// uses (§3.4.2.4: computed as if an <attributeGroup ref> child
// appeared after any other attributeGroup children), unless the type
// opts out via defaultAttributesApply="false". The appended uses
// enforce exactly like declared ones (form/qualification per the
// attribute-row binding, types, facets); the group's xs:anyAttribute
// participates in the wildcard computation; defaults ride derivation
// (every derived type receives its own synthetic ref). Schema-error
// classes: a ref that does not resolve (unresolved or foreign
// namespace — QName resolution §3.17.6.2) and the duplicate
// attribute use the synthetic ref can introduce (c-props-correct.4 —
// the ibm si02/si03 shape; circular group refs are legal in 1.1 but
// the cycle re-introduces the same name).
//
// Fixtures: the pinned w3c/xsdtests corpus rows for this family
// (saxonData/Open/open035/044/045/202-205 + the ibm D sets) plus
// local derivation shapes under xsd11-defattr/.
#include <gtest/gtest.h>

#include <string>

#include "leptris.h"

namespace {

const char* kCorpus = LEPTRIS_XSD11_CASES_DIR;
const char* kDir = LEPTRIS_XSD11_DEFATTR_DIR;

std::string corpus_path(const char* name) {
    return std::string(kCorpus) + "/" + name;
}

std::string path(const char* name) {
    return std::string(kDir) + "/" + name;
}

/* Compile `schema`; 1 = clean schema, 0 = rejected, -1 = harness
 * expectation failed. */
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

}  // namespace

/* ---- application ------------------------------------------------- */

/* saxon open201: the group's optional uses attach to BOTH the named
 * type and the inline doc type; optional uses need not appear. */
TEST(Xsd11DefAttr, AppliesToNamedAndInlineTypes) {
    const std::string xsd = corpus_path("saxonData/Open/open201.xsd");
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_EQ(verdict_of(xsd, corpus_path("saxonData/Open/open201.v1.xml")),
              1);
    EXPECT_EQ(verdict_of(xsd, corpus_path("saxonData/Open/open201.v2.xml")),
              1);
}

/* saxon open202: defaultAttributesApply="false" opts the type out —
 * the group's attrs are NOT admitted there. */
TEST(Xsd11DefAttr, ApplyFalseOptsOut) {
    const std::string xsd = corpus_path("saxonData/Open/open202.xsd");
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_EQ(verdict_of(xsd, corpus_path("saxonData/Open/open202.v1.xml")),
              1);
    EXPECT_NE(verdict_of(xsd, corpus_path("saxonData/Open/open202.n1.xml")),
              1);
}

/* ibm s3_4_2_4v02/ii02: the opt-out schema admits NO defaultAttr;
 * carrying one is invalid. */
TEST(Xsd11DefAttr, IbmdApplyFalse) {
    const std::string xsd =
        corpus_path("ibmData/valid/S3_4_2_4/s3_4_2_4v02.xsd");
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_EQ(
        verdict_of(xsd,
                   corpus_path("ibmData/valid/S3_4_2_4/s3_4_2_4v02.xml")),
        1);
    EXPECT_NE(
        verdict_of(
            xsd,
            corpus_path(
                "ibmData/instance_invalid/S3_4_2_4/s3_4_2_4ii02.xml")),
        1);
}

/* saxon open035: a required use from the default group is enforced —
 * missing id is invalid, and an element typed xs:string rejects the
 * attribute outright. */
TEST(Xsd11DefAttr, RequiredUseEnforced) {
    const std::string xsd = corpus_path("saxonData/Open/open035.xsd");
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_EQ(verdict_of(xsd, corpus_path("saxonData/Open/open035.v1.xml")),
              1);
    EXPECT_NE(verdict_of(xsd, corpus_path("saxonData/Open/open035.n1.xml")),
              1);
    EXPECT_NE(verdict_of(xsd, corpus_path("saxonData/Open/open035.n2.xml")),
              1);
}

/* ibm v01/v03/v13: required qualified-form default uses bind the
 * qualified instance spellings; explicit defaultAttributesApply=
 * "true" keeps the application; a type that refs another group
 * still receives the default group's rows. */
TEST(Xsd11DefAttr, IbmdValidSets) {
    EXPECT_EQ(
        schema_ok(
            corpus_path("ibmData/valid/S3_4_2_4/s3_4_2_4v01.xsd")),
        1);
    EXPECT_EQ(
        verdict_of(
            corpus_path("ibmData/valid/S3_4_2_4/s3_4_2_4v01.xsd"),
            corpus_path("ibmData/valid/S3_4_2_4/s3_4_2_4v01.xml")),
        1);
    EXPECT_EQ(
        verdict_of(
            corpus_path("ibmData/valid/S3_4_2_4/s3_4_2_4v03.xsd"),
            corpus_path("ibmData/valid/S3_4_2_4/s3_4_2_4v03.xml")),
        1);
    EXPECT_EQ(
        verdict_of(
            corpus_path("ibmData/valid/S3_4_2_4/s3_4_2_4v13.xsd"),
            corpus_path("ibmData/valid/S3_4_2_4/s3_4_2_4v13.xml")),
        1);
    EXPECT_NE(
        verdict_of(
            corpus_path("ibmData/valid/S3_4_2_4/s3_4_2_4v13.xsd"),
            corpus_path(
                "ibmData/instance_invalid/S3_4_2_4/s3_4_2_4ii11.xml")),
        1);
}

/* saxon open044: the group's xs:anyAttribute rides the default —
 * and the defaults APPLY to types declared within xs:redefine
 * (the redefining beta admits xml:lang). */
TEST(Xsd11DefAttr, WildcardRidesRedefinedType) {
    const std::string xsd = corpus_path("saxonData/Open/open044.xsd");
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_EQ(verdict_of(xsd, corpus_path("saxonData/Open/open044.v1.xml")),
              1);
    EXPECT_NE(verdict_of(xsd, corpus_path("saxonData/Open/open044.n1.xml")),
              1);
    EXPECT_NE(verdict_of(xsd, corpus_path("saxonData/Open/open044.n2.xml")),
              1);
}

/* saxon open045: defaults do NOT apply to types declared within
 * xs:override (§4.2.5: document-level defaults ride the OVERRIDDEN
 * document) — xml:lang on the overridden beta is invalid. */
TEST(Xsd11DefAttr, OverrideBodyExcluded) {
    const std::string xsd = corpus_path("saxonData/Open/open045.xsd");
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_EQ(verdict_of(xsd, corpus_path("saxonData/Open/open045.v1.xml")),
              1);
    EXPECT_NE(verdict_of(xsd, corpus_path("saxonData/Open/open045.n1.xml")),
              1);
    EXPECT_NE(verdict_of(xsd, corpus_path("saxonData/Open/open045.n2.xml")),
              1);
}

/* saxon open205: the ref may name a group in another namespace —
 * the group's qualified-form rows bind attributes in ITS namespace. */
TEST(Xsd11DefAttr, CrossNamespaceGroup) {
    const std::string xsd = corpus_path("saxonData/Open/open205.xsd");
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_EQ(verdict_of(xsd, corpus_path("saxonData/Open/open205.v1.xml")),
              1);
    EXPECT_NE(verdict_of(xsd, corpus_path("saxonData/Open/open205.n1.xml")),
              1);
}

/* Defaults ride derivation: the extension of a defaulted base
 * enforces the default rows too (#2182 clause 4). */
TEST(Xsd11DefAttr, DefaultsRideDerivation) {
    const std::string xsd = path("deriv_ext.xsd");
    ASSERT_EQ(schema_ok(xsd), 1);
    EXPECT_EQ(verdict_of(xsd, path("deriv_ext_v1.xml")), 1);
    EXPECT_EQ(verdict_of(xsd, path("deriv_ext_v2.xml")), 1);
    EXPECT_NE(verdict_of(xsd, path("deriv_ext_n1.xml")), 1);
}

/* ---- schema-error classes ----------------------------------------- */

/* saxon open203.bad + ibm si01: the defaultAttributes reference does
 * not resolve — the schema is rejected. */
TEST(Xsd11DefAttr, SchemaErrorUnresolvedRef) {
    EXPECT_EQ(
        schema_ok(corpus_path("saxonData/Open/open203.bad.xsd")), 0);
    EXPECT_EQ(
        schema_ok(
            corpus_path("ibmData/schema_invalid/S3_4_2_4/"
                        "s3_4_2_4si01.xsd")),
        0);
}

/* saxon open204.bad: the ref resolves to a namespace that did not
 * import the group — QName resolution fails, the schema is
 * rejected. */
TEST(Xsd11DefAttr, SchemaErrorForeignNamespace) {
    EXPECT_EQ(
        schema_ok(corpus_path("saxonData/Open/open204.bad.xsd")), 0);
}

/* ibm si02: the synthetic ref collides with the type's own use —
 * c-props-correct.4 (two attribute uses with the same expanded
 * name) fails the schema. */
TEST(Xsd11DefAttr, SchemaErrorDuplicateUse) {
    EXPECT_EQ(
        schema_ok(
            corpus_path("ibmData/schema_invalid/S3_4_2_4/"
                        "s3_4_2_4si02.xsd")),
        0);
}

/* ibm si03: the cycle through the default group re-introduces the
 * same expanded name on the type — the duplicate use fails the
 * schema. */
TEST(Xsd11DefAttr, SchemaErrorCircularThroughDefault) {
    EXPECT_EQ(
        schema_ok(
            corpus_path("ibmData/schema_invalid/S3_4_2_4/"
                        "s3_4_2_4si03.xsd")),
        0);
}
