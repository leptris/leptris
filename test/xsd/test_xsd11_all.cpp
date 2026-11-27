// test/xsd/test_xsd11_all.cpp — #2184: the relaxed xs:all lane
// (XSD 1.1 §3.8.3). 1.0 pins an all particle to at-most-one per
// child and bars wildcards and group references from the model;
// 1.1 generalizes it: every child carries its own minOccurs/maxOccurs
// (0..N), the match is ORDER-FREE (counting, not sequencing),
// xs:any children participate, group references resolving to all
// groups flatten in, xs:all extends xs:all by merging the particle
// sets, and an xs:all restricts an xs:all particle-for-particle
// (bounds narrowing + wildcard subset) like any other particle pair.
//
// Disposition row 19 (saxon All + ibm S3_3_6/S3_4_6): the engine
// rejected the legal 1.1 forms (compile-side UPA over the all's
// overlapping element/wildcard FIRST sets, instance-side
// sequence-order matching) and accepted the violation rows its
// relaxed limits must reject.
//
// Fixtures: local shapes under xsd11-all/ plus the pinned
// w3c/xsdtests corpus rows for the family (saxonData/All,
// ibmData/{valid,instance_invalid,schema_invalid}/{S3_3_6,S3_4_6}).
#include <gtest/gtest.h>

#include <string>

#include "leptris.h"

namespace {

const char* kCorpus = LEPTRIS_XSD11_CASES_DIR;
const char* kDir = LEPTRIS_XSD11_ALL_DIR;

std::string corpus_path(const char* name) {
    return std::string(kCorpus) + "/" + name;
}

std::string path(const char* name) {
    return std::string(kDir) + "/" + name;
}

/* Compile `schema`; 1 = clean schema, 0 = rejected. */
int schema_ok(const std::string& spath) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile_file(spath.c_str(), &st);
    if (!s) return 0;
    int ok = (st == LEPTRIS_OK);
    leptris_xsd_free(s);
    return ok ? 1 : 0;
}

/* 1 = valid, 0 = invalid/rejected, -1 = harness compile failure. */
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
    LeptrisDocument d = leptris_parse_file(ipath.c_str(), &st);
    EXPECT_NE(d, (LeptrisDocument)0) << ipath << " must parse";
    if (!d) {
        leptris_xsd_free(s);
        return -1;
    }
    int v = leptris_xsd_validate(s, d);
    leptris_document_free(d);
    leptris_xsd_free(s);
    return v == 1 ? 1 : 0;
}

/* ---- local fixtures: order-free relaxed counting ------------------- */

TEST(Xsd11All, RelaxedCountsOrderFree) {
    /* min/max > 1 per child; the valid instances interleave the
     * particles freely (all's semantics are order-free with
     * counts) */
    EXPECT_EQ(verdict_of(path("relaxed-counts.xsd"),
                         path("rc_v1.xml")),
              1)
        << "in-bounds counts in scrambled order must validate";
    EXPECT_EQ(verdict_of(path("relaxed-counts.xsd"),
                         path("rc_v2.xml")),
              1)
        << "another order, same counts";
    EXPECT_EQ(verdict_of(path("relaxed-counts.xsd"),
                         path("rc_n1.xml")),
              0)
        << "below a child's minOccurs must fail";
    EXPECT_EQ(verdict_of(path("relaxed-counts.xsd"),
                         path("rc_n2.xml")),
              0)
        << "above a child's maxOccurs must fail";
}

TEST(Xsd11All, RelaxedCountsSubstitutionGroup) {
    /* substitution-group members count toward the head's particle */
    EXPECT_EQ(verdict_of(path("relaxed-subst.xsd"),
                         path("rs_v1.xml")),
              1)
        << "members count toward the head's occurrence window";
    EXPECT_EQ(verdict_of(path("relaxed-subst.xsd"),
                         path("rs_n1.xml")),
              0)
        << "head+members below min must fail";
    EXPECT_EQ(verdict_of(path("relaxed-subst.xsd"),
                         path("rs_n2.xml")),
              0)
        << "head+members above max must fail";
}

