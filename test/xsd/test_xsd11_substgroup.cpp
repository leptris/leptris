// test/xsd/test_xsd11_substgroup.cpp — #2184: the substitution-group
// / weak-wildcard endgame clusters (#2234 disposition items 5/8/9).
// Every spec rides the pinned xsd11-cases corpus (oracle: the
// w3c/xsdtests suite metadata) or a local fixture in
// xsd/xsd11-dispositions-subst/.
//
// Clusters:
//   (1) weak-wildcard lax typing (§3.8.6.4 + assessment) — the ibm
//       S3_8_6/S3_10_1 rows: the element-vs-wildcard overlap shape is
//       1.1-by-syntax-evidence (a legal 1.0 schema cannot carry it),
//       and an element ADMITTED by the wildcard after the shadowing
//       local particle's window is exhausted resolves its GLOBAL
//       declaration, whose type must stay compatible with the
//       shadowed local particle's (the §3.8.6 governing-type law).
//   (2) substitution-group compile constraints — circular
//       substitution groups, member-type substitutability for EVERY
//       head, cos-element-consistent between a local particle and a
//       substitution member of one model, and the UPA an all inherits
//       when two of its particles share a substitution member.
//   (3) union substitutability in substitution groups — union(x,y) is
//       not validly derived from union(x,y,z), and a union member is
//       not substitutable when the union is derived by restriction
//       (the same facet-free-path law the xsi:type lane pins).
//   (4) the value-space riders in the same disposition rows: the
//       base64Binary lexical requires RFC 2045 padding, and a union's
//       memberTypes tokens resolve by local name (the list itemType
//       convention) so the members' facets govern the value.
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>

#include "leptris.h"

namespace {

const char* kCases = LEPTRIS_XSD11_CASES_DIR;

// Compile one schema; returns the engine status.
LeptrisStatus compile_status(const char* rel) {
    char p[1024];
    snprintf(p, sizeof(p), "%s/%s", kCases, rel);
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s = leptris_xsd_compile_file(p, &st);
    if (s) leptris_xsd_free(s);
    return st;
}

// Compile+parse+validate one corpus row; returns the document verdict
// (1 valid, 0 invalid, -1 the schema or document did not load).
int verdict(const char* schema, const char* instance) {
    char spath[1024], ipath[1024];
    snprintf(spath, sizeof(spath), "%s/%s", kCases, schema);
    snprintf(ipath, sizeof(ipath), "%s/%s", kCases, instance);
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s = leptris_xsd_compile_file(spath, &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0)
        << schema << " status " << (int)st << " "
        << (s && leptris_xsd_error(s) ? leptris_xsd_error(s) : "");
    if (!s) return -1;
    EXPECT_EQ(st, LEPTRIS_OK) << schema << " compile status "
                              << (int)st;
    LeptrisDocument d = leptris_parse_file(ipath, &st);
    EXPECT_NE(d, (LeptrisDocument)nullptr)
        << instance << " status " << (int)st;
    if (!d) {
        leptris_xsd_free(s);
        return -1;
    }
    int v = leptris_xsd_validate(s, d);
    if (v != 1) printf("  [%s] %s\n", instance,
                       leptris_xsd_error_at(s, 0));
    leptris_document_free(d);
    leptris_xsd_free(s);
    return v;
}

}  // namespace

// ---- (1) weak wildcards: the compile-side dialect evidence ---------

// s3_8_6v01/s3_8_6ii01 (identical schemas): <element minOccurs="0"/>
// followed by a lax <any/> over the same name — a UPA violation in
// 1.0, legal in 1.1 (the wildcard is weak). The overlap shape itself
// is the 1.1 evidence: no legal 1.0 schema can carry it.
TEST(Xsd11SubstGroup, WeakWildcardOverlapCompilesClean) {
    EXPECT_EQ(compile_status("ibmData/valid/S3_8_6/s3_8_6v01.xsd"),
              LEPTRIS_OK);
    EXPECT_EQ(
        compile_status("ibmData/instance_invalid/S3_8_6/s3_8_6ii01.xsd"),
        LEPTRIS_OK);
}

