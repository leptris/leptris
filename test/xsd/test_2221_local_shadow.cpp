// leptris#2221 — same-named global/local element declarations: the
// content-model walk must type each child by the particle IT matched
// (position-exact), not by the first same-named particle anywhere in
// the model (moxml#594 consumer).
//
// XSD 1.0 §3.9.3/§3.3.2: element particle matching resolves against
// the particle's own declaration in scope — a local declaration
// shadows the global for its parent's content model. Two shapes
// collapsed to ONE binding under the first-name-match typing DFS:
//
//   1. two same-named LOCAL particles with different types (UPA-legal
//      when f separates them): each slot must face its OWN particle's
//      type;
//   2. a wildcard-admitted same-named child (every same-named
//      particle window exhausted): the child rides the wildcard —
//      processContents decides (strict demands a declaration; lax
//      assesses declared names only).
//
// libxml2 is the reference oracle; every verdict below mirrors it.
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "leptris.h"

namespace {

// Returns 1 (valid), 0 (invalid), -1 (compile/parse failure).
int validate_pair(const std::string& xsd, const char* xml) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s =
        leptris_xsd_compile(xsd.c_str(), xsd.size(), &st);
    if (!s) return -1;
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    if (!d) {
        leptris_xsd_free(s);
        return -1;
    }
    int ok = leptris_xsd_validate(s, d);
    if (getenv("I2221_VERBOSE"))
        for (size_t i = 0; i < leptris_xsd_error_count(s); i++)
            printf("    err: %s\n", leptris_xsd_error_at(s, i));
    leptris_document_free(d);
    leptris_xsd_free(s);
    return ok;
}

// The issue's schema, minimalized: local e (xs:integer), f
// (xs:integer), trailing ##local wildcard whose processContents and
// minOccurs parameterize the legs. with_global_e adds the GLOBAL
// xs:positiveInteger e declaration — the shadowing counterpart.
std::string shadow_schema(const char* pc, int with_global_e,
                          const char* min_occurs) {
    std::string xsd =
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
        "<xs:complexType name=\"zing\"><xs:sequence>"
        "<xs:element name=\"e\" type=\"xs:integer\"/>"
        "<xs:element name=\"f\" type=\"xs:integer\"/>"
        "<xs:any namespace=\"##local\" processContents=\"";
    xsd += pc;
    xsd += "\"";
    if (min_occurs && *min_occurs) {
        xsd += " minOccurs=\"";
        xsd += min_occurs;
        xsd += "\"";
    }
    xsd += "/></xs:sequence></xs:complexType>"
           "<xs:element name=\"doc\" type=\"zing\"/>";
    if (with_global_e)
        xsd += "<xs:element name=\"e\" "
               "type=\"xs:positiveInteger\"/>";
    xsd += "</xs:schema>";
    return xsd;
}

// Two same-named LOCAL particles with different types, f separating
// them (UPA-legal: every slot is deterministic with one lookahead).
const char kTwoLocal[] =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
    "<xs:complexType name=\"zz\"><xs:sequence>"
    "<xs:element name=\"e\" type=\"xs:integer\"/>"
    "<xs:element name=\"f\" type=\"xs:integer\"/>"
    "<xs:element name=\"e\" type=\"xs:positiveInteger\"/>"
    "</xs:sequence></xs:complexType>"
    "<xs:element name=\"doc\" type=\"zz\"/>"
    "</xs:schema>";

} // namespace

// ---- the issue's two-child forms: local particle binds, global is
// out of the picture (wildcard optional so the two-child form is
// content-model-valid; the issue's literal schema keeps the wildcard
// required and the missing third child is its own content-model
// failure — libxml2 rejects both child counts there alike).

TEST(Shadow2221, NegativeTwelveBindsLocalInteger) {
    EXPECT_EQ(validate_pair(
                  shadow_schema("lax", 1, "0"),
                  "<doc><e>-12</e><f>42</f></doc>"),
              1);
}

TEST(Shadow2221, PositiveTwelveSatisfiesBothStillValid) {
    EXPECT_EQ(validate_pair(
                  shadow_schema("lax", 1, "0"),
                  "<doc><e>12</e><f>42</f></doc>"),
              1);
}

// ---- wildcard-absorbed same-named children: the child rides the
// wildcard slot once the local e window [1..1] is exhausted, so
// processContents governs — lax/strict resolve the GLOBAL
// declaration (positiveInteger), not the local particle.

TEST(Shadow2221, LaxWildcardSecondETypesByGlobalDecl) {
    std::string xsd = shadow_schema("lax", 1, NULL);
    EXPECT_EQ(validate_pair(
                  xsd, "<doc><e>-12</e><f>42</f><e>-5</e></doc>"),
              0)
        << "-5 rode the LOCAL integer particle; the wildcard slot "
           "must assess the global positiveInteger";
    EXPECT_EQ(validate_pair(
                  xsd, "<doc><e>-12</e><f>42</f><e>7</e></doc>"),
              1);
}

TEST(Shadow2221, StrictWildcardSecondETypesByGlobalDecl) {
    std::string xsd = shadow_schema("strict", 1, NULL);
    EXPECT_EQ(validate_pair(
                  xsd, "<doc><e>-12</e><f>42</f><e>-5</e></doc>"),
              0);
    EXPECT_EQ(validate_pair(
                  xsd, "<doc><e>-12</e><f>42</f><e>7</e></doc>"),
              1);
}

// ---- processContents gates the wildcard-admitted name itself:
// with NO global e declaration the same-named child is undeclared —
// strict demands a declaration (rejected), lax admits it.

TEST(Shadow2221, StrictWildcardUndeclaredNameRejected) {
    EXPECT_EQ(validate_pair(
                  shadow_schema("strict", 0, NULL),
                  "<doc><e>-12</e><f>42</f><e>-12</e></doc>"),
              0)
        << "strict wildcard admitted an undeclared name";
}

TEST(Shadow2221, LaxWildcardUndeclaredNameAdmitted) {
    EXPECT_EQ(validate_pair(
                  shadow_schema("lax", 0, NULL),
                  "<doc><e>-12</e><f>42</f><e>-12</e></doc>"),
              1)
        << "lax wildcard rejected an undeclared name";
}

// ---- two same-named LOCAL particles: each slot faces its own
// particle's type (the second e is positiveInteger, not integer).

TEST(Shadow2221, TwoLocalParticlesEachSlotOwnType) {
    EXPECT_EQ(validate_pair(
                  kTwoLocal,
                  "<doc><e>-12</e><f>42</f><e>-5</e></doc>"),
              0)
        << "second slot accepted an integer-only value; it must "
           "face its own positiveInteger particle";
    EXPECT_EQ(validate_pair(
                  kTwoLocal,
                  "<doc><e>-12</e><f>42</f><e>7</e></doc>"),
              1);
    EXPECT_EQ(validate_pair(
                  kTwoLocal,
                  "<doc><e>abc</e><f>42</f><e>7</e></doc>"),
              0)
        << "first slot stopped enforcing the local integer";
}

// ---- the global binding keeps working where no local particle
// exists: the root e instance assesses through the global
// declaration alone.

TEST(Shadow2221, GlobalOnlyRootStillTypesByGlobalDecl) {
    std::string xsd = shadow_schema("lax", 1, NULL);
    EXPECT_EQ(validate_pair(xsd, "<e>7</e>"), 1);
    EXPECT_EQ(validate_pair(xsd, "<e>-12</e>"), 0);
}