TEST(Xsd11All, WildcardChildInAll) {
    /* 1.1 allows xs:any inside xs:all (s3_3_6v01 shape): the schema
     * must compile (no UPA rejection over the element/wildcard
     * FIRST-set overlap — §3.8.6 weak wildcards defer to element
     * particles) and the wildcard child counts occurrences */
    EXPECT_EQ(schema_ok(path("all-wildcard.xsd")), 1)
        << "element + lax wildcard in xs:all must compile";
    EXPECT_EQ(verdict_of(path("all-wildcard.xsd"),
                         path("aw_v1.xml")),
              1)
        << "wildcard occurrences within max";
    EXPECT_EQ(verdict_of(path("all-wildcard.xsd"),
                         path("aw_n1.xml")),
              0)
        << "wildcard occurrences above max must fail";
}

TEST(Xsd11All, GroupRefToAllFlattens) {
    /* a group reference inside xs:all whose group is an xs:all
     * group flattens its particles into the parent (all007 vs
     * all001's instances) */
    EXPECT_EQ(verdict_of(path("all-groupref.xsd"),
                         path("gr_v1.xml")),
              1)
        << "flattened group particles count toward the all";
    EXPECT_EQ(verdict_of(path("all-groupref.xsd"),
                         path("gr_n1.xml")),
              0)
        << "below the flattened group's min must fail";
}

TEST(Xsd11All, ExtensionMergesAlls) {
    /* xs:all extends xs:all by merging the particle sets — the
     * instance may interleave base and derived particles freely */
    EXPECT_EQ(verdict_of(path("all-ext.xsd"), path("ae_v1.xml")),
              1)
        << "merged all accepts interleaved base+derived particles";
    EXPECT_EQ(verdict_of(path("all-ext.xsd"), path("ae_n1.xml")),
              0)
        << "a derived particle below its min must fail";
    /* optional whole-all extension (all314): either all may be
     * absent entirely, but a present all must satisfy its
     * particles */
    EXPECT_EQ(verdict_of(path("all-ext-opt.xsd"),
                         path("ao_v1.xml")),
              1)
        << "both particles present";
    EXPECT_EQ(verdict_of(path("all-ext-opt.xsd"),
                         path("ao_n1.xml")),
              0)
        << "one particle alone cannot satisfy the merged all";
}

TEST(Xsd11All, RestrictionOfAllCompiles) {
    /* an equal-shape restriction of an all-group base compiles
     * (s3_3_6v04) and validates against its own model */
    EXPECT_EQ(schema_ok(path("all-restrict.xsd")), 1)
        << "all-to-all restriction within bounds must compile";
    EXPECT_EQ(verdict_of(path("all-restrict.xsd"),
                         path("ar_v1.xml")),
              1);
}

TEST(Xsd11All, RestrictionOfAllRejectsWidening) {
    /* broader wildcard, wider occurrence window, or a dropped
     * required particle must fail the SCHEMA (s3_3_6si02 /
     * all202/all203/all204 shapes) */
    EXPECT_EQ(schema_ok(path("all-restrict-bad-wild.xsd")), 0)
        << "restriction may not widen the wildcard's namespace set";
    EXPECT_EQ(schema_ok(path("all-restrict-bad-max.xsd")), 0)
        << "restriction may not raise maxOccurs";
    EXPECT_EQ(schema_ok(path("all-restrict-bad-min.xsd")), 0)
        << "restriction may not lower minOccurs";
    EXPECT_EQ(schema_ok(path("all-restrict-bad-drop.xsd")), 0)
        << "restriction may not drop a required particle";
}

