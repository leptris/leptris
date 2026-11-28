// test/xsd/test_xsd11_wild_all.cpp — #2184 wildcard/all residue: the
// four dispositioned clusters this branch owns.
//
//   C. compile-time wildcard EDC (§3.4.4.3): a content model with a
//      local element particle AND a strict/lax wildcard admitting the
//      like-named global declaration type-checks the two type tables
//      at COMPILE time (wild078/079/081.bad — the static half; the
//      dynamic half shipped in the #2184-1 wave).
//   R. wildcard restriction over-rejection: the UPA gate must not
//      drop @notNamespace exclusions when pairing wildcard FIRST
//      sets — wild049/050's disjoint ##local vs notNamespace-##local
//      wildcards compile clean (all005's disjoint sets and all006's
//      element-over-wildcard relaxation stay the pinned valids).
//   L. cos-all-limited compile residue: group references inside an
//      all resolve to ALL groups with minOccurs=maxOccurs=1
//      (all008-011); an all extending an all keeps the base's
//      occurrence (all313 vs all314).
//   O. the all UPA (cos-nonambig over one all): substitution-group
//      and wildcard overlaps between all-particles (all241-243) and
//      across an all-extension merge (all302/303).
//   A. assessment-side: ##targetNamespace in a chameleon-include
//      wildcard resolves per the OWNING unit's effective tns
//      (wild009.n1/wild010.n1); xsi:nil must be a boolean lexical
//      even under a skip wildcard (wild042.n1); lax/strict children
//      consumed at a wildcard position assess against the
//      wildcard-resolved global declaration (wild063.n1).
//
// Fixtures ride the pinned w3c/xsdtests corpus (xsd11-cases).
#include <gtest/gtest.h>

#include <string>

#include "leptris.h"

namespace {

const char* kCorpus = LEPTRIS_XSD11_CASES_DIR;

std::string corpus_path(const char* name) {
    return std::string(kCorpus) + "/" + name;
}

/* 1 = clean compile, 0 = rejected. */
int schema_ok(const std::string& spath) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s = leptris_xsd_compile_file(spath.c_str(), &st);
    if (!s) return 0;
    int ok = (st == LEPTRIS_OK);
    leptris_xsd_free(s);
    return ok ? 1 : 0;
}

/* 1 = valid, 0 = invalid/rejected. */
int verdict_of(const std::string& spath, const std::string& ipath) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s = leptris_xsd_compile_file(spath.c_str(), &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0) << spath << " must compile";
    EXPECT_EQ(st, LEPTRIS_OK) << spath << " compile status";
    if (!s || st != LEPTRIS_OK) {
        if (s) leptris_xsd_free(s);
        return 0;
    }
    LeptrisDocument d = leptris_parse_file(ipath.c_str(), &st);
    EXPECT_NE(d, (LeptrisDocument)0) << ipath << " must parse";
    if (!d) {
        leptris_xsd_free(s);
        return 0;
    }
    int v = leptris_xsd_validate(s, d);
    leptris_document_free(d);
    leptris_xsd_free(s);
    return v == 1 ? 1 : 0;
}

/* ---- R: the UPA gate keeps @notNamespace sets ---------------------- */

