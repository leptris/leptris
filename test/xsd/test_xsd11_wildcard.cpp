// test/xsd/test_xsd11_wildcard.cpp — #2184: the XSD 1.1 wildcard
// negative namespace/name sets (§3.10.2 XML representation,
// §3.10.6.3 subset/union/intersection laws). The saxon Wild family
// exercises four levers the engine left open:
//
//   1. xs:anyAttribute/@notNamespace — captured nowhere, enforced
//      nowhere (the element wildcard's cm_not_ns_hits arm has no
//      attribute-side twin).
//   2. @notQName prefixed tokens — stored RAW; the matchers skip
//      them ("prefixed: deferred"), so xml:space / p:foo exclusions
//      never bite. The open-content lane canonizes to {uri}local at
//      capture (s3_4_1ii04); every wildcard gets the same treatment.
//   3. Wildcard algebra — a type GATHERING wildcards from several
//      attributeGroup refs admits a name only when EVERY member
//      admits (§3.10.6.3 intersection, wild023/025/043); across a
//      DERIVATION the base and derived wildcards union, each arm
//      applying its own exclusions (wild045/046).
//   4. The compile sweep — namespace= + notNamespace= together fail
//      the schema (src-wildcard, wild007/008); a notQName token with
//      an undeclared prefix or a malformed spelling fails the schema;
//      a notQName exclusion whose namespace the wildcard could never
//      admit is vacuous and fails the schema (wild031-035); an
//      attribute-wildcard RESTRICTION whose admitted set is not a
//      subset of the base's fails the schema (§3.10.6.3 subset +
//      derivation-ok-restriction.4.1, wild020-022/057).
//
// The element wildcard carries ##definedSibling (§3.10.2): a token
// that excludes every name a sibling element particle declares.
//
// Fixtures: inline schemas mirroring the pinned saxonData/Wild rows,
// plus direct corpus pins (LEPTRIS_XSD11_CASES_DIR) for the rows the
// levers flip.
#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "leptris.h"

namespace {

const char* kCorpus = LEPTRIS_XSD11_CASES_DIR;

std::string corpus_path(const char* name) {
    return std::string(kCorpus) + "/" + name;
}

/* Compile schema text: 1 = clean compile, 0 = rejected. */
int schema_ok_text(const char* xsd) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile(xsd, strlen(xsd), &st);
    if (!s) return 0;
    int ok = (st == LEPTRIS_OK);
    leptris_xsd_free(s);
    return ok ? 1 : 0;
}

/* The suite's schema-error contract: the engine must reject the
 * document (a NULL handle or any non-OK status). */
int schema_rejected_text(const char* xsd) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile(xsd, strlen(xsd), &st);
    int rejected = (s == (LeptrisXsdSchema)0) ||
                   st != LEPTRIS_OK;
    if (s) leptris_xsd_free(s);
    return rejected ? 1 : 0;
}

int verdict_text(const char* xsd, const char* xml) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile(xsd, strlen(xsd), &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0) << "schema must compile";
    EXPECT_EQ(st, LEPTRIS_OK) << "schema compile status";
    if (!s || st != LEPTRIS_OK) {
        if (s) leptris_xsd_free(s);
        return -1;
    }
    LeptrisStatus pst = (LeptrisStatus)0;
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &pst);
    EXPECT_NE(d, (LeptrisDocument)0) << "instance must parse";
    if (!d) {
        leptris_xsd_free(s);
        return -1;
    }
    int v = leptris_xsd_validate(s, d);
    leptris_document_free(d);
    leptris_xsd_free(s);
    return v;
}

int corpus_verdict(const char* schema, const char* instance) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile_file(corpus_path(schema).c_str(), &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0) << schema << " must compile";
    EXPECT_EQ(st, LEPTRIS_OK) << schema << " compile status";
    if (!s || st != LEPTRIS_OK) {
        if (s) leptris_xsd_free(s);
        return -1;
    }
    LeptrisStatus pst = (LeptrisStatus)0;
    LeptrisDocument d = leptris_parse_file(
        corpus_path(instance).c_str(), &pst);
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