// s3_10_1v04: <element ref minOccurs="0"/> + a strict <any/>; and
// s3_10_1ii06: a notNamespace wildcard BEFORE the element particle.
TEST(Xsd11SubstGroup, WeakWildcardOverlapReversedCompilesClean) {
    EXPECT_EQ(compile_status("ibmData/valid/S3_10_1/s3_10_1v04.xsd"),
              LEPTRIS_OK);
    EXPECT_EQ(
        compile_status("ibmData/instance_invalid/S3_10_1/s3_10_1ii06.xsd"),
        LEPTRIS_OK);
}

// The choice-branch overlap stays REJECTED: any4_0's wildcard branch
// covers the element branch of a choice and the pinned 1.0 posture
// keeps the gate there (the evidence pass reads sequence geometry
// only).
TEST(Xsd11SubstGroup, ChoiceOverlapStillRejected) {
    EXPECT_NE(
        compile_status("../libxml2-cases/any4_0.xsd"),
        LEPTRIS_OK);
}

// ---- (1) weak wildcards: the assessment side -----------------------

// s3_10_1v04.xml: the second <z> rides the (strict) wildcard once the
// ref particle's window is spent; the global declaration z:integer
// types it and "1" is valid.
TEST(Xsd11SubstGroup, WeakWildcardStrictResolveValid) {
    EXPECT_EQ(verdict("ibmData/valid/S3_10_1/s3_10_1v04.xsd",
                      "ibmData/valid/S3_10_1/s3_10_1v04.xml"),
              1);
}

// s3_8_6v01.xml: the second <x> rides the lax wildcard, resolves the
// GLOBAL x (integer), and the governing-type law bites: integer is
// not compatible with the shadowed local particle's string — the
// instance is INVALID (the suite's verdict).
TEST(Xsd11SubstGroup, WeakWildcardGoverningTypeCompatible) {
    EXPECT_EQ(verdict("ibmData/valid/S3_8_6/s3_8_6v01.xsd",
                      "ibmData/valid/S3_8_6/s3_8_6v01.xml"),
              0);
    EXPECT_EQ(verdict("ibmData/instance_invalid/S3_8_6/s3_8_6ii01.xsd",
                      "ibmData/instance_invalid/S3_8_6/s3_8_6ii01.xml"),
              0);
}

// s3_10_1ii06.xml: <b:b> and <a> are excluded by the wildcard's
// notNamespace and no particle claims them.
TEST(Xsd11SubstGroup, WeakWildcardNotNamespaceInvalid) {
    EXPECT_EQ(
        verdict("ibmData/instance_invalid/S3_10_1/s3_10_1ii06.xsd",
                "ibmData/instance_invalid/S3_10_1/s3_10_1ii06.xml"),
        0);
}

// ---- (4a) base64Binary lexical strictness --------------------------

// d3_3_17ii01: "123456" is not a base64Binary lexical (the RFC 2045
// padding is mandatory — six chars cannot close a quantum).
TEST(Xsd11SubstGroup, Base64BinaryRequiresPadding) {
    EXPECT_EQ(verdict("ibmData/instance_invalid/D3_3_17/d3_3_17ii01.xsd",
                      "ibmData/instance_invalid/D3_3_17/d3_3_17ii01.xml"),
              0);
}

// ---- (4b) union memberTypes resolve by local name ------------------

// s3_16_2ii06: unionAll's memberTypes spell prefixed LOCAL names; the
// members' own facets govern (313/a1z match no member — the instance
// is invalid).
TEST(Xsd11SubstGroup, UnionMemberTypesResolveLocally) {
    EXPECT_EQ(
        verdict("ibmData/instance_invalid/S3_16_2/s3_16_2ii06.xsd",
                "ibmData/instance_invalid/S3_16_2/s3_16_2ii06.xml"),
        0);
}

// ---- (2) substitution-group compile constraints --------------------

