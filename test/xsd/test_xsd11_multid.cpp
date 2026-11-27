// test/xsd/test_xsd11_multid.cpp — #2184: the XSD 1.1 ID bookkeeping
// wave. Two halves:
//
// (1) the one-ID-per-element constraint (§3.4.4.3) is 1.0-ONLY: a
//     type whose DECLARED attribute table carries two or more
//     ID-typed uses cannot be a legal 1.0 schema, so the schema is
//     1.1 by syntax evidence (the #2149 posture) and the gate does
//     not apply. A version-marked 1.0 schema keeps failing, and the
//     rs#73 wildcard shapes (one declared ID + a wildcard-admitted
//     global ID) keep their pinned 1.0 verdicts (test_xsd_compile.cpp
//     AttZ014aIdCountInvalid10/AttZ014bDeclaredAndAdmittedIdInvalid10).
//
// (2) the ID/IDREF bookkeeping the 1.1 ID-family corpus pins:
//     registration — atomic ID values (attributes, element content,
//     defaulted/fixed-filled values) and every item of a list-of-ID
//     value enter the resolution pool, with the FIRST list item also
//     entering the uniqueness table (id004.v01: repeats across
//     elements of multi-item lists are legal; id008.v02: atomic
//     repeats are not); union values register only when the ACTUAL
//     member type (first accepting member) is ID-derived
//     (id006/id007/ii17 — "true" riding the boolean arm is not an
//     ID). Resolution — every IDREF/IDREFS value must match some ID
//     registered in the same document (§C.4 integrity: ii04-ii18,
//     id005/id006/id008).
//
// The suite fixtures ride the pinned xsd11-cases corpus (oracle: the
// w3c/xsdtests suite metadata).
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>

#include "leptris.h"

namespace {

const char* kCases = LEPTRIS_XSD11_CASES_DIR;

// Compile+parse+validate one corpus row; returns the document verdict
// (1 valid, 0 invalid, -1 the schema or document did not load).
int verdict(const char* schema, const char* instance) {
    char spath[1024], ipath[1024];
    snprintf(spath, sizeof(spath), "%s/%s", kCases, schema);
    snprintf(ipath, sizeof(ipath), "%s/%s", kCases, instance);
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s = leptris_xsd_compile_file(spath, &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0)
        << schema << " status " << (int)st;
    if (!s) return -1;
    LeptrisDocument d = leptris_parse_file(ipath, &st);
    EXPECT_NE(d, (LeptrisDocument)nullptr)
        << instance << " status " << (int)st;
    if (!d) {
        leptris_xsd_free(s);
        return -1;
    }
    int v = leptris_xsd_validate(s, d);
    if (v != 1) printf("  [%s] %s\n", instance, leptris_xsd_error_at(s, 0));
    leptris_document_free(d);
    leptris_xsd_free(s);
    return v;
}

}  // namespace

// ---- (1) the one-ID gate: declared multi-ID is 1.1 evidence ---------

// saxonData/Id id001: two ID-typed attribute uses on one type — legal
// since 1.1; the type's own table is the evidence (illegal in 1.0, so
// the schema cannot be a 1.0 schema).
TEST(Xsd11MultiId, TwoDeclaredIdAttrsValid) {
    EXPECT_EQ(verdict("saxonData/Id/id001.xsd", "saxonData/Id/id001.v01.xml"),
              1);
}

// ibm S3_3_4 v12: a named complexType with id1 + id2.
TEST(Xsd11MultiId, NamedTypeTwoIdAttrsValid) {
    EXPECT_EQ(verdict("ibmData/valid/S3_3_4/s3_3_4v12.xsd",
                      "ibmData/valid/S3_3_4/s3_3_4v12.xml"),
              1);
}

// ibm v22: the two ID uses arrive through the EXTENSION fold of the
// base type's table (from_base rows) — the folded table is the
// evidence.
TEST(Xsd11MultiId, ExtensionFoldedIdAttrsValid) {
    EXPECT_EQ(verdict("ibmData/valid/S3_3_4/s3_3_4v22.xsd",
                      "ibmData/valid/S3_3_4/s3_3_4v22.xml"),
              1);
}

// ibm v23: the multi-ID type is the ROOT element's type.
TEST(Xsd11MultiId, RootTypeMultiIdValid) {
    EXPECT_EQ(verdict("ibmData/valid/S3_3_4/s3_3_4v23.xsd",
                      "ibmData/valid/S3_3_4/s3_3_4v23.xml"),
              1);
}