TEST(Xsd11WildAll, NotNamespaceWildcardsStayDisjoint) {
    /* wild049: the base all pairs ##local against
     * notNamespace="##local" (disjoint); the restriction carries one
     * ##any/notQName wildcard inside both sets. The suite compiles
     * this schema. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/Wild/wild049.xsd")), 1)
        << "wild049 must compile";
    /* wild050: the derived sequence pairs the same disjoint pair as
     * adjacent repeating particles. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/Wild/wild050.xsd")), 1)
        << "wild050 must compile";
}

TEST(Xsd11WildAll, DisjointAndOverlappingAllWildcardsHold) {
    /* all005 (valid): two disjoint-namespace skip wildcards in one
     * all — must stay clean. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all005.xsd")), 1)
        << "all005 must stay valid";
    /* all243 (schema-error): {one,two} vs {two,three} overlap. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all243.n.xsd")), 0)
        << "all243 must be rejected";
}

/* ---- C: compile-time wildcard EDC ---------------------------------- */

TEST(Xsd11WildAll, WildcardResolvedTypeTableCompileCheck) {
    /* wild078 (strict) and wild079 (lax): the ##local wildcard
     * resolves the like-named global 'a' whose type table
     * (xs:integer alternative) differs from the local particle's
     * (anyType) — cos-element-consistent fails the schema. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/Wild/wild078.bad.xsd")), 0)
        << "wild078 must be rejected";
    EXPECT_EQ(schema_ok(corpus_path("saxonData/Wild/wild079.bad.xsd")), 0)
        << "wild079 must be rejected";
    /* wild081: the local particle's table [(0=1)->integer, default
     * date] differs from the global's [default date]. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/Wild/wild081.bad.xsd")), 0)
        << "wild081 must be rejected";
}

TEST(Xsd11WildAll, SkipWildcardOwesNoCompileCheck) {
    /* the dynamic-law carrier (wild063) is a skip-free lax model that
     * must keep compiling: local integer + lax wildcard with global
     * positiveInteger — the wildcard-resolved decl may DIFFER at
     * compile time here because... no: lax participates too (skip is
     * the exempt posture), so this shape IS wild063's schema —
     * which the suite marks valid. The compile law only fires when a
     * wildcard-resolved GLOBAL declaration like-named to the particle
     * exists AND its table differs — wild063's global 'e'
     * (positiveInteger) vs local 'e' (integer): positiveInteger
     * derives from integer, and the §3.4.4.3 consistency law holds
     * for deriving types — the schema stays valid. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/Wild/wild063.xsd")), 1)
        << "wild063 must stay valid";
}

/* ---- L: cos-all-limited group refs + extension occurrence ---------- */

TEST(Xsd11WildAll, GroupRefInsideAllMustResolveToAllGroup) {
    /* all008: the referenced group is a SEQUENCE — schema-error. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all008.n.xsd")), 0)
        << "all008 must be rejected";
    /* all011: the referenced group is a CHOICE — schema-error. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all011.n.xsd")), 0)
        << "all011 must be rejected";
}

TEST(Xsd11WildAll, GroupRefInsideAllCarriesUnitOccurrence) {
    /* all009/all010: the ref's minOccurs/maxOccurs must both be 1. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all009.n.xsd")), 0)
        << "all009 must be rejected";
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all010.n.xsd")), 0)
        << "all010 must be rejected";
    /* the legal flatten (all206) stays clean. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all206.xsd")), 1)
        << "all206 must stay valid";
}

TEST(Xsd11WildAll, AllExtensionKeepsTheBaseOccurrence) {
    /* all313: base all minOccurs=1, extension all minOccurs=0 — the
     * extension's all occurrence must equal the base's. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all313.n.xsd")), 0)
        << "all313 must be rejected";
    /* all314: both alls carry minOccurs=0 — legal. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all314.xsd")), 1)
        << "all314 must stay valid";
}

/* ---- O: the all UPA ------------------------------------------------- */

TEST(Xsd11WildAll, SubstitutionOverlapInsideOneAll) {
    /* all241: p substitutes o; both are particles of one all. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all241.n.xsd")), 0)
        << "all241 must be rejected";
    /* all242: q substitutes both o and p; o and p are particles of
     * one all — the common member makes them overlap. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all242.n.xsd")), 0)
        << "all242 must be rejected";
}

TEST(Xsd11WildAll, AllExtensionMergeRejectsOverlappingParticles) {
    /* all302: the extension re-declares 'c' — the merged all carries
     * it twice. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all302.n.xsd")), 0)
        << "all302 must be rejected";
    /* all303: the extension adds 'g' whose member 'dg' also
     * substitutes the base's 'd'. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all303.n.xsd")), 0)
        << "all303 must be rejected";
}

TEST(Xsd11WildAll, LegalAllShapesStayValid) {
    /* the pinned valids the new overlap law must not disturb: the
     * disjoint-wildcard all (all005), the element-over-wildcard
     * relaxation (all006), the group-ref flatten (all206), the
     * subsumption valids (all201/206/217/231/232/237/238's family is
     * the gate's duty — spot the merged-extension one all301), and
     * the all314 extension. */
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all005.xsd")), 1);
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all006.xsd")), 1);
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all301.xsd")), 1);
    EXPECT_EQ(schema_ok(corpus_path("saxonData/All/all314.xsd")), 1);
}

/* ---- A: assessment-side -------------------------------------------- */

TEST(Xsd11WildAll, ChameleonTargetNamespaceAdmitsByOwningUnit) {
    /* wild009.n1 vs wild011.xsd: the anyAttribute
     * notNamespace="##targetNamespace ##local" rides a chameleon
     * include — ##targetNamespace resolves to the OVERALL (includer)
     * tns, so the eden.com attribute is excluded → invalid. */
    EXPECT_EQ(verdict_of(corpus_path("saxonData/Wild/wild011.xsd"),
                         corpus_path("saxonData/Wild/wild009.n1.xml")),
              0)
        << "wild009.n1 must be invalid";
    /* wild010.n1 vs wild012.xsd: same law on an element wildcard. */
    EXPECT_EQ(verdict_of(corpus_path("saxonData/Wild/wild012.xsd"),
                         corpus_path("saxonData/Wild/wild010.n1.xml")),
              0)
        << "wild010.n1 must be invalid";
}

TEST(Xsd11WildAll, XsiNilMustBeBooleanEvenUnderSkip) {
    /* wild042.n1: xsi:nil="1234" — the nil attribute's VALUE is
     * checked regardless of the skip wildcard that admits it. */
    EXPECT_EQ(verdict_of(corpus_path("saxonData/Wild/wild042.xsd"),
                         corpus_path("saxonData/Wild/wild042.n1.xml")),
              0)
        << "wild042.n1 must be invalid";
}

TEST(Xsd11WildAll, WildcardPositionChildAssessesResolvedDecl) {
    /* wild063.n1: the third child e rides the wildcard position —
     * the lax-resolved global 'e' (positiveInteger) assesses -12 as
     * invalid, regardless of the first-position local 'e'
     * (integer). */
    EXPECT_EQ(verdict_of(corpus_path("saxonData/Wild/wild063.xsd"),
                         corpus_path("saxonData/Wild/wild063.n1.xml")),
              0)
        << "wild063.n1 must be invalid";
}

/* wild041: an explicit attribute USE in the xsi namespace fails the
 * schema (§3.2.7 — xsi attributes are never declared). */
TEST(Xsd11WildAll, XsiNamespaceAttributeUseFailsSchema) {
    EXPECT_EQ(schema_ok(corpus_path("saxonData/Wild/wild041.bad.xsd")), 0)
        << "wild041 must be rejected";
}

}  // namespace