// s2_2_2si01: circular substitution groups (elem0 heads elem1, elem1
// heads elem0).
TEST(Xsd11SubstGroup, CircularSubstitutionGroupRejected) {
    EXPECT_EQ(
        compile_status("ibmData/schema_invalid/S2_2_2/s2_2_2si01.xsd"),
        LEPTRIS_ERROR_PARSE);
}

// s2_2_2si02: elem2's type (string) is not validly substitutable for
// the second head's (integer) — the multi-head form checks every
// head.
TEST(Xsd11SubstGroup, MemberTypeSubstitutableForEachHead) {
    EXPECT_EQ(
        compile_status("ibmData/schema_invalid/S2_2_2/s2_2_2si02.xsd"),
        LEPTRIS_ERROR_PARSE);
}

// The green multi-head twins keep compiling (subsgroup001's n is a
// member of chapContent:string and appendixContent:anyType — both
// substitutable).
TEST(Xsd11SubstGroup, GreenMultiHeadSubstitutionKeepsCompiling) {
    EXPECT_EQ(compile_status("saxonData/Subsgroup/subsgroup001.xsd"),
              LEPTRIS_OK);
    EXPECT_EQ(compile_status("saxonData/Subsgroup/subsgroup002.xsd"),
              LEPTRIS_OK);
    EXPECT_EQ(compile_status("ibmData/valid/S2_2_2/s2_2_2v01.xsd"),
              LEPTRIS_OK);
    EXPECT_EQ(compile_status("ibmData/valid/S2_2_2/s2_2_2v02.xsd"),
              LEPTRIS_OK);
}

// subsgroup901: cos-element-consistent — the locally declared
// <element name="n" type="xs:date"/> conflicts with the substitution
// member n (string) the appendixContent particle pulls into the same
// model.
TEST(Xsd11SubstGroup, ElementDeclarationsConsistentWithMember) {
    EXPECT_EQ(compile_status("saxonData/Subsgroup/subsgroup901.bad.xsd"),
              LEPTRIS_ERROR_PARSE);
}

// subsgroup903: para sits in BOTH heads referenced by one xs:all —
// the all inherits the UPA violation (para can satisfy either
// particle).
TEST(Xsd11SubstGroup, SharedMemberViolatesAllUpa) {
    EXPECT_EQ(compile_status("saxonData/Subsgroup/subsgroup903.bad.xsd"),
              LEPTRIS_ERROR_PARSE);
}

// The green alls with substitution groups keep compiling (each all's
// particle substitution sets are pairwise disjoint).
TEST(Xsd11SubstGroup, GreenAllsWithSubstitutionKeepsCompiling) {
    EXPECT_EQ(compile_status("saxonData/All/all002.xsd"), LEPTRIS_OK);
    EXPECT_EQ(compile_status("saxonData/All/all221.xsd"), LEPTRIS_OK);
    EXPECT_EQ(compile_status("saxonData/All/all225.xsd"), LEPTRIS_OK);
}

// ---- (3) union substitutability in substitution groups -------------

// simple011: sub-chap = union(date,time) is not validly derived from
// chap = union(date,dateTime,time) — a union derives from no atomic
// member, so the substitution member's restriction fails.
TEST(Xsd11SubstGroup, UnionSubsetNotDerivedFromWiderUnion) {
    EXPECT_EQ(compile_status("saxonData/Simple/simple011.bad.xsd"),
              LEPTRIS_ERROR_PARSE);
}

// simple014: a union member is not substitutable when the union is
// derived by restriction — chap carries a pattern, so the member path
// through date is blocked.
TEST(Xsd11SubstGroup, RestrictedUnionBlocksMemberPath) {
    EXPECT_EQ(compile_status("saxonData/Simple/simple014.bad.xsd"),
              LEPTRIS_ERROR_PARSE);
    EXPECT_EQ(compile_status("saxonData/Simple/simple015.bad.xsd"),
              LEPTRIS_ERROR_PARSE);
}