int corpus_schema_ok(const char* schema) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile_file(corpus_path(schema).c_str(), &st);
    int ok = (s != (LeptrisXsdSchema)0) && st == LEPTRIS_OK;
    if (s) leptris_xsd_free(s);
    return ok ? 1 : 0;
}

int corpus_schema_rejected(const char* schema) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile_file(corpus_path(schema).c_str(), &st);
    int rejected = (s == (LeptrisXsdSchema)0) || st != LEPTRIS_OK;
    if (s) leptris_xsd_free(s);
    return rejected ? 1 : 0;
}

/* ---- the wild001 shape: @notNamespace excludes foreign URIs ------ */

const char* kNotNsForeign =
    "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>"
    "<xs:element name='eden'><xs:complexType><xs:sequence/>"
    "<xs:anyAttribute notNamespace='http://apple.com/ http://devil.com/'"
    " processContents='skip'/></xs:complexType></xs:element>"
    "</xs:schema>";

TEST(Xsd11Wildcard, AttrNotNsExcludesForeignNamespace) {
    EXPECT_EQ(0, verdict_text(
                     kNotNsForeign,
                     "<eden adam='m' evil:eve='f'"
                     " xmlns:evil='http://devil.com/'/>"));
    EXPECT_EQ(1, verdict_text(
                     kNotNsForeign,
                     "<eden c:cain='m'"
                     " xmlns:c='http://genesis.com/'/>"));
}

TEST(Xsd11Wildcard, AttrNotNsLocalExcludesNoNamespace) {
    const char* xsd =
        "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>"
        "<xs:element name='eden'><xs:complexType><xs:sequence/>"
        "<xs:anyAttribute notNamespace=' ##local '"
        " processContents='skip'/></xs:complexType></xs:element>"
        "</xs:schema>";
    EXPECT_EQ(0, verdict_text(xsd, "<eden cain='abel'/>"));
    EXPECT_EQ(1, verdict_text(
                     xsd, "<eden c:cain='m'"
                          " xmlns:c='http://genesis.com/'/>"));
}

TEST(Xsd11Wildcard, AttrNotNsTargetNamespaceResolves) {
    const char* xsd =
        "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'"
        " targetNamespace='http://eden.com/'>"
        "<xs:element name='eden'><xs:complexType><xs:sequence/>"
        "<xs:anyAttribute notNamespace=' ##targetNamespace ##local '"
        " processContents='lax'/></xs:complexType></xs:element>"
        "</xs:schema>";
    EXPECT_EQ(0, verdict_text(
                     xsd, "<eden z:cain='abel'"
                          " xmlns='http://eden.com/'"
                          " xmlns:z='http://eden.com/'/>"));
    EXPECT_EQ(1, verdict_text(
                     xsd, "<eden e:eve='f' xmlns='http://eden.com/'"
                          " xmlns:e='http://other.com/'/>"));
}

/* ---- notQName: literals, ##defined, prefixed names --------------- */

const char* kNotQNameDefined =
    "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>"
    "<xs:complexType name='zing'><xs:sequence/>"
    "<xs:anyAttribute namespace='##any' notQName='##defined jang'"
    " processContents='skip'/></xs:complexType>"
    "<xs:element name='zing' type='zing'/>"
    "<xs:attribute name='zang' type='xs:date'/>"
    "</xs:schema>";

TEST(Xsd11Wildcard, AttrNotQNameLiteralAndDefinedExclude) {
    EXPECT_EQ(0, verdict_text(kNotQNameDefined,
                              "<zing jang='jing'/>"));
    EXPECT_EQ(0, verdict_text(kNotQNameDefined,
                              "<zing zang='2008-12-12'/>"));
    EXPECT_EQ(1, verdict_text(kNotQNameDefined,
                              "<zing ping='pong'/>"));
}

TEST(Xsd11Wildcard, AttrNotQNamePrefixedResolvesSchemaBindings) {
    const char* xsd =
        "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>"
        "<xs:element name='eden'><xs:complexType><xs:sequence/>"
        "<xs:anyAttribute notQName='xml:space xml:id'"
        " processContents='skip'/></xs:complexType></xs:element>"
        "</xs:schema>";
    EXPECT_EQ(0, verdict_text(xsd,
                              "<eden xml:space='preserve'/>"));
    EXPECT_EQ(1, verdict_text(xsd, "<eden keep='me'/>"));
}

