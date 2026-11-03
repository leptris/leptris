// leptris-rs#35 — the 1.9.403.0 regression wave: the #25/#1936
// strictness fixes introduced or exposed four defects against the
// W3C suite. All cases expected VALID; all passed on 1.9.402.0.
//
//   1. fixed + empty CHILD elements in a sequence (isDefault005/006/
//      025/026/060_1) — §4.3.1.2: empty content satisfies the fixed
//      facet; only the bare-element leg was fixed.
//   2. mixed="true" on a particle-less complexType admits character
//      data (ctA033, ctA035).
//   3. xs:redefine composition compiles the redefined body's
//      particles (idF025/033/034, idG018/019/027/028).
//   4. the default value supplies content on an empty list-typed
//      element (stZ072).
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "leptris.h"

namespace {

#ifndef LEPTRIS_XSD_CASES_DIR
#define LEPTRIS_XSD_CASES_DIR "libxml2-cases"
#endif

std::string cases_path(const char* sub) {
    return std::string(LEPTRIS_XSD_CASES_DIR) + "/" + sub;
}

// Returns 1 (valid), 0 (invalid), -1 (other).
int validate_case(const std::string& base) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s = leptris_xsd_compile_file(
        (base + ".xsd").c_str(), &st);
    if (!s) return -1;
    LeptrisDocument d = leptris_parse_file((base + ".xml").c_str(), &st);
    if (!d) {
        leptris_xsd_free(s);
        return -1;
    }
    int ok = leptris_xsd_validate(s, d);
    leptris_document_free(d);
    leptris_xsd_free(s);
    return ok;
}

} // namespace

TEST(Rs35Regression, FixedEmptyChildElementsValidate) {
    EXPECT_EQ(validate_case(cases_path("rs35_fixed_empty/isdefault005")), 1);
}

TEST(Rs35Regression, MixedParticlelessTypeAdmitsText) {
    EXPECT_EQ(validate_case(cases_path("rs35_mixed_particleless/ctA033")), 1);
}

TEST(Rs35Regression, RedefineExtensionKeepsParticles) {
    EXPECT_EQ(validate_case(cases_path("rs35_redefine_ext/idF025")), 1);
}

TEST(Rs35Regression, DefaultSuppliesListContent) {
    EXPECT_EQ(validate_case(cases_path("rs35_list_default/stZ072")), 1);
}

/* The FULL rs#35 wave: every W3C case named in the issue (expected
 * VALID, all passing on 1.9.402.0). Real msData fixtures verbatim. */
namespace {
struct Rs35Case {
    const char* schema;
    const char* instance;
};
// The msMeta manifest's own schema assignment per instance (the
// isDefault family shares schemas: 005/006 → isdefault002,
// 025/026 → isdefault004, 060_1 → test95960_1).
int validate_pair(const Rs35Case& c) {
    LeptrisStatus st = LEPTRIS_OK;
    std::string base = cases_path("rs35_w3c");
    LeptrisXsdSchema s =
        leptris_xsd_compile_file((base + "/" + c.schema).c_str(), &st);
    if (!s) return -1;
    LeptrisDocument d =
        leptris_parse_file((base + "/" + c.instance).c_str(), &st);
    if (!d) {
        leptris_xsd_free(s);
        return -1;
    }
    int ok = leptris_xsd_validate(s, d);
    leptris_document_free(d);
    leptris_xsd_free(s);
    return ok;
}
} // namespace

TEST(Rs35Regression, AllFifteenW3cCasesValidate) {
    static const Rs35Case cases[] = {
        {"isdefault002.xsd", "isdefault005.xml"},
        {"isdefault002.xsd", "isdefault006.xml"},
        {"isdefault004.xsd", "isdefault025.xml"},
        {"isdefault004.xsd", "isdefault026.xml"},
        {"test95960_1.xsd", "test95960_1.xml"},
        {"ctA033.xsd", "ctA033.xml"},
        {"ctA035.xsd", "ctA035.xml"},
        {"idF025.xsd", "idF025.xml"},
        {"idF033.xsd", "idF033.xml"},
        {"idF034.xsd", "idF034.xml"},
        {"idG018.xsd", "idG018.xml"},
        {"idG019.xsd", "idG019.xml"},
        {"idG027.xsd", "idG027.xml"},
        {"idG028.xsd", "idG028.xml"},
        {"stZ072.xsd", "stZ072.xml"},
    };
    for (const auto& c : cases)
        EXPECT_EQ(validate_pair(c), 1) << c.schema << " / " << c.instance;
}
