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