TEST(Xsd11All, CorpusSaxonAllRelaxedRows) {
    /* the pinned suite rows for the family */
    EXPECT_EQ(verdict_of(corpus_path("saxonData/All/all002.xsd"),
                         corpus_path("saxonData/All/all002.v01.xml")),
              1);
    EXPECT_EQ(verdict_of(corpus_path("saxonData/All/all005.xsd"),
                         corpus_path("saxonData/All/all005.v01.xml")),
              1);
    EXPECT_EQ(verdict_of(corpus_path("saxonData/All/all006.xsd"),
                         corpus_path("saxonData/All/all006.v01.xml")),
              1);
    EXPECT_EQ(verdict_of(corpus_path("saxonData/All/all007.xsd"),
                         corpus_path("saxonData/All/all001.v01.xml")),
              1);
    EXPECT_EQ(verdict_of(corpus_path("saxonData/All/all301.xsd"),
                         corpus_path("saxonData/All/all301.v01.xml")),
              1);
    EXPECT_EQ(verdict_of(corpus_path("saxonData/All/all304.xsd"),
                         corpus_path("saxonData/All/all304.v01.xml")),
              1);
    EXPECT_EQ(verdict_of(corpus_path("saxonData/All/all306.xsd"),
                         corpus_path("saxonData/All/all306.v01.xml")),
              1);
    EXPECT_EQ(verdict_of(corpus_path("saxonData/All/all314.xsd"),
                         corpus_path("saxonData/All/all314.v01.xml")),
              1);
    EXPECT_EQ(verdict_of(corpus_path("saxonData/All/all314.xsd"),
                         corpus_path("saxonData/All/all314.v02.xml")),
              1);
    EXPECT_EQ(verdict_of(corpus_path("saxonData/All/all314.xsd"),
                         corpus_path("saxonData/All/all314.n03.xml")),
              0);
    EXPECT_EQ(verdict_of(corpus_path("saxonData/All/all314.xsd"),
                         corpus_path("saxonData/All/all314.n04.xml")),
              0);
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all306.xsd")), 1);
}

TEST(Xsd11All, CorpusIbmRestrictionRows) {
    /* S3_3_6/S3_4_6: wildcards in all + restriction of all compile
     * clean and validate */
    EXPECT_EQ(schema_ok(
                  corpus_path("ibmData/valid/S3_3_6/s3_3_6v01.xsd")),
              1);
    EXPECT_EQ(verdict_of(
                  corpus_path("ibmData/valid/S3_3_6/s3_3_6v01.xsd"),
                  corpus_path("ibmData/valid/S3_3_6/s3_3_6v01.xml")),
              1);
    EXPECT_EQ(schema_ok(
                  corpus_path("ibmData/valid/S3_3_6/s3_3_6v04.xsd")),
              1);
    EXPECT_EQ(verdict_of(
                  corpus_path("ibmData/valid/S3_3_6/s3_3_6v04.xsd"),
                  corpus_path("ibmData/valid/S3_3_6/s3_3_6v04.xml")),
              1);
    EXPECT_EQ(schema_ok(
                  corpus_path("ibmData/valid/S3_4_6/s3_4_6v01.xsd")),
              1);
    EXPECT_EQ(verdict_of(
                  corpus_path("ibmData/valid/S3_4_6/s3_4_6v01.xsd"),
                  corpus_path("ibmData/valid/S3_4_6/s3_4_6v01.xml")),
              1);
    EXPECT_EQ(schema_ok(
                  corpus_path("ibmData/valid/S3_4_6/s3_4_6v04.xsd")),
              1);
    EXPECT_EQ(verdict_of(
                  corpus_path("ibmData/valid/S3_4_6/s3_4_6v04.xsd"),
                  corpus_path("ibmData/valid/S3_4_6/s3_4_6v04.xml")),
              1);
    /* the invalid-restriction schemas must be rejected: the broader
     * wildcard and the widened occurrence windows (si02's d/e/f).
     * si06's all-to-SEQUENCE subsumption rides the all
     * compile-constraint class, not this slice. */
    EXPECT_EQ(schema_ok(
                  corpus_path(
                      "ibmData/schema_invalid/S3_3_6/s3_3_6si02.xsd")),
              0);
    EXPECT_EQ(schema_ok(
                  corpus_path(
                      "ibmData/schema_invalid/S3_4_6/s3_4_6si02.xsd")),
              0);
}

TEST(Xsd11All, NilSpellingOne) {
    /* xsi:nil="1" is the XSD boolean spelling of true (all004): a
     * nil'ed element carries no content; content on a nil'ed
     * element is invalid */
    EXPECT_EQ(verdict_of(path("nil-one.xsd"), path("nil_v1.xml")),
              1)
        << "xsi:nil='1' with empty content must validate";
    EXPECT_EQ(verdict_of(path("nil-one.xsd"), path("nil_n1.xml")),
              0)
        << "xsi:nil='1' with element content must fail";
}

}  // namespace
