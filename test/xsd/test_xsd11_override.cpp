// test/xsd/test_xsd11_override.cpp — #2150: xs:override (XSD 1.1
// §4.3 component replacement). The overriding document's top-level
// components REPLACE the overridden document's same-(namespace,
// name, kind) components WHOLESALE — no derivation relationship is
// required; components the override does not name survive; an
// overriding component that overrides nothing is IGNORED and its
// references must still resolve; overriding references re-resolve
// against the POST-merge set (so a replacement restricting its own
// former name restricts ITSELF — a circular derivation, unlike the
// 1.0 xs:redefine hook where the base names the original); the
// target namespaces must align (an overridden document without a
// target namespace is the only exception); an overriding set may not
// declare one name twice; and a mutual override is a schema error
// exactly when the inner override would replace something.
#include <gtest/gtest.h>

#include <string>

#include "leptris.h"

namespace {

const char* kDir = LEPTRIS_XSD11_OVERRIDE_DIR;

std::string path(const char* name) {
    return std::string(kDir) + "/" + name;
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

int verdict_of(const char* schema, const char* instance) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s = leptris_xsd_compile_file(path(schema).c_str(), &st);
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

/* Wholesale replacement: the overriding simpleType (Z-suffixed
 * pattern) types the replaced component; the overridden document's
 * unnamed components (note) survive untouched. */
TEST(Xsd11Override, ReplacedTypeTypesInstancesAndSurvivorsStay) {
    EXPECT_EQ(verdict_of("A_over.xsd", "A_v.xml"), 1);
    EXPECT_NE(verdict_of("A_over.xsd", "A_n.xml"), 1);
    EXPECT_EQ(verdict_of("A_over.xsd", "A_survivor.xml"), 1);
}

/* §4.3.2 + over011: the overriding simpleType's restriction base
 * names the component it replaces — post-merge re-resolution makes
 * the reference SELF-referential: circular restriction, schema
 * error. (1.0 redefine semantics — base names the original — do not
 * apply to override.) */
TEST(Xsd11Override, SelfReferencingSimpleRestrictionIsSchemaError) {
    EXPECT_EQ(compile_of("B_over.xsd"), 0);
}

/* over014: the complexType form of the same self-reference — the
 * overriding restriction's base names its own replacement. */
TEST(Xsd11Override, SelfReferencingComplexRestrictionIsSchemaError) {
    EXPECT_EQ(compile_of("C_over.xsd"), 0);
}

/* over016: an overriding document with NO target namespace cannot
 * override a document that declares one. */
TEST(Xsd11Override, NoTargetNamespaceOverriderIsSchemaError) {
    EXPECT_EQ(compile_of("D_over.xsd"), 0);
}

/* over017: differing target namespaces are an error; only the
 * target-namespace-ABSENT overridden document is the exception. */
TEST(Xsd11Override, TargetNamespaceMismatchIsSchemaError) {
    EXPECT_EQ(compile_of("E_over.xsd"), 0);
}

/* over021: one component name twice inside a single override body —
 * the overriding set is not unique. */
TEST(Xsd11Override, DuplicateComponentInOneBodyIsSchemaError) {
    EXPECT_EQ(compile_of("F_over.xsd"), 0);
}

/* over022: two override elements designate the same document and
 * both override the same component, conflicting. */
TEST(Xsd11Override, ConflictingOverridesOfSameDocumentAreSchemaError) {
    EXPECT_EQ(compile_of("G_over.xsd"), 0);
}

/* over024: mutual override where the inner override would REPLACE a
 * component of the overriding document — schema error. */
TEST(Xsd11Override, MutualOverrideConflictIsSchemaError) {
    EXPECT_EQ(compile_of("H_over.xsd"), 0);
}

/* over023: mutual override with an EMPTY inner body replaces
 * nothing — legal, and the merged component is the replacement. */
TEST(Xsd11Override, MutualOverrideWithoutReplacementIsLegal) {
    ASSERT_EQ(compile_of("I_over.xsd"), 1);
    EXPECT_EQ(verdict_of("I_over.xsd", "I_v.xml"), 1);
    EXPECT_NE(verdict_of("I_over.xsd", "I_n.xml"), 1);
}

/* over026: an overriding component that overrides nothing is
 * ignored — a kept component's reference to it no longer resolves:
 * schema error. */
TEST(Xsd11Override, ReferenceToIgnoredComponentIsSchemaError) {
    EXPECT_EQ(compile_of("J_over.xsd"), 0);
}

/* over007: an ignored component that nothing references does no
 * harm — legal, and the replaced group drives the content model. */
TEST(Xsd11Override, IgnoredUnreferencedComponentIsLegal) {
    ASSERT_EQ(compile_of("K_over.xsd"), 1);
    EXPECT_EQ(verdict_of("K_over.xsd", "K_v.xml"), 1);
    EXPECT_NE(verdict_of("K_over.xsd", "K_n.xml"), 1);
}

/* 1.0 §4.2.2 preserved: a single redefinition extending its own
 * name folds against the ORIGINAL definition (unlike override). */
TEST(Xsd11Override, LegalRedefineExtensionStillFoldsBase) {
    ASSERT_EQ(compile_of("M_a.xsd"), 1);
    EXPECT_EQ(verdict_of("M_a.xsd", "M_v.xml"), 1);
    EXPECT_NE(verdict_of("M_a.xsd", "M_n.xml"), 1);
}

/* ---- #2184: the Override tails (fixtures ride the pinned
 * xsd11-cases corpus; oracle = the suite metadata) ----------------- */

namespace {

/* verdict against a pinned-corpus schema: 1 valid, 0 invalid,
 * -1 schema/parse failure. */
int cases_verdict_of(const char* schema, const char* instance) {
    std::string sp =
        std::string(LEPTRIS_XSD11_CASES_DIR) + "/" + schema;
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s = leptris_xsd_compile_file(sp.c_str(), &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0) << schema << " must compile";
    EXPECT_EQ(st, LEPTRIS_OK) << schema << " compile status";
    if (!s) return -1;
    std::string ip =
        std::string(LEPTRIS_XSD11_CASES_DIR) + "/" + instance;
    LeptrisStatus pst = (LeptrisStatus)0;
    LeptrisDocument d = leptris_parse_file(ip.c_str(), &pst);
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

/* s4_2_4si02c (IBM "Cyclic dependencies redefine_2"): the redefine
 * location graph must not close back on itself — si02c redefines
 * si02, si02 redefines si02b, si02b redefines si02c. The pinned
 * snapshot's closing document s4_2_4si02b.xsd is now in the corpus
 * (it was missing, so the cycle the oracle judged was not
 * reconstructible). */
TEST(Xsd11Override, RedefineLocationCycleIsSchemaError) {
    LeptrisStatus st = (LeptrisStatus)0;
    std::string sp = std::string(LEPTRIS_XSD11_CASES_DIR) +
                     "/ibmData/schema_invalid/S4_2_4/s4_2_4si02c.xsd";
    LeptrisXsdSchema s = leptris_xsd_compile_file(sp.c_str(), &st);
    if (s) leptris_xsd_free(s);
    EXPECT_TRUE(s == (LeptrisXsdSchema)0 || st != LEPTRIS_OK)
        << "the cyclic redefine must be rejected";
}

/* over027 (§3.2.2.1 a-props-correct): an attribute typed
 * xs:NOTATION — or any type derived from NOTATION — must carry an
 * enumeration (the NOTATION symbols are declared by enumeration
 * only). The suite's revised reading (w3c/xsdtests issue 12) keeps
 * this a schema constraint; the schema must be rejected. */
TEST(Xsd11Override, NotationAttributeWithoutEnumerationIsSchemaError) {
    LeptrisStatus st = (LeptrisStatus)0;
    std::string sp =
        std::string(LEPTRIS_XSD11_CASES_DIR) +
        "/saxonData/Override/over027.xsd";
    LeptrisXsdSchema s = leptris_xsd_compile_file(sp.c_str(), &st);
    if (s) leptris_xsd_free(s);
    EXPECT_TRUE(s == (LeptrisXsdSchema)0 || st != LEPTRIS_OK)
        << "an un-enumerated NOTATION attribute must fail the schema";
}

/* over008 (§3.3.5): ID-typed attribute values are unique within the
 * document — the overridden attributeGroup's required @id is no
 * exception. The .v01 instance (distinct ids) stays valid. */
TEST(Xsd11Override, DocumentScopedIdUniqueness) {
    EXPECT_NE(cases_verdict_of("saxonData/Override/over008.xsd",
                               "saxonData/Override/over008.n01.xml"),
              1);
    EXPECT_NE(cases_verdict_of("saxonData/Override/over008.xsd",
                               "saxonData/Override/over008.n02.xml"),
              1);
    EXPECT_EQ(cases_verdict_of("saxonData/Override/over008.xsd",
                               "saxonData/Override/over008.v01.xml"),
              1);
}

/* ---- #2208: xs:override @schemaLocation through the SET path ---- */

namespace {

/* verdict against a compiled SET: 1 valid, 0 invalid, -1 schema or
 * parse failure. The members compile as one merged schema — the
 * same composition rule leptris_xsd_compile_set documents. */
int set_verdict_of(const char* const* paths, size_t count,
                   const char* instance) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile_set(paths, count, &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0) << "set must compile";
    EXPECT_EQ(st, LEPTRIS_OK) << "set compile status";
    if (!s) return -1;
    std::string ip =
        std::string(LEPTRIS_XSD11_CASES_DIR) + "/" + instance;
    LeptrisStatus pst = (LeptrisStatus)0;
    LeptrisDocument d = leptris_parse_file(ip.c_str(), &pst);
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

/* moxml#590 / leptris#2208 (over001): an xs:override whose
 * overridden document arrives through @schemaLocation must resolve
 * into the set composition — the located base is chased, captured,
 * and its same-named components replaced (§4.3). The override's
 * date shape validates; the base's para shape and the undeclared
 * root do not. The single-document path already passes these rows
 * (the suite manifest is green); the set path drops the located
 * document, leaving the surface with no top-level 'doc'. */
TEST(Xsd11Override, OverrideSchemaLocationChasedThroughSet) {
    std::string sp = std::string(LEPTRIS_XSD11_CASES_DIR) +
                     "/saxonData/Override/over001.xsd";
    const char* paths[1] = {sp.c_str()};
    EXPECT_EQ(set_verdict_of(
                  paths, 1, "saxonData/Override/over001.v01.xml"),
              1);
    EXPECT_NE(set_verdict_of(
                  paths, 1, "saxonData/Override/over001.n01.xml"),
              1);
    EXPECT_NE(set_verdict_of(
                  paths, 1, "saxonData/Override/over001.n02.xml"),
              1);
}

/* #2208 (over001, base-first member order): when the overridden
 * document lands as an EARLIER set member, the override directive's
 * chase sees the target already visited — §4.3 must still apply:
 * the body's components replace the base's same-named ones. The
 * base's para shape must not survive unreplaced (the failure mode:
 * the visited dedup swallowed the whole chase arm, replacement
 * included). */
TEST(Xsd11Override, OverrideReplacesBaseCapturedAsEarlierMember) {
    std::string over = std::string(LEPTRIS_XSD11_CASES_DIR) +
                       "/saxonData/Override/over001.xsd";
    std::string base = std::string(LEPTRIS_XSD11_CASES_DIR) +
                       "/saxonData/Override/over001a.xsd";
    const char* paths[2] = {base.c_str(), over.c_str()};
    EXPECT_EQ(set_verdict_of(
                  paths, 2, "saxonData/Override/over001.v01.xml"),
              1)
        << "the override's date shape must win over the base's";
    EXPECT_NE(set_verdict_of(
                  paths, 2, "saxonData/Override/over001.n01.xml"),
              1);
}

/* Control (the pre-#2150 probe shape): the overridden base as a
 * LATER set member composes correctly — the set-member shape is
 * not the gap; this pins it while the schemaLocation shape heals. */
TEST(Xsd11Override, OverrideBaseAsExplicitSetMemberComposes) {
    std::string over = std::string(LEPTRIS_XSD11_CASES_DIR) +
                       "/saxonData/Override/over001.xsd";
    std::string base = std::string(LEPTRIS_XSD11_CASES_DIR) +
                       "/saxonData/Override/over001a.xsd";
    const char* paths[2] = {over.c_str(), base.c_str()};
    EXPECT_EQ(set_verdict_of(
                  paths, 2, "saxonData/Override/over001.v01.xml"),
              1);
    EXPECT_NE(set_verdict_of(
                  paths, 2, "saxonData/Override/over001.n01.xml"),
              1);
}
