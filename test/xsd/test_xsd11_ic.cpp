// test/xsd/test_xsd11_ic.cpp — #2156: the XSD 1.1 identity-constraint
// extensions. Selector/field XPaths are parsed at COMPILE time into
// the §3.11.6.2/3.11.6.3 subset (child chains, './/', '.'-steps, a
// final '@'-step, selector unions) with prefixes resolved against the
// SCHEMA document's bindings + xpathDefaultNamespace (§3.13.2) —
// never the instance context. Out-of-subset expressions and
// unresolved prefixes fail the schema; @ref reuse resolves with the
// §3.11.3 category match; a vacuous key/unique contributes an
// (empty) node table so keyrefs against it fail (§3.11.4 cl. 4.3).
#include <gtest/gtest.h>

#include <string>

#include "leptris.h"

namespace {

const char* kDir = LEPTRIS_XSD11_IC_DIR;

std::string path(const char* name) {
    return std::string(kDir) + "/" + name;
}

int verdict_of(const char* schema, const char* instance) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s = leptris_xsd_compile_file(path(schema).c_str(), &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0) << schema << " must compile";
    EXPECT_EQ(st, LEPTRIS_OK) << schema << " compile status: "
                              << leptris_xsd_error(s);
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

/* The suite's schema-error verdict: the compile must fail (a
 * NULL handle or a non-OK status). */
void expect_schema_error(const char* schema) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s = leptris_xsd_compile_file(path(schema).c_str(), &st);
    EXPECT_TRUE(s == (LeptrisXsdSchema)0 || st != LEPTRIS_OK)
        << schema << " must be rejected";
    if (s) leptris_xsd_free(s);
}

}  // namespace

/* Schema-prefix selector/field resolution + key/keyref dependency on
 * one declaration. The instance binds the namespace as the DEFAULT —
 * the s: prefix only exists in the schema document. */
TEST(Xsd11Ic, SchemaPrefixSelectorResolves) {
    EXPECT_EQ(verdict_of("schema_ns.xsd", "schema_ns_v.xml"), 1);
    EXPECT_NE(verdict_of("schema_ns.xsd", "schema_ns_dup.xml"), 1);
    EXPECT_NE(verdict_of("schema_ns.xsd", "schema_ns_n.xml"), 1);
}

/* The leading './/' descendant form reaches nested matches. */
TEST(Xsd11Ic, DescendantSelector) {
    EXPECT_EQ(verdict_of("desc_sel.xsd", "desc_sel_v.xml"), 1);
    EXPECT_NE(verdict_of("desc_sel.xsd", "desc_sel_n.xml"), 1);
}

/* The '.' field takes the selected node's string value; the './'
 * spelled selector form parses. */
TEST(Xsd11Ic, DotFieldWithDotSlashSelector) {
    EXPECT_EQ(verdict_of("dot_field.xsd", "dot_field_v.xml"), 1);
    EXPECT_NE(verdict_of("dot_field.xsd", "dot_field_n.xml"), 1);
}

/* A two-step child-chain field. */
TEST(Xsd11Ic, MultiStepFieldChain) {
    EXPECT_EQ(verdict_of("multi_step.xsd", "multi_step_v.xml"), 1);
    EXPECT_NE(verdict_of("multi_step.xsd", "multi_step_n.xml"), 1);
}

/* The '*' NameTest matches any element child regardless of
 * namespace. */
TEST(Xsd11Ic, WildcardSelector) {
    EXPECT_EQ(verdict_of("wild_sel.xsd", "wild_sel_v.xml"), 1);
    EXPECT_NE(verdict_of("wild_sel.xsd", "wild_sel_n.xml"), 1);
}

/* The four xpathDefaultNamespace forms: literal URI, ##targetNamespace,
 * ##local overridden by a selector-level URI, ##local overridden by a
 * selector-level ##targetNamespace (the id050-id053 shapes). */
TEST(Xsd11Ic, XpathDefaultNamespaceForms) {
    EXPECT_EQ(verdict_of("xdns_uri.xsd", "xdns_v.xml"), 1);
    EXPECT_NE(verdict_of("xdns_uri.xsd", "xdns_n.xml"), 1);
    EXPECT_NE(verdict_of("xdns_tns.xsd", "xdns_n.xml"), 1);
    EXPECT_NE(verdict_of("xdns_override.xsd", "xdns_n.xml"), 1);
    EXPECT_NE(verdict_of("xdns_sel_tns.xsd", "xdns_n.xml"), 1);
}

/* Zero selected nodes satisfies key/unique (§3.11.4: the qualified
 * node set may be empty for unique). */
TEST(Xsd11Ic, VacuousUniqueSatisfies) {
    EXPECT_EQ(verdict_of("vacuous.xsd", "vacuous_v.xml"), 1);
}

/* A keyref tuple against a key whose node table is EMPTY must fail —
 * the (empty) table still exists. */
TEST(Xsd11Ic, KeyrefAgainstEmptyKeyTableFails) {
    EXPECT_NE(verdict_of("keyref_empty.xsd", "keyref_empty_n.xml"), 1);
}

/* @ref reuse: the key and keyref named on `list` re-used on `other`;
 * both scopes evaluate under the same constraint names. */
TEST(Xsd11Ic, RefFormReuse) {
    EXPECT_EQ(verdict_of("ref_form.xsd", "ref_form_v.xml"), 1);
    EXPECT_NE(verdict_of("ref_form.xsd", "ref_form_n.xml"), 1);
}

/* Selector path unions are in the 1.1 subset — both branches are
 * evaluated into one target node set. */
TEST(Xsd11Ic, UnionSelectorEvaluatesBothBranches) {
    EXPECT_EQ(verdict_of("union_sel.xsd", "union_sel_v.xml"), 1);
    EXPECT_NE(verdict_of("union_sel.xsd", "union_sel_n.xml"), 1);
}

/* Out-of-subset expressions, unresolved schema prefixes, and broken
 * @ref forms all fail the SCHEMA (§3.11.6, §3.11.3 cl. 5). */
TEST(Xsd11Ic, SubsetViolationsAreSchemaErrors) {
    expect_schema_error("ref_kind.bad.xsd");
    expect_schema_error("ref_missing.bad.xsd");
    expect_schema_error("bad_axis.xsd");
    expect_schema_error("bad_pred.xsd");
    expect_schema_error("bad_abs.xsd");
    expect_schema_error("bad_dslash.xsd");
    expect_schema_error("bad_attr_mid.xsd");
    expect_schema_error("bad_field_union.xsd");
    expect_schema_error("bad_prefix.xsd");
}
