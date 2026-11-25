// test/xsd/test_xsd11_opencontent.cpp — #2151: xs:openContent /
// xs:defaultOpenContent (XSD 1.1 §3.4.2.3.3 clauses 5/6, §3.4.4.3,
// cos-ct-extends.1.4.3.2.2.3/4, cos-ns-subset). The suite pin is the
// pinned w3c/xsdtests verdict metadata (saxonData/Open, ibmData
// S3_4_1); these specs pin the rules those rows hinge on:
//   - capture: mode absent = interleave; defaultOpenContent
//     (appliesToEmpty); the unmarked-schema syntax gate (the Open
//     corpus carries no version marker);
//   - suffix: extras only after the model's final satisfied particle;
//   - interleave: extras anywhere between matched particles;
//   - empty content: variety empty forbids text; the declared
//     wildcard upgrades the variety to element-only;
//   - derivation: extension inherits / unions / mode ladder;
//     restriction subset + no-add; the empty-model exemption;
//   - xs:all / xs:choice models carry open content too;
//   - the schema-error class: mode=none + any, missing any, non-any
//     child, annotation limit, ID form/uniqueness, occurrence
//     attributes on the wildcard, simpleContent carriers, mixed
//     conflicts and the mixed-mismatch extension.
#include <gtest/gtest.h>

#include <string>

#include "leptris.h"

namespace {

const char* kDir = LEPTRIS_XSD11_OPENCONTENT_DIR;

std::string path(const char* name) {
    return std::string(kDir) + "/" + name;
}

void expect_schema_error(const char* schema) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile_file(path(schema).c_str(), &st);
    EXPECT_TRUE(s == (LeptrisXsdSchema)0 || st != LEPTRIS_OK)
        << schema << " must be rejected";
    if (s) leptris_xsd_free(s);
}

void expect_schema_ok(const char* schema) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile_file(path(schema).c_str(), &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0) << schema << " must compile";
    EXPECT_EQ(st, LEPTRIS_OK) << schema << " compile status";
    if (s) leptris_xsd_free(s);
}

int verdict_of(const char* schema, const char* instance) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile_file(path(schema).c_str(), &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0) << schema << " must compile";
    EXPECT_EQ(st, LEPTRIS_OK) << schema << " compile status";
    if (!s) return -1;
    LeptrisStatus pst = (LeptrisStatus)0;
    LeptrisDocument d = leptris_parse_file(path(instance).c_str(), &pst);
    EXPECT_NE(d, (LeptrisDocument)0) << instance << " must parse";
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

/* ---- modes -------------------------------------------------------- */

TEST(Xsd11OpenContent, SuffixMode) {
    EXPECT_EQ(verdict_of("suffix.xsd", "suffix.v1.xml"), 1);
    EXPECT_NE(verdict_of("suffix.xsd", "suffix.n1.xml"), 1);
    EXPECT_NE(verdict_of("suffix.xsd", "suffix.n2.xml"), 1);
}

/* The mode attribute defaults to interleave (schema for schema
 * documents; open025's suite note confirms). */
TEST(Xsd11OpenContent, ModeDefaultsToInterleave) {
    EXPECT_EQ(verdict_of("default_mode.xsd", "default_mode.v1.xml"), 1);
    EXPECT_NE(verdict_of("default_mode.xsd", "default_mode.n1.xml"), 1);
}

/* ---- defaultOpenContent ------------------------------------------- */

TEST(Xsd11OpenContent, DefaultOpenContentApplies) {
    EXPECT_EQ(verdict_of("default_open.xsd", "default_open.v1.xml"), 1);
    EXPECT_NE(verdict_of("default_open.xsd", "default_open.n1.xml"), 1);
}

/* appliesToEmpty="true" extends the default to empty models. */
TEST(Xsd11OpenContent, DefaultAppliesToEmpty) {
    EXPECT_EQ(verdict_of("applies_empty.xsd", "applies_empty.v1.xml"), 1);
    EXPECT_NE(verdict_of("applies_empty.xsd", "applies_empty.n1.xml"), 1);
}

/* Without appliesToEmpty an empty model stays EMPTY: no extras and
 * no character content at all (variety empty, open012.n3). */
TEST(Xsd11OpenContent, EmptyModelWithoutDefaultStaysEmpty) {
    EXPECT_EQ(verdict_of("not_applies_empty.xsd",
                         "not_applies_empty.v1.xml"), 1);
    EXPECT_NE(verdict_of("not_applies_empty.xsd",
                         "not_applies_empty.n1.xml"), 1);
    EXPECT_NE(verdict_of("not_applies_empty.xsd",
                         "not_applies_empty.n2.xml"), 1);
}

/* A DECLARED wildcard applies to an empty model regardless, and the
 * variety upgrades to element-only (whitespace text legal —
 * open010.v2). */
TEST(Xsd11OpenContent, DeclaredWildcardUpgradesEmptyVariety) {
    EXPECT_EQ(verdict_of("empty_declared.xsd", "empty_declared.v1.xml"),
              1);
    EXPECT_EQ(verdict_of("empty_declared.xsd", "empty_declared.v2.xml"),
              1);
    EXPECT_NE(verdict_of("empty_declared.xsd", "empty_declared.n1.xml"),
              1);
}

/* ---- derivation --------------------------------------------------- */

/* An extension without declared open content inherits the base's. */
TEST(Xsd11OpenContent, ExtensionInheritsBase) {
    EXPECT_EQ(verdict_of("ext_inherit.xsd", "ext_inherit.v1.xml"), 1);
    EXPECT_NE(verdict_of("ext_inherit.xsd", "ext_inherit.n1.xml"), 1);
}