/* ---- the group-ref fold carries the negative sets ---------------- */

TEST(Xsd11Wildcard, AttrGroupRefCarriesNotQNameAndNotNs) {
    const char* xsd =
        "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>"
        "<xs:element name='zing' type='zing'/>"
        "<xs:complexType name='zing'><xs:sequence/>"
        "<xs:attributeGroup ref='g'/></xs:complexType>"
        "<xs:attributeGroup name='g'>"
        "<xs:anyAttribute notNamespace='http://devil.com/'"
        " notQName='##defined jang' processContents='skip'/>"
        "</xs:attributeGroup>"
        "<xs:attribute name='zang' type='xs:date'/>"
        "</xs:schema>";
    EXPECT_EQ(0, verdict_text(xsd, "<zing jang='jing'/>"));
    EXPECT_EQ(0, verdict_text(xsd, "<zing zang='2008'/>"));
    EXPECT_EQ(1, verdict_text(
                     xsd, "<zing e:eve='f'"
                          " xmlns:e='http://eve.com/'/>"));
    EXPECT_EQ(0, verdict_text(
                     xsd, "<zing evil:eve='f'"
                          " xmlns:evil='http://devil.com/'/>"));
}

/* ---- gathered wildcards intersect (§3.10.6.3) -------------------- */

TEST(Xsd11Wildcard, GatheredAttributeWildcardsIntersect) {
    const char* xsd =
        "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>"
        "<xs:element name='eden' type='T'/>"
        "<xs:complexType name='T'><xs:sequence/>"
        "<xs:attributeGroup ref='a'/>"
        "<xs:attributeGroup ref='b'/></xs:complexType>"
        "<xs:attributeGroup name='a'>"
        "<xs:anyAttribute notNamespace='##local'"
        " processContents='lax'/></xs:attributeGroup>"
        "<xs:attributeGroup name='b'>"
        "<xs:anyAttribute notNamespace='http://eve.com/'"
        " processContents='lax'/></xs:attributeGroup>"
        "</xs:schema>";
    /* eve-ns: excluded by b -> the intersection rejects */
    EXPECT_EQ(0, verdict_text(
                     xsd, "<eden m:adam='m' f:eve='f'"
                          " xmlns:m='http://adam.com/'"
                          " xmlns:f='http://eve.com/'/>"));
    /* no-namespace: excluded by a */
    EXPECT_EQ(0, verdict_text(xsd, "<eden moses='old'/>"));
    /* adam-ns: admitted by both */
    EXPECT_EQ(1, verdict_text(
                     xsd, "<eden m:adam='m'"
                          " xmlns:m='http://adam.com/'/>"));
}

TEST(Xsd11Wildcard, GatheredIntersectionWithNotQName) {
    const char* xsd =
        "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>"
        "<xs:element name='computer' type='computer'/>"
        "<xs:complexType name='computer'><xs:sequence/>"
        "<xs:attributeGroup ref='a'/>"
        "<xs:attributeGroup ref='b'/></xs:complexType>"
        "<xs:attributeGroup name='a'>"
        "<xs:anyAttribute namespace='##local' notQName='a b c'"
        " processContents='skip'/></xs:attributeGroup>"
        "<xs:attributeGroup name='b'>"
        "<xs:anyAttribute namespace='##local' notQName='c d e'"
        " processContents='skip'/></xs:attributeGroup>"
        "</xs:schema>";
    EXPECT_EQ(0, verdict_text(xsd, "<computer a='a'/>"));
    EXPECT_EQ(0, verdict_text(xsd, "<computer c='c'/>"));
    EXPECT_EQ(1, verdict_text(xsd, "<computer z='z'/>"));
}

/* ---- extension: union with per-arm exclusions --------------------- */

