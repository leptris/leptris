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
