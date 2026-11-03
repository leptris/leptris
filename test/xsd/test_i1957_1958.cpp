// leptris#1957 + leptris#1958 — two XSD component-resolution defects.
//
// #1957: nested attributeGroup refs — the level-2 members of a group
// referenced from inside another attributeGroup are rejected by
// strict admission (the pre-pass captures group defs in document
// order, so an inner ref that names a LATER group expands empty);
// and a complexType whose content model is only an attributeGroup
// ref + mixed="true" reported "the content model is empty".
//
// #1958: multi-document component resolution — an imported
// no-targetNamespace same-named complexType shadowed the importing
// namespace's own declaration, and xs:redefine'd simpleTypes were
// not registered ("type not found in the schema").
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

int validate_case(const std::string& schema, const std::string& doc) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s =
        leptris_xsd_compile_file(schema.c_str(), &st);
    if (!s) return -1;
    LeptrisDocument d = leptris_parse_file(doc.c_str(), &st);
    if (!d) {
        leptris_xsd_free(s);
        return -1;
    }
    int ok = leptris_xsd_validate(s, d);
    if (getenv("I1958_VERBOSE"))
        for (size_t i = 0; i < leptris_xsd_error_count(s); i++)
            printf("    err: %s\n", leptris_xsd_error_at(s, i));
    leptris_document_free(d);
    leptris_xsd_free(s);
    return ok;
}

} // namespace

TEST(I1957, NestedAttrGroupLevel2AdmitsAndMixedAdmitsText) {
    EXPECT_EQ(validate_case(cases_path("i1957/schema.xsd"),
                            cases_path("i1957/instance.xml")),
              1);
}

TEST(I1958, ImportedSameNameTypeDoesNotShadow) {
    EXPECT_EQ(validate_case(cases_path("i1958/schF1_a.xsd"),
                            cases_path("i1958/schF1.xml")),
              1);
}

TEST(I1958, RedefinedSimpleTypeRegistered) {
    EXPECT_EQ(validate_case(cases_path("i1958/schP2_a.xsd"),
                            cases_path("i1958/schP2.xml")),
              1);
}

TEST(I1958, RedefinedSimpleTypeFacetEnforced) {
    /* "abc" is 3 chars — the redefined minLength 4 rejects it (the
     * facet proves the REDEFINED type, not the original, resolved). */
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s = leptris_xsd_compile_file(
        cases_path("i1958/schP2_a.xsd").c_str(), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char too_short[] =
        "<e1 xmlns=\"ns-a\">abc</e1>";
    LeptrisDocument d = leptris_parse_string(too_short,
                                             strlen(too_short), &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, d), 0)
        << "minLength 4 rejected nothing";
    size_t errs = leptris_xsd_error_count(s);
    for (size_t i = 0; i < errs; i++)
        printf("    err: %s\n", leptris_xsd_error_at(s, i));
    leptris_document_free(d);
    leptris_xsd_free(s);
}