TEST(Xsd11Wildcard, ExtensionWildcardUnionAppliesEachArmExclusions) {
    const char* xsd =
        "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>"
        "<xs:element name='computer' type='extendedComputer'/>"
        "<xs:complexType name='computer'><xs:sequence/>"
        "<xs:attributeGroup ref='a'/></xs:complexType>"
        "<xs:complexType name='extendedComputer'>"
        "<xs:complexContent><xs:extension base='computer'>"
        "<xs:attributeGroup ref='b'/></xs:extension>"
        "</xs:complexContent></xs:complexType>"
        "<xs:attributeGroup name='a'>"
        "<xs:anyAttribute namespace='##local' notQName='a b c'"
        " processContents='skip'/></xs:attributeGroup>"
        "<xs:attributeGroup name='b'>"
        "<xs:anyAttribute namespace='##local' notQName='c d e'"
        " processContents='skip'/></xs:attributeGroup>"
        "</xs:schema>";
    /* c: excluded by BOTH arms -> the union rejects */
    EXPECT_EQ(0, verdict_text(xsd, "<computer c='c'/>"));
    /* a/d: admitted by one arm each */
    EXPECT_EQ(1, verdict_text(xsd, "<computer a='a'/>"));
    EXPECT_EQ(1, verdict_text(xsd, "<computer d='d'/>"));
}

TEST(Xsd11Wildcard, ExtensionUnionCarriesBaseNotNamespace) {
    const char* xsd =
        "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>"
        "<xs:element name='comp' type='ext'/>"
        "<xs:complexType name='base'><xs:sequence/>"
        "<xs:anyAttribute namespace='##local' notQName='a b'"
        " processContents='skip'/></xs:complexType>"
        "<xs:complexType name='ext'><xs:complexContent>"
        "<xs:extension base='base'><xs:sequence/>"
        "<xs:anyAttribute notNamespace='http://devil.com/'"
        " processContents='skip'/></xs:extension>"
        "</xs:complexContent></xs:complexType>"
        "</xs:schema>";
    /* devil-ns: the derived arm excludes; no-ns a: the base arm
     * excludes -> both rejected by the union */
    EXPECT_EQ(0, verdict_text(
                     xsd, "<comp evil:eve='f'"
                          " xmlns:evil='http://devil.com/'/>"));
    EXPECT_EQ(0, verdict_text(xsd, "<comp a='1'/>"));
    EXPECT_EQ(1, verdict_text(xsd, "<comp ok='1'/>"));
}

/* ---- compile sweep: co-occurrence --------------------------------- */

TEST(Xsd11Wildcard, SchemaErrorAttrNamespacePlusNotNamespace) {
    EXPECT_EQ(1, schema_rejected_text(
                     "<xs:schema xmlns:xs='http://www.w3.org/2001/"
                     "XMLSchema'>"
                     "<xs:element name='eden'><xs:complexType>"
                     "<xs:sequence/>"
                     "<xs:anyAttribute namespace='##other'"
                     " notNamespace=' ##targetNamespace '"
                     " processContents='skip'/>"
                     "</xs:complexType></xs:element>"
                     "</xs:schema>"));
    EXPECT_EQ(1, schema_rejected_text(
                     "<xs:schema xmlns:xs='http://www.w3.org/2001/"
                     "XMLSchema'>"
                     "<xs:element name='eden'><xs:complexType>"
                     "<xs:sequence><xs:any namespace='##any'"
                     " notNamespace=' ##targetNamespace '"
                     " processContents='skip' minOccurs='0'/>"
                     "</xs:sequence></xs:complexType></xs:element>"
                     "</xs:schema>"));
}

/* ---- compile sweep: notQName token forms -------------------------- */

TEST(Xsd11Wildcard, SchemaErrorNotQNameUndeclaredPrefix) {
    EXPECT_EQ(1, schema_rejected_text(
                     "<xs:schema xmlns:xs='http://www.w3.org/2001/"
                     "XMLSchema'>"
                     "<xs:element name='computer' type='computer'/>"
                     "<xs:complexType name='computer'><xs:sequence/>"
                     "<xs:anyAttribute"
                     " notNamespace='http://www.w3.org/2001/"
                     "XMLSchema-instance'"
                     " notQName='xsl:stylesheet'"
                     " processContents='lax'/>"
                     "</xs:complexType></xs:schema>"));
    EXPECT_EQ(1, schema_rejected_text(
                     "<xs:schema xmlns:xs='http://www.w3.org/2001/"
                     "XMLSchema'>"
                     "<xs:element name='computer' type='computer'/>"
                     "<xs:complexType name='computer'>"
                     "<xs:sequence><xs:any"
                     " notNamespace='http://www.w3.org/2001/"
                     "XMLSchema-instance'"
                     " notQName='xsl:stylesheet'/>"
                     "</xs:sequence></xs:complexType>"
                     "</xs:schema>"));
}

