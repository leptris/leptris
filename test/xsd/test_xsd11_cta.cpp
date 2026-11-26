// test/xsd/test_xsd11_cta.cpp — #2149: conditional type assignment
// (XSD 1.1 §3.13.5 / §3.12.4). The xs:alternative chain is captured
// per element declaration (global and local particle forms), and at
// validate time the first alternative whose test evaluates true —
// or the test-less default alternative — types the instance element.
// The selected type's asserts and content model run through the
// existing evaluator. xsi:type on the instance overrides the whole
// chain (§3.3.4.1). A test that fails to evaluate is FALSE
// (§3.12.4: dynamic/compile errors evaluate as false).
#include <gtest/gtest.h>

#include <string>

#include "leptris.h"

namespace {

const char* kDir = LEPTRIS_XSD11_CTA_DIR;

std::string path(const char* name) {
    return std::string(kDir) + "/" + name;
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

/* Global element declaration: the test match must type the element
 * with the alternative (r=0 is not positiveInteger), and the
 * declared type stands when no test matches. */
TEST(Xsd11Cta, GlobalChainSelectsAlternative) {
    EXPECT_EQ(verdict_of("chain_global.xsd", "chain_global_v.xml"), 1);
    EXPECT_NE(verdict_of("chain_global.xsd", "chain_global_n.xml"), 1);
    EXPECT_EQ(verdict_of("chain_global.xsd", "chain_global_fallback.xml"),
              1);
}

/* The same chain captured on a LOCAL element particle. */
TEST(Xsd11Cta, LocalParticleChainSelectsAlternative) {
    EXPECT_EQ(verdict_of("chain_local.xsd", "chain_local_v.xml"), 1);
    EXPECT_NE(verdict_of("chain_local.xsd", "chain_local_n.xml"), 1);
}

/* The final alternative carries no test: it is the type table's
 * {default type definition} — no-match falls back to IT, not to the
 * declared type. */
TEST(Xsd11Cta, TestlessFinalAlternativeIsDefault) {
    EXPECT_EQ(verdict_of("default_notest.xsd", "default_notest_v.xml"), 1);
    EXPECT_NE(verdict_of("default_notest.xsd", "default_notest_n.xml"), 1);
}

/* An alternative may carry an inline complexType instead of a
 * type attribute (§3.12.3: exactly one of the two). The selected
 * inline extension requires the ISBN child. */
TEST(Xsd11Cta, InlineTypeAlternativeSelected) {
    EXPECT_EQ(verdict_of("inline_alt.xsd", "inline_alt_v.xml"), 1);
    EXPECT_NE(verdict_of("inline_alt.xsd", "inline_alt_n.xml"), 1);
}

/* A test-less default typed xs:error makes every no-match instance
 * invalid — xs:error has no valid instances (§2.2.1.1). */
TEST(Xsd11Cta, XsErrorDefaultInvalidates) {
    EXPECT_NE(verdict_of("xserror_default.xsd", "xserror_default_n.xml"),
              1);
    EXPECT_EQ(verdict_of("xserror_default.xsd", "xserror_default_v.xml"),
              1);
}

/* §3.3.4.1: an instance-specified type definition (xsi:type)
 * overrides the conditionally selected one. */
TEST(Xsd11Cta, XsiTypeOverridesSelectedAlternative) {
    EXPECT_EQ(verdict_of("xsi_wins.xsd", "xsi_wins_v.xml"), 1);
    /* and without xsi:type the alternative (ext, requiring b) governs */
    EXPECT_NE(verdict_of("xsi_wins.xsd", "xsi_wins_n.xml"), 1);
}

/* §3.12.4: a test that raises a DYNAMIC error is treated as false —
 * the declared type applies and the document is valid. (Malformed
 * SYNTAX is the schema-error channel: §3.12.2 XPath valid, pinned by
 * Xsd11Cta.MalformedTestSyntaxIsSchemaError below and the
 * test_xsd11_ctatable suite.) */
TEST(Xsd11Cta, TestEvaluationErrorIsFalse) {
    EXPECT_EQ(verdict_of("testerr_fallback.xsd", "testerr_fallback_v.xml"),
              1);
}

/* #2184 (§3.12.2 XPath valid): an alternative test whose expression
 * does not PARSE fails the SCHEMA. */
TEST(Xsd11Cta, MalformedTestSyntaxIsSchemaError) {
    EXPECT_EQ(compile_of("testerr_syntax.xsd"), 0);
}

/* moxml#579: the attributes referenced in the SELECTED alternative's
 * test are exempt from the strict attribute-declaration check — the
 * alternative's own selector attribute must not be false-rejected
 * when the test matches. Unreferenced attributes stay strict, and
 * when no alternative matches nothing is exempt. */
TEST(Xsd11Cta, SelectedTestReferencesAdmitted) {
    /* kind is the selector: admitted when @kind='text' matches,
     * flagged when it does not (base typing applies). */
    EXPECT_EQ(verdict_of("selector_admitted.xsd", "selector_v.xml"), 1);
    EXPECT_NE(verdict_of("selector_admitted.xsd", "selector_n.xml"), 1);
    /* only the referenced attribute is exempt: lang admitted on a
     * match, kind strict even on a match. */
    EXPECT_EQ(verdict_of("only_referenced_exempt.xsd", "lang_v.xml"), 1);
    EXPECT_NE(verdict_of("only_referenced_exempt.xsd",
                         "unreferenced_n.xml"),
              1);
    EXPECT_NE(verdict_of("only_referenced_exempt.xsd",
                         "lang_plus_kind_n.xml"),
              1);
}