// ibm v29: two ID uses with DEFAULT values (the defaulted values also
// register — see the bookkeeping half below).
TEST(Xsd11MultiId, MultiIdWithDefaultsValid) {
    EXPECT_EQ(verdict("ibmData/valid/S3_3_4/s3_3_4v29.xsd",
                      "ibmData/valid/S3_3_4/s3_3_4v29.xml"),
              1);
}

// A schema MARKED 1.0 keeps the one-ID gate: the relaxation keys on
// 1.1 evidence, and a version="1.0" schema with two declared ID uses
// is judged in the 1.0 dialect (the attZ014a-11 inverse).
TEST(Xsd11MultiId, Marked10MultiIdStillInvalid) {
    const char schema[] =
        "<?xml version='1.0'?>"
        "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'"
        " version='1.0'>"
        " <xs:element name='root'>"
        "  <xs:complexType>"
        "   <xs:attribute name='a' type='xs:ID'/>"
        "   <xs:attribute name='b' type='xs:ID'/>"
        "  </xs:complexType>"
        " </xs:element>"
        "</xs:schema>";
    const char instance[] = "<root a='x' b='y'/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s = leptris_xsd_compile(schema, strlen(schema), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0) << "status " << (int)st;
    LeptrisDocument d =
        leptris_parse_string(instance, strlen(instance), &st);
    ASSERT_NE(d, (LeptrisDocument)nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, d), 0);
    leptris_document_free(d);
    leptris_xsd_free(s);
}

// id003: ONE declared ID use + a lax anyAttribute admitting the
// global id-two — the type's own table is 1.0-legal, so this is NOT
// multi-ID evidence and the rs#73 verdicts stand. The suite tags the
// same shape VALID (v02/id003) — the three rows this class leaves red
// (id003.v01, id001.v01 vs id002, ibm v02) need the OWNER-level
// engine-dialect flip the disposition names: an unmarked schema whose
// wildcard admits a second ID-typed global cannot be told apart from
// the 1.0 suite's attZ014b pin by schema content alone.
TEST(Xsd11MultiId, WildcardAdmittedSecondIdStillInvalid) {
    EXPECT_EQ(verdict("saxonData/Id/id002.xsd", "saxonData/Id/id001.v01.xml"),
              0);
    EXPECT_EQ(verdict("saxonData/Id/id003.xsd", "saxonData/Id/id003.v01.xml"),
              0);
}

// ---- (2) the ID/IDREF bookkeeping -----------------------------------

// ibm ii04: the ID-typed element's value registers; the IDREF does
// not resolve (ads_ vs the ID ads).
TEST(Xsd11Bookkeeping, IdrefUnresolvedInvalid) {
    EXPECT_EQ(verdict("ibmData/instance_invalid/S3_3_4/s3_3_4ii04.xsd",
                      "ibmData/instance_invalid/S3_3_4/s3_3_4ii04.xml"),
              0);
}

// ibm ii06: asd_ is the only ID; idref asd does not resolve.
TEST(Xsd11Bookkeeping, IdrefNearMissInvalid) {
    EXPECT_EQ(verdict("ibmData/instance_invalid/S3_3_4/s3_3_4ii06.xsd",
                      "ibmData/instance_invalid/S3_3_4/s3_3_4ii06.xml"),
              0);
}

// ibm ii07: list-of-ID element values register their items;
// idref u1234 does not resolve.
TEST(Xsd11Bookkeeping, ListItemsRegisterResolve) {
    EXPECT_EQ(verdict("ibmData/instance_invalid/S3_3_4/s3_3_4ii07.xsd",
                      "ibmData/instance_invalid/S3_3_4/s3_3_4ii07.xml"),
              0);
}

// saxon id005.v01: IDREFS resolve against list-of-ID items INCLUDING
// an item that is not its list's first (ddd in <id>ccc ddd</id>).
TEST(Xsd11Bookkeeping, ListSecondItemResolvesIdrefs) {
    EXPECT_EQ(verdict("saxonData/Id/id005.xsd", "saxonData/Id/id005.v01.xml"),
              1);
}

// id005.n01: idref hhh matches nothing.
TEST(Xsd11Bookkeeping, ListIdrefUnresolvedInvalid) {
    EXPECT_EQ(verdict("saxonData/Id/id005.xsd", "saxonData/Id/id005.n01.xml"),
              0);
}