TEST(Xsd11Wildcard, SchemaErrorNotQNameMalformedSpellings) {
    EXPECT_EQ(1, schema_rejected_text(
                     "<xs:schema xmlns:xs='http://www.w3.org/2001/"
                     "XMLSchema'>"
                     "<xs:element name='computer' type='computer'/>"
                     "<xs:complexType name='computer'><xs:sequence/>"
                     "<xs:anyAttribute notQName='xml:xml:lang'"
                     " processContents='lax'/>"
                     "</xs:complexType></xs:schema>"));
    EXPECT_EQ(1, schema_rejected_text(
                     "<xs:schema xmlns:xs='http://www.w3.org/2001/"
                     "XMLSchema'>"
                     "<xs:element name='computer' type='computer'/>"
                     "<xs:complexType name='computer'>"
                     "<xs:sequence><xs:any notQName=':stylesheet'"
                     " processContents='lax'/>"
                     "</xs:sequence></xs:complexType>"
                     "</xs:schema>"));
}

/* ---- compile sweep: vacuous exclusions ---------------------------- */

TEST(Xsd11Wildcard, SchemaErrorNotQNameVacuousAgainstPositiveSet) {
    /* ##other admits no absent-namespace name; "memory" is one */
    EXPECT_EQ(1, schema_rejected_text(
                     "<xs:schema xmlns:xs='http://www.w3.org/2001/"
                     "XMLSchema'>"
                     "<xs:element name='computer' type='computer'/>"
                     "<xs:complexType name='computer'><xs:all>"
                     "<xs:any processContents='lax'"
                     " namespace='##other' notQName='memory'"
                     " minOccurs='0' maxOccurs='unbounded'/>"
                     "</xs:all></xs:complexType>"
                     "</xs:schema>"));
    /* ##local admits only absent-namespace names; xml:space is not */
    EXPECT_EQ(1, schema_rejected_text(
                     "<xs:schema xmlns:xs='http://www.w3.org/2001/"
                     "XMLSchema'>"
                     "<xs:element name='computer' type='computer'/>"
                     "<xs:complexType name='computer'><xs:sequence/>"
                     "<xs:anyAttribute processContents='lax'"
                     " namespace='##local' notQName='xml:space'/>"
                     "</xs:complexType></xs:schema>"));
}

TEST(Xsd11Wildcard, SchemaErrorNotQNameVacuousAgainstNotNamespace) {
    /* no tns: ##targetNamespace in notNamespace excludes the absent
     * namespace, so the absent-ns "CPU" is already out */
    EXPECT_EQ(1, schema_rejected_text(
                     "<xs:schema xmlns:xs='http://www.w3.org/2001/"
                     "XMLSchema'>"
                     "<xs:element name='computer' type='computer'/>"
                     "<xs:complexType name='computer'><xs:sequence/>"
                     "<xs:anyAttribute processContents='lax'"
                     " notNamespace='##targetNamespace'"
                     " notQName='CPU'/>"
                     "</xs:complexType></xs:schema>"));
    EXPECT_EQ(1, schema_rejected_text(
                     "<xs:schema xmlns:xs='http://www.w3.org/2001/"
                     "XMLSchema'>"
                     "<xs:element name='computer' type='computer'/>"
                     "<xs:complexType name='computer'><xs:sequence/>"
                     "<xs:anyAttribute processContents='lax'"
                     " notNamespace='http://www.w3.org/XML/1998/"
                     "namespace'"
                     " notQName='xml:space'/>"
                     "</xs:complexType></xs:schema>"));
    EXPECT_EQ(1, schema_rejected_text(
                     "<xs:schema xmlns:xs='http://www.w3.org/2001/"
                     "XMLSchema'"
                     " xmlns:xsi='http://www.w3.org/2001/"
                     "XMLSchema-instance'>"
                     "<xs:element name='computer' type='computer'/>"
                     "<xs:complexType name='computer'><xs:sequence/>"
                     "<xs:anyAttribute processContents='lax'"
                     " notNamespace='http://www.w3.org/2001/"
                     "XMLSchema-instance'"
                     " notQName='xsi:type'/>"
                     "</xs:complexType></xs:schema>"));
}

