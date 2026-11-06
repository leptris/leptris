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


// ---- leptris-rs#45: union-typed fixed constraints compare in the
// union's VALUE SPACE — the first member type validating the fixed
// lexical must also validate the instance lexical (stE050: "1" and
// "true" both xs:boolean; stE055: "1.0"/"1.000" both xs:float).
// Raw strcmp wrongly rejected value-equal forms. Real W3C fixtures
// (msData/simpleType, vendored under libxml2-cases/stE-union-fixed).
TEST(XsdUnionFixed, StE050BooleanMemberAcceptsBoth) {
    EXPECT_EQ(validate_case(cases_path("stE-union-fixed/stE050.xsd"),
                            cases_path("stE-union-fixed/stE050.xml")),
              1);
}

TEST(XsdUnionFixed, StE055FloatMemberAcceptsBothSpellings) {
    EXPECT_EQ(validate_case(cases_path("stE-union-fixed/stE055.xsd"),
                            cases_path("stE-union-fixed/stE055.xml")),
              1);
}

TEST(XsdUnionFixed, NonMemberValueStillRejects) {
    /* "2.0" validates through the union (float) but differs from the
     * fixed 1.0 in the value space — the fixed constraint holds. */
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s = leptris_xsd_compile_file(
        cases_path("stE-union-fixed/stE055.xsd").c_str(), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char other[] = "<root>2.000</root>";
    LeptrisDocument d = leptris_parse_string(other, strlen(other), &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, d), 0)
        << "fixed 1.0 accepted the value 2.0";
    leptris_document_free(d);
    leptris_xsd_free(s);
}

// ---- leptris-rs#46 (elemZ020): particle typing is ns-aware ------
// Real W3C fixture msData/element/elemZ020: a form="unqualified"
// local particle (xs:int) and a same-name substitution member
// (xs:boolean) discriminate by namespace. The namespaced child
// types through its member declaration; the no-ns child types
// through the unqualified particle.

TEST(XsdSubstNs, ElemZ020RealFixtureValidates) {
    EXPECT_EQ(validate_case(cases_path("elemZ020-subst-ns/elemZ020.xsd"),
                            cases_path("elemZ020-subst-ns/elemZ020.xml")),
              1);
}

TEST(XsdSubstNs, NamespacedChildTypedByMemberNotUnqualifiedParticle) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s = leptris_xsd_compile_file(
        cases_path("elemZ020-subst-ns/elemZ020.xsd").c_str(), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    /* "true" is boolean (the member), not int (the unqualified
     * particle) — the ns-strict member binding types it. */
    const char q[] = "<?xml version=\"1.0\"?>\n"
                     "<foo:root xmlns:foo=\"foo\">"
                     "<foo:foo>true</foo:foo></foo:root>";
    LeptrisDocument d = leptris_parse_string(q, strlen(q), &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, d), 1);
    leptris_document_free(d);
    /* int lexical in the member's slot still rejects */
    const char bad[] = "<?xml version=\"1.0\"?>\n"
                       "<foo:root xmlns:foo=\"foo\">"
                       "<foo:foo>123</foo:foo></foo:root>";
    d = leptris_parse_string(bad, strlen(bad), &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, d), 0);
    leptris_document_free(d);
    /* the no-ns child rides the form="unqualified" particle */
    const char u[] = "<?xml version=\"1.0\"?>\n"
                     "<foo:root xmlns:foo=\"foo\">"
                     "<foo>123</foo></foo:root>";
    d = leptris_parse_string(u, strlen(u), &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, d), 1);
    leptris_document_free(d);
    /* boolean lexical in the unqualified particle's slot rejects */
    const char bad2[] = "<?xml version=\"1.0\"?>\n"
                        "<foo:root xmlns:foo=\"foo\">"
                        "<foo>true</foo></foo:root>";
    d = leptris_parse_string(bad2, strlen(bad2), &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, d), 0);
    leptris_document_free(d);
    leptris_xsd_free(s);
}

// ---- leptris-rs#47: the 1.9.407 regex-wave regression claim -------
// reG8: char-range [0-z]* over ASCII punctuation; Cyrillic: the
// \p{IsCyrillic} block escape over block members. Both expected VALID.

TEST(XsdRegexRs47, ReG8CharRangeStaysValid) {
    EXPECT_EQ(validate_case(cases_path("rs47-regex/reG8.xsd"),
                            cases_path("rs47-regex/reG8.xml")), 1);
}

TEST(XsdRegexRs47, CyrillicBlockEscapeStaysValid) {
    EXPECT_EQ(validate_case(cases_path("rs47-regex/Cyrillic.xsd"),
                            cases_path("rs47-regex/Cyrillic.xml")), 1);
}


