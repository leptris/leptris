// test/xsd/test_xsd11_assertions.cpp — #2154: xs:assertion facets on
// simple types (XSD 1.1 §3.16). The facet list is captured per
// restriction, ACCUMULATES across the restriction ladder (§3.16.6 —
// base facets hold on derived types, and every facet in one
// restriction must hold), and evaluates at validate time with the
// value bound to $value (§3.16.4): typed where the datatype is known
// (numerics as bare literals), the itemized sequence for list values,
// the lexical string otherwise. List ITEM types and union MEMBER
// types contribute their own assertion chains per item/member. The
// complexType xs:assert capture keys on the assert SYNTAX ITSELF
// (schemas carry asserts without @version="1.1" — the #2149 posture).
#include <gtest/gtest.h>

#include <string>

#include "leptris.h"

namespace {

const char* kDir = LEPTRIS_XSD11_ASSERTIONS_DIR;

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

}  // namespace

/* The headline: an assertion facet on an element's simpleType. */
TEST(Xsd11Assertions, FacetOnElementSimpleType) {
    EXPECT_EQ(verdict_of("facet_element.xsd", "facet_element_v.xml"), 1);
    EXPECT_NE(verdict_of("facet_element.xsd", "facet_element_n.xml"), 1);
}

/* Several facets in one restriction: ALL must hold. */
TEST(Xsd11Assertions, FacetsInOneRestrictionAccumulate) {
    EXPECT_EQ(verdict_of("facet_multi.xsd", "facet_multi_v.xml"), 1);
    EXPECT_NE(verdict_of("facet_multi.xsd", "facet_multi_n1.xml"), 1);
    EXPECT_NE(verdict_of("facet_multi.xsd", "facet_multi_n2.xml"), 1);
}

/* The ladder: the base type's facet holds on the derived type. */
TEST(Xsd11Assertions, FacetsAccumulateAcrossRestrictionLadder) {
    EXPECT_EQ(verdict_of("facet_chain.xsd", "facet_chain_v.xml"), 1);
    EXPECT_NE(verdict_of("facet_chain.xsd", "facet_chain_n1.xml"), 1);
    EXPECT_NE(verdict_of("facet_chain.xsd", "facet_chain_n2.xml"), 1);
}

/* A list's ITEM type contributes its own facet chain per item. */
TEST(Xsd11Assertions, ListItemTypeFacetPerItem) {
    EXPECT_EQ(verdict_of("facet_list_item.xsd", "facet_list_item_v.xml"),
              1);
    EXPECT_NE(verdict_of("facet_list_item.xsd", "facet_list_item_n.xml"),
              1);
}

/* An assertion on a restriction OF a list binds $value as the
 * itemized typed sequence. */
TEST(Xsd11Assertions, ListValueBindingIsItemized) {
    EXPECT_EQ(verdict_of("facet_list_value.xsd",
                         "facet_list_value_v.xml"),
              1);
    EXPECT_NE(verdict_of("facet_list_value.xsd",
                         "facet_list_value_n.xml"),
              1);
}

/* The facet holds on attribute values too. */
TEST(Xsd11Assertions, FacetOnAttributeType) {
    EXPECT_EQ(verdict_of("facet_attribute.xsd", "facet_attribute_v.xml"),
              1);
    EXPECT_NE(verdict_of("facet_attribute.xsd", "facet_attribute_n.xml"),
              1);
}

/* #2206 (moxml#585): the consumer's repro — an assertion naming a
 * builtin datatype UNPREFIXED with no xpathDefaultNamespace in
 * force. `double` resolves to the XML Schema namespace: 42.5 is
 * castable, not-a-number is not (both validated clean before the
 * fix — the unresolvable name rode the permissive castable path). */
TEST(Xsd11Assertions, UnprefixedBuiltinWithoutXdn) {
    EXPECT_EQ(verdict_of("facet_unprefixed_builtin.xsd",
                         "facet_unprefixed_builtin_v.xml"),
              1);
    EXPECT_NE(verdict_of("facet_unprefixed_builtin.xsd",
                         "facet_unprefixed_builtin_n.xml"),
              1);
}

/* #2206: the bare builtin spelling on the restriction LADDER — the
 * derived type's own assertion and the inherited base assertion
 * both evaluate (§3.16.6). Each negative isolates one lane: n1 is
 * 12 chars (the derived length holds) but is not a double (the
 * BASE assertion rejects it); n2 is a double (the base holds) but
 * breaks the derived length. */