TEST(Xsd11Wildcard, LegalNotQNameFormsStayGreen) {
    /* resolvable prefixed token, admitted namespace: legal */
    EXPECT_EQ(1, schema_ok_text(
                     "<xs:schema xmlns:xs='http://www.w3.org/2001/"
                     "XMLSchema'"
                     " xmlns:x='http://extra.com/'>"
                     "<xs:element name='computer' type='computer'/>"
                     "<xs:complexType name='computer'><xs:sequence/>"
                     "<xs:anyAttribute processContents='skip'"
                     " namespace='##any' notQName='x:c x:d x:e'/>"
                     "</xs:complexType></xs:schema>"));
    /* no-namespace literals over ##any: legal */
    EXPECT_EQ(1, schema_ok_text(
                     "<xs:schema xmlns:xs='http://www.w3.org/2001/"
                     "XMLSchema'>"
                     "<xs:element name='computer' type='computer'/>"
                     "<xs:complexType name='computer'><xs:sequence/>"
                     "<xs:anyAttribute processContents='skip'"
                     " notQName='a b c'/>"
                     "</xs:complexType></xs:schema>"));
    /* notNamespace + non-overlapping notQName: legal */
    EXPECT_EQ(1, schema_ok_text(
                     "<xs:schema xmlns:xs='http://www.w3.org/2001/"
                     "XMLSchema'>"
                     "<xs:element name='computer' type='computer'/>"
                     "<xs:complexType name='computer'><xs:sequence/>"
                     "<xs:anyAttribute processContents='skip'"
                     " notNamespace='http://x.com/'"
                     " notQName='xml:lang'/>"
                     "</xs:complexType></xs:schema>"));
}

/* ---- attribute-wildcard restriction subset (§3.10.6.3) ------------ */

const char* kRestrictHead =
    "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>"
    "<xs:element name='eden' type='R'/>"
    "<xs:complexType name='B'><xs:sequence/>%s</xs:complexType>"
    "<xs:complexType name='R'><xs:complexContent>"
    "<xs:restriction base='B'><xs:sequence/>%s</xs:restriction>"
    "</xs:complexContent></xs:complexType></xs:schema>";

TEST(Xsd11Wildcard, SchemaErrorAttrWildcardRestrictionNotSubset) {
    char xsd[1024];
    /* wild020: the derived list admits adam-ns, the base excludes it */
    snprintf(xsd, sizeof(xsd), kRestrictHead,
             "<xs:anyAttribute notNamespace="
             "'http://cain.com/ http://abel.com/ http://adam.com/'"
             " processContents='lax'/>",
             "<xs:anyAttribute"
             " namespace='http://eve.com/ http://adam.com/'"
             " processContents='lax'/>");
    EXPECT_EQ(1, schema_rejected_text(xsd));
    /* wild021: the derived excludes less than the base */
    snprintf(xsd, sizeof(xsd), kRestrictHead,
             "<xs:anyAttribute notNamespace="
             "'http://cain.com/ http://abel.com/ http://adam.com/'"
             " processContents='lax'/>",
             "<xs:anyAttribute notNamespace='http://adam.com/'"
             " processContents='lax'/>");
    EXPECT_EQ(1, schema_rejected_text(xsd));
    /* wild022: the derived widens to ##any */
    snprintf(xsd, sizeof(xsd), kRestrictHead,
             "<xs:anyAttribute notNamespace="
             "'http://cain.com/ http://abel.com/ http://adam.com/'"
             " processContents='lax'/>",
             "<xs:anyAttribute namespace='##any'"
             " processContents='lax'/>");
    EXPECT_EQ(1, schema_rejected_text(xsd));
    /* wild057: ##defined base, the derived admits a declared name
     * the base excludes (the group's "ok") */
    snprintf(xsd, sizeof(xsd), kRestrictHead,
             "<xs:anyAttribute namespace='##any'"
             " notQName='##defined' processContents='skip'/>",
             "<xs:anyAttribute namespace='##local'"
             " notQName='zang zong' processContents='skip'/>");
    std::string with_ok(xsd);
    with_ok.insert(with_ok.find("</xs:schema>"),
                   "<xs:attribute name='zang' type='xs:date'/>"
                   "<xs:attribute name='zong' type='xs:time'/>"
                   "<xs:attributeGroup name='a'>"
                   "<xs:attribute name='ok' type='xs:boolean'/>"
                   "</xs:attributeGroup>");
    EXPECT_EQ(1, schema_rejected_text(with_ok.c_str()));
}