// saxon id006: the list item type is a union(ID, integer) — items
// riding the integer arm are not IDs (no NCName check, no
// registration); the IDREF-or-integer idrefs likewise only bind the
// IDREF arm. v01 is valid; n01's hhh does not resolve.
TEST(Xsd11Bookkeeping, UnionItemActualMemberDecides) {
    EXPECT_EQ(verdict("saxonData/Id/id006.xsd", "saxonData/Id/id006.v01.xml"),
              1);
    EXPECT_EQ(verdict("saxonData/Id/id006.xsd", "saxonData/Id/id006.n01.xml"),
              0);
}

// saxon id007: each attribute's union arms split the items into IDs
// and IDREFs by prefix; the cross-attribute duality (A001 is an ID
// via mixedB and an IDREF via mixedA) makes v01 valid, while n01's
// B999 rides the IDREF arm and matches no ID.
TEST(Xsd11Bookkeeping, UnionArmDuality) {
    EXPECT_EQ(verdict("saxonData/Id/id007.xsd", "saxonData/Id/id007.v01.xml"),
              1);
    EXPECT_EQ(verdict("saxonData/Id/id007.xsd", "saxonData/Id/id007.n01.xml"),
              0);
    EXPECT_EQ(verdict("saxonData/Id/id007.xsd", "saxonData/Id/id007.n02.xml"),
              0);
}

// saxon id008.v02: pseudoID (complexType with simpleContent over
// xs:ID) element values are atomic IDs — bbb on two id elements is a
// duplicate. v01's swap shape stays valid; v03's ddd does not resolve.
TEST(Xsd11Bookkeeping, AtomicContentDuplicateInvalid) {
    EXPECT_EQ(verdict("saxonData/Id/id008.xsd", "saxonData/Id/id008.v01.xml"),
              1);
    EXPECT_EQ(verdict("saxonData/Id/id008.xsd", "saxonData/Id/id008.v02.xml"),
              0);
    EXPECT_EQ(verdict("saxonData/Id/id008.xsd", "saxonData/Id/id008.v03.xml"),
              0);
}

// saxon id004: a LIST value's FIRST item joins the uniqueness table
// (atomic-width values only for conflicts) — v01's cross-element
// repeats of multi-item values are legal, while n01 repeats zzz as
// two id elements' FIRST items and n02 repeats aaa (attribute first
// vs element first).
TEST(Xsd11Bookkeeping, ListFirstItemUniqueness) {
    EXPECT_EQ(verdict("saxonData/Id/id004.xsd", "saxonData/Id/id004.v01.xml"),
              1);
    EXPECT_EQ(verdict("saxonData/Id/id004.xsd", "saxonData/Id/id004.n01.xml"),
              0);
    EXPECT_EQ(verdict("saxonData/Id/id004.xsd", "saxonData/Id/id004.n02.xml"),
              0);
}

// saxon id010: an ID attribute's DEFAULT registers — v01's defaulted
// para001 beside the explicit para002 is valid; n01 (default vs
// explicit para001) and n02 (both defaulted) are duplicates.
TEST(Xsd11Bookkeeping, AttrDefaultRegisters) {
    EXPECT_EQ(verdict("saxonData/Id/id010.xsd", "saxonData/Id/id010.v01.xml"),
              1);
    EXPECT_EQ(verdict("saxonData/Id/id010.xsd", "saxonData/Id/id010.n01.xml"),
              0);
    EXPECT_EQ(verdict("saxonData/Id/id010.xsd", "saxonData/Id/id010.n02.xml"),
              0);
}

// saxon id011: an ID attribute's FIXED value acts as the default when
// the attribute is absent — two para elements both carry para001.
TEST(Xsd11Bookkeeping, AttrFixedActsAsDefault) {
    EXPECT_EQ(verdict("saxonData/Id/id011.xsd", "saxonData/Id/id011.n02.xml"),
              0);
}

// saxon id014/id015: an ID-typed ELEMENT's default/fixed fills empty
// content — two <id/> elements both carry p1.
TEST(Xsd11Bookkeeping, ElementFillRegisters) {
    EXPECT_EQ(verdict("saxonData/Id/id014.xsd", "saxonData/Id/id014.n01.xml"),
              0);
    EXPECT_EQ(verdict("saxonData/Id/id015.xsd", "saxonData/Id/id015.n01.xml"),
              0);
}