TEST(Xsd11Assertions, LadderUnprefixedBuiltinsEvaluate) {
    EXPECT_EQ(verdict_of("facet_ladder_unprefixed.xsd",
                         "facet_ladder_unprefixed_v.xml"),
              1);
    EXPECT_NE(verdict_of("facet_ladder_unprefixed.xsd",
                         "facet_ladder_unprefixed_n1.xml"),
              1);
    EXPECT_NE(verdict_of("facet_ladder_unprefixed.xsd",
                         "facet_ladder_unprefixed_n2.xml"),
              1);
}

/* complexType xs:assert captured on assert syntax alone (no
 * @version="1.1", no alternatives). */
TEST(Xsd11Assertions, AssertCapturedOnSyntaxPresence) {
    EXPECT_EQ(verdict_of("assert_ct.xsd", "assert_ct_v.xml"), 1);
    EXPECT_NE(verdict_of("assert_ct.xsd", "assert_ct_n.xml"), 1);
}

/* xs:assert on a simpleContent restriction: $value binds the typed
 * content value. */
TEST(Xsd11Assertions, AssertOnSimpleContentBindsValue) {
    EXPECT_EQ(verdict_of("assert_simplecontent.xsd",
                         "assert_simplecontent_v.xml"),
              1);
    EXPECT_NE(verdict_of("assert_simplecontent.xsd",
                         "assert_simplecontent_n.xml"),
              1);
}

/* ---- the chameleon include lane (#2184, assert020/021) ------------ */

/* A chameleon include (§4.2.2: the included unit has no
 * targetNamespace of its own) absorbs its components into the
 * INCLUDER's tns — and the absorbtion governs the PARTICLE refs
 * captured from the included unit exactly as it governs the
 * declarations: an unprefixed `ref="temp"` inside the included
 * unit's complexType resolves to the includer's
 * {targetNamespace}temp, and the assert test's unprefixed names
 * ride the included unit's xpathDefaultNamespace=
 * "##targetNamespace" (the includer's tns). The saxonData Assert
 * assert020/assert021 rows pin the shape: three nested temps
 * satisfy empty(temp/temp/temp) and the doc validates; a fourth
 * temp fails the assert and rejects. */
TEST(Xsd11Assertions, ChameleonIncludeParticleRefsAdoptTns) {
    EXPECT_EQ(verdict_of("chameleon_main.xsd", "chameleon_v.xml"), 1);
    EXPECT_NE(verdict_of("chameleon_main.xsd", "chameleon_n.xml"), 1);
}

/* ---- the assert-test function sweep (#2184, d4_3_15si04) ----------- */
/* §3.13.2: an assert's test is a VALID XPath expression — an
 * unknown function call is the static XPST0017 error and fails the
 * SCHEMA (d4_3_15si04), not the §3.13.4 "evaluation error = false"
 * channel (that channel is for DYNAMIC errors: the div-by-zero and
 * doc() miss laws of #2183). The check resolves exactly like the
 * evaluator: the standard registry (fns + xs: constructors), the
 * prebound prefixes, and the schema document's own bindings. */
TEST(Xsd11Assertions, UnknownFunctionInAssertFailsSchema) {
    const char* bad =
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
        " <xs:element name=\"root\" type=\"rootType\"/>"
        " <xs:complexType name=\"rootType\">"
        "  <xs:complexContent>"
        "   <xs:extension base=\"xs:string\">"
        "    <xs:attribute name=\"attr\" type=\"xs:string\"/>"
        "    <xs:assert test=\"invalid-XPath-expression() le @date\"/>"
        "   </xs:extension>"
        "  </xs:complexContent>"
        " </xs:complexType>"
        "</xs:schema>";
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s = leptris_xsd_compile(bad, strlen(bad), &st);
    EXPECT_TRUE(s == (LeptrisXsdSchema)0 || st != LEPTRIS_OK)
        << "an assert naming an unknown function fails the schema";
    if (s) leptris_xsd_free(s);
}

/* Known functions and xs: constructors — spelled through any prefix
 * the schema binds to the XML namespace — keep compiling. */