TEST(Xsd11Wildcard, LegalAttrWildcardRestrictionSubsetStaysGreen) {
    char xsd[1024];
    /* excluding MORE namespaces is a legal restriction */
    snprintf(xsd, sizeof(xsd), kRestrictHead,
             "<xs:anyAttribute notNamespace="
             "'http://cain.com/ http://abel.com/'"
             " processContents='lax'/>",
             "<xs:anyAttribute notNamespace="
             "'http://cain.com/ http://abel.com/ http://adam.com/'"
             " processContents='lax'/>");
    EXPECT_EQ(1, schema_ok_text(xsd));
    /* narrowing an enumeration list is legal */
    snprintf(xsd, sizeof(xsd), kRestrictHead,
             "<xs:anyAttribute"
             " namespace='http://eve.com/ http://adam.com/'"
             " processContents='lax'/>",
             "<xs:anyAttribute namespace='http://adam.com/'"
             " processContents='lax'/>");
    EXPECT_EQ(1, schema_ok_text(xsd));
}

/* ---- element wildcard --------------------------------------------- */

TEST(Xsd11Wildcard, ElemNotQNamePrefixedResolves) {
    const char* xsd =
        "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>"
        "<xs:element name='zing' type='zing'/>"
        "<xs:complexType name='zing'><xs:sequence>"
        "<xs:any notQName='xml:space xml:id'"
        " processContents='skip' maxOccurs='unbounded'/>"
        "</xs:sequence></xs:complexType>"
        "</xs:schema>";
    EXPECT_EQ(0, verdict_text(xsd, "<zing><xml:space/></zing>"));
    EXPECT_EQ(1, verdict_text(xsd, "<zing><e/></zing>"));
}

TEST(Xsd11Wildcard, ElemDefinedSiblingExcludesSiblingNames) {
    const char* xsd =
        "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>"
        "<xs:element name='root' type='zing'/>"
        "<xs:complexType name='zing'><xs:sequence>"
        "<xs:element name='a' type='xs:string' minOccurs='0'/>"
        "<xs:element name='b' type='xs:string' minOccurs='0'/>"
        "<xs:element name='c' type='xs:string' minOccurs='0'/>"
        "<xs:any notQName='##definedSibling'"
        " processContents='skip' maxOccurs='unbounded'/>"
        "</xs:sequence></xs:complexType>"
        "</xs:schema>";
    EXPECT_EQ(0,
              verdict_text(xsd, "<root><a/><b/><c/><a/><e/></root>"));
    EXPECT_EQ(1, verdict_text(xsd, "<root><a/><b/><c/><e/></root>"));
}

TEST(Xsd11Wildcard, ElemNotNsStillEnforced) {
    const char* xsd =
        "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'"
        " targetNamespace='http://eden.com/'>"
        "<xs:element name='eden'><xs:complexType><xs:sequence>"
        "<xs:any notNamespace=' ##targetNamespace ##local '"
        " processContents='lax' minOccurs='0'/>"
        "</xs:sequence></xs:complexType></xs:element>"
        "</xs:schema>";
    EXPECT_EQ(0, verdict_text(
                     xsd, "<eden xmlns='http://eden.com/'>"
                          "<eve/></eden>"));
    EXPECT_EQ(1, verdict_text(
                     xsd, "<eden xmlns='http://eden.com/'>"
                          "<e:f xmlns:e='http://other.com/'/></eden>"));
}