// saxon id016: DEFAULTED IDREF values must resolve — n01 supplies
// id=para002 while the defaulted idref para001 matches nothing; n02's
// second para leaves the idref defaulted and para001 unregistered.
TEST(Xsd11Bookkeeping, DefaultedIdrefMustResolve) {
    EXPECT_EQ(verdict("saxonData/Id/id016.xsd", "saxonData/Id/id016.n01.xml"),
              0);
    EXPECT_EQ(verdict("saxonData/Id/id016.xsd", "saxonData/Id/id016.n02.xml"),
              0);
}

// ibm v26/v28: the defaulted ID makes the (explicit or defaulted)
// idref resolve — the guard against over-rejection.
TEST(Xsd11Bookkeeping, DefaultedIdResolvesIdref) {
    EXPECT_EQ(verdict("ibmData/valid/S3_3_4/s3_3_4v26.xsd",
                      "ibmData/valid/S3_3_4/s3_3_4v26.xml"),
              1);
    EXPECT_EQ(verdict("ibmData/valid/S3_3_4/s3_3_4v28.xsd",
                      "ibmData/valid/S3_3_4/s3_3_4v28.xml"),
              1);
}

// ibm ii10-ii18: the union actual-member law — "true" riding the
// boolean arm of unionOfIDs is not an ID, so the idref true/
// abc_/rre values do not resolve.
TEST(Xsd11Bookkeeping, IbmUnionIdrefRowsInvalid) {
    EXPECT_EQ(verdict("ibmData/instance_invalid/S3_3_4/s3_3_4ii10.xsd",
                      "ibmData/instance_invalid/S3_3_4/s3_3_4ii10.xml"),
              0);
    EXPECT_EQ(verdict("ibmData/instance_invalid/S3_3_4/s3_3_4ii11.xsd",
                      "ibmData/instance_invalid/S3_3_4/s3_3_4ii11.xml"),
              0);
    EXPECT_EQ(verdict("ibmData/instance_invalid/S3_3_4/s3_3_4ii12.xsd",
                      "ibmData/instance_invalid/S3_3_4/s3_3_4ii12.xml"),
              0);
    EXPECT_EQ(verdict("ibmData/instance_invalid/S3_3_4/s3_3_4ii13.xsd",
                      "ibmData/instance_invalid/S3_3_4/s3_3_4ii13.xml"),
              0);
    EXPECT_EQ(verdict("ibmData/instance_invalid/S3_3_4/s3_3_4ii14.xsd",
                      "ibmData/instance_invalid/S3_3_4/s3_3_4ii14.xml"),
              0);
    EXPECT_EQ(verdict("ibmData/instance_invalid/S3_3_4/s3_3_4ii15.xsd",
                      "ibmData/instance_invalid/S3_3_4/s3_3_4ii15.xml"),
              0);
    EXPECT_EQ(verdict("ibmData/instance_invalid/S3_3_4/s3_3_4ii16.xsd",
                      "ibmData/instance_invalid/S3_3_4/s3_3_4ii16.xml"),
              0);
    EXPECT_EQ(verdict("ibmData/instance_invalid/S3_3_4/s3_3_4ii17.xsd",
                      "ibmData/instance_invalid/S3_3_4/s3_3_4ii17.xml"),
              0);
    EXPECT_EQ(verdict("ibmData/instance_invalid/S3_3_4/s3_3_4ii18.xsd",
                      "ibmData/instance_invalid/S3_3_4/s3_3_4ii18.xml"),
              0);
}

// ibm v15: a list-of-ID carried as SIMPLE CONTENT (extension base) —
// the idref b2 resolves against the content's items; ibm ii13's b2_
// does not (the guard pair).
TEST(Xsd11Bookkeeping, SimpleContentListResolves) {
    EXPECT_EQ(verdict("ibmData/valid/S3_3_4/s3_3_4v15.xsd",
                      "ibmData/valid/S3_3_4/s3_3_4v15.xml"),
              1);
}

// The pre-existing pin (#2184 wave 2, over008.n01): the same @id on
// two elements is still a duplicate.
TEST(Xsd11Bookkeeping, CrossElementDuplicateStillInvalid) {
    EXPECT_EQ(verdict("saxonData/Override/over008.xsd",
                      "saxonData/Override/over008.n01.xml"),
              0);
}