TEST(Xsd11Assertions, KnownAssertFunctionsStayLegal) {
    const char* ok =
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\""
        " xmlns:zd=\"http://www.w3.org/2001/XMLSchema\">"
        " <xs:element name=\"root\">"
        "  <xs:complexType>"
        "   <xs:sequence>"
        "    <xs:element name=\"temp\" maxOccurs=\"unbounded\"/>"
        "   </xs:sequence>"
        "   <xs:assert test=\"empty(temp) and count(temp) le 4\"/>"
        "  </xs:complexType>"
        " </xs:element>"
        " <xs:element name=\"d\" type=\"dateT\"/>"
        " <xs:complexType name=\"dateT\">"
        "  <xs:simpleContent>"
        "   <xs:extension base=\"xs:date\">"
        "    <xs:assert test=\"zd:date(.) gt xs:date('2000-01-01')\"/>"
        "   </xs:extension>"
        "  </xs:simpleContent>"
        " </xs:complexType>"
        "</xs:schema>";
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s = leptris_xsd_compile(ok, strlen(ok), &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0)
        << "known functions compile" << leptris_xsd_error(s);
    EXPECT_EQ(st, LEPTRIS_OK)
        << leptris_xsd_error(s);
    if (s) leptris_xsd_free(s);
}

/* The pinned corpus row (d4_3_15si04) flips to schema-error. */
TEST(Xsd11Assertions, CorpusAssertCompileRowFlips) {
    std::string p = std::string(LEPTRIS_XSD11_CASES_DIR) +
                    "/ibmData/schema_invalid/D4_3_15/"
                    "d4_3_15si04.xsd";
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s = leptris_xsd_compile_file(p.c_str(), &st);
    EXPECT_TRUE(s == (LeptrisXsdSchema)0 || st != LEPTRIS_OK)
        << "d4_3_15si04 must be rejected";
    if (s) leptris_xsd_free(s);
}

/* ---- fn:in-scope-prefixes (#2184, the ns4 rows) ---------------------
 * ns4's assert (in-scope-prefixes(.) = 'a') is the pinned corpus
 * row: an unprefixed fn: call through the assert's default function
 * namespace. The schema compiles (the function IS in the standard
 * registry now) and the assert EVALUATES — the instance that
 * declares xmlns:a is valid, the twin that doesn't is not (an
 * unevaluated assert would admit both). */
static int ns4_verdict(const char* instance) {
    std::string sp = std::string(LEPTRIS_XSD11_CASES_DIR) +
                     "/ibmData/mixed/assertions/namespace/ns4.xsd";
    std::string ip = std::string(LEPTRIS_XSD11_CASES_DIR) +
                     "/ibmData/mixed/assertions/namespace/" +
                     instance;
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s = leptris_xsd_compile_file(sp.c_str(), &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0) << "ns4.xsd must compile";
    EXPECT_EQ(st, LEPTRIS_OK) << "ns4.xsd compile status";
    if (!s) return -1;
    LeptrisStatus pst = (LeptrisStatus)0;
    LeptrisDocument d = leptris_parse_file(ip.c_str(), &pst);
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

TEST(Xsd11Assertions, InScopePrefixesAssertCorpusRow) {
    /* the pinned rows: ns4.xsd (schema) and ns4.xml (instance) */
    EXPECT_EQ(ns4_verdict("ns4.xml"), 1);
}

TEST(Xsd11Assertions, InScopePrefixesAssertEvaluates) {
    /* the assert is not just compiled — it evaluates: without the
     * xmlns:a declaration the probe finds no 'a' and fails */
    const std::string twin =
        "<x xmlns=\"http://www.example.org\"><y/></x>";
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisDocument d =
        leptris_parse_string(twin.data(), twin.size(), &st);
    ASSERT_NE(d, (LeptrisDocument)0);
    std::string sp = std::string(LEPTRIS_XSD11_CASES_DIR) +
                     "/ibmData/mixed/assertions/namespace/ns4.xsd";
    LeptrisXsdSchema s = leptris_xsd_compile_file(sp.c_str(), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    EXPECT_EQ(st, LEPTRIS_OK);
    EXPECT_NE(leptris_xsd_validate(s, d), 1)
        << "the in-scope-prefixes assert must fail the twin without "
           "xmlns:a";
    leptris_document_free(d);
    leptris_xsd_free(s);
}