/* ---- corpus pins: the rows the levers flip ------------------------ */

TEST(Xsd11Wildcard, CorpusInvalidRowsFlip) {
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild001.xsd",
                                "saxonData/Wild/wild001.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild001.xsd",
                                "saxonData/Wild/wild001.n2.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild003.xsd",
                                "saxonData/Wild/wild003.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild009.xsd",
                                "saxonData/Wild/wild009.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild009.xsd",
                                "saxonData/Wild/wild009.n2.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild014.xsd",
                                "saxonData/Wild/wild014.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild015.xsd",
                                "saxonData/Wild/wild015.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild017.xsd",
                                "saxonData/Wild/wild017.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild023.xsd",
                                "saxonData/Wild/wild023.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild023.xsd",
                                "saxonData/Wild/wild023.n2.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild025.xsd",
                                "saxonData/Wild/wild025.n3.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild026.xsd",
                                "saxonData/Wild/wild025.n3.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild027.xsd",
                                "saxonData/Wild/wild027.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild028.xsd",
                                "saxonData/Wild/wild028.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild043.xsd",
                                "saxonData/Wild/wild043.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild044.xsd",
                                "saxonData/Wild/wild044.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild045.xsd",
                                "saxonData/Wild/wild045.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild058.xsd",
                                "saxonData/Wild/wild058.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild058.xsd",
                                "saxonData/Wild/wild058.n2.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild059.xsd",
                                "saxonData/Wild/wild058.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild070.xsd",
                                "saxonData/Wild/wild070.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild071.xsd",
                                "saxonData/Wild/wild071.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild074.xsd",
                                "saxonData/Wild/wild074.n1.xml"));
    EXPECT_EQ(0, corpus_verdict("saxonData/Wild/wild084.xsd",
                                "saxonData/Wild/wild084.n1.xml"));
}

TEST(Xsd11Wildcard, CorpusSchemaErrorRowsFlip) {
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild007.bad.xsd"));
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild008.bad.xsd"));
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild020.bad.xsd"));
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild021.bad.xsd"));
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild022.bad.xsd"));
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild031.bad.xsd"));
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild032.bad.xsd"));
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild033.bad.xsd"));
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild034.bad.xsd"));
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild035.bad.xsd"));
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild036.bad.xsd"));
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild037.bad.xsd"));
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild038.bad.xsd"));
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild039.bad.xsd"));
    EXPECT_EQ(1, corpus_schema_rejected(
                     "saxonData/Wild/wild057.bad.xsd"));
}

/* legal corpus schemas in the same family must stay green */
TEST(Xsd11Wildcard, CorpusLegalSchemasStayGreen) {
    EXPECT_EQ(1, corpus_schema_ok("saxonData/Wild/wild001.xsd"));
    EXPECT_EQ(1, corpus_schema_ok("saxonData/Wild/wild003.xsd"));
    EXPECT_EQ(1, corpus_schema_ok("saxonData/Wild/wild009.xsd"));
    EXPECT_EQ(1, corpus_schema_ok("saxonData/Wild/wild014.xsd"));
    EXPECT_EQ(1, corpus_schema_ok("saxonData/Wild/wild023.xsd"));
    EXPECT_EQ(1, corpus_schema_ok("saxonData/Wild/wild026.xsd"));
    EXPECT_EQ(1, corpus_schema_ok("saxonData/Wild/wild027.xsd"));
    EXPECT_EQ(1, corpus_schema_ok("saxonData/Wild/wild043.xsd"));
    EXPECT_EQ(1, corpus_schema_ok("saxonData/Wild/wild045.xsd"));
    EXPECT_EQ(1, corpus_schema_ok("saxonData/Wild/wild058.xsd"));
    EXPECT_EQ(1, corpus_schema_ok("saxonData/Wild/wild059.xsd"));
    EXPECT_EQ(1, corpus_schema_ok("saxonData/Wild/wild070.xsd"));
    EXPECT_EQ(1, corpus_schema_ok("saxonData/Wild/wild074.xsd"));
}

} // namespace