/* mode="none" on an extension blocks the default and keeps the
 * base's content type — open content included (open031). */
TEST(Xsd11OpenContent, ExtensionNoneInheritsBase) {
    EXPECT_EQ(verdict_of("ext_none_inherit.xsd",
                         "ext_none_inherit.v1.xml"), 1);
}

/* cos-ct-extends.1.4.3.2.2.3: base interleave, derived suffix. */
TEST(Xsd11OpenContent, ExtensionSuffixOverInterleaveRejected) {
    expect_schema_error("ext_bad_modes.xsd");
}

/* The schema default takes precedence over the base's open content;
 * suffix-over-interleave then fails the ladder (open030). */
TEST(Xsd11OpenContent, ExtensionDefaultOverBaseRejected) {
    expect_schema_error("ext_default_over_base.xsd");
}

/* The extension wildcard is the UNION of base and derived. */
TEST(Xsd11OpenContent, ExtensionWildcardUnion) {
    EXPECT_EQ(verdict_of("ext_union.xsd", "ext_union.v1.xml"), 1);
    EXPECT_NE(verdict_of("ext_union.xsd", "ext_union.n1.xml"), 1);
}

/* cos-content-act-restrict: a restriction cannot add open content
 * the base's model rejects (open016.bad). */
TEST(Xsd11OpenContent, RestrictionCannotAddOpenContent) {
    expect_schema_error("res_add_oc.xsd");
}

/* cos-ns-subset: the derived namespace set may not widen. */
TEST(Xsd11OpenContent, RestrictionNamespaceSubset) {
    expect_schema_error("res_widen_ns.xsd");
}

/* A weaker processContents admits undeclared elements the base
 * rejects (open018.bad). */
TEST(Xsd11OpenContent, RestrictionProcessContentsNoWeaker) {
    expect_schema_error("res_weaken_pc.xsd");
}

/* The mode may not widen on a non-empty model (open019.bad). */
TEST(Xsd11OpenContent, RestrictionModeMayNotWiden) {
    expect_schema_error("res_widen_mode.xsd");
}

/* The suite posture (open020/021/022): an EMPTY derived model makes
 * the wider derived open content harmless. */
TEST(Xsd11OpenContent, RestrictionEmptyModelExemption) {
    expect_schema_ok("res_empty_ok.xsd");
    EXPECT_EQ(verdict_of("res_empty_ok.xsd", "res_empty_ok.v1.xml"), 1);
}

/* ---- xs:all / xs:choice models ------------------------------------ */

TEST(Xsd11OpenContent, AllModelSuffix) {
    EXPECT_EQ(verdict_of("all_suffix.xsd", "all_suffix.v1.xml"), 1);
    EXPECT_NE(verdict_of("all_suffix.xsd", "all_suffix.n1.xml"), 1);
}

TEST(Xsd11OpenContent, AllModelInterleave) {
    EXPECT_EQ(verdict_of("all_interleave.xsd", "all_interleave.v1.xml"),
              1);
    EXPECT_NE(verdict_of("all_interleave.xsd", "all_interleave.n1.xml"),
              1);
}

TEST(Xsd11OpenContent, ChoiceModelInterleave) {
    EXPECT_EQ(verdict_of("choice_interleave.xsd",
                         "choice_interleave.v1.xml"), 1);
    EXPECT_NE(verdict_of("choice_interleave.xsd",
                         "choice_interleave.n1.xml"), 1);
}

/* ---- the schema-error class --------------------------------------- */

/* mode="none" forbids an xs:any child (bug 7069 / open036.bad). */
TEST(Xsd11OpenContent, NoneWithAnyRejected) {
    expect_schema_error("none_with_any.xsd");
}

/* A mode other than none requires an xs:any child (s3_4_1si01). */
TEST(Xsd11OpenContent, MissingAnyRejected) {
    expect_schema_error("none_no_any.xsd");
}

/* Children other than xs:annotation / xs:any are not allowed
 * (s3_4_1si03). */
TEST(Xsd11OpenContent, NonAnyChildRejected) {
    expect_schema_error("bad_child.xsd");
}

/* At most one annotation (open037.bad). */
TEST(Xsd11OpenContent, AnnotationLimit) {
    expect_schema_error("two_annos.xsd");
}

/* xs:ID values must be unique (open038.bad) and NCNames
 * (open039.bad). */
TEST(Xsd11OpenContent, IdUniquenessAndForm) {
    expect_schema_error("dup_id.xsd");
    expect_schema_error("bad_id.xsd");
}

/* The open content wildcard carries no occurrence attributes
 * (bug 15618 / open048.bad). */
TEST(Xsd11OpenContent, WildcardOccurrenceRejected) {
    expect_schema_error("any_maxoccurs.xsd");
}

/* ct-props-correct.5: open content needs element-only or mixed
 * content — simpleContent types cannot carry it. */
TEST(Xsd11OpenContent, SimpleContentCarrierRejected) {
    expect_schema_error("simple_content_oc.xsd");
}

/* src-ct.5: the mixed attributes must agree (s3_4_1si09); an
 * extension's mixedness must match the base's (s3_4_1si08). */
TEST(Xsd11OpenContent, MixedAgreement) {
    expect_schema_error("mixed_conflict.xsd");
    expect_schema_error("ext_mixed_mismatch.xsd");
}
