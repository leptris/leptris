// test/xsd/test_xsd11_assert_sem.cpp — #2183: the assertion-facet
// XQuery-semantics residue. The evaluator substrate (#2154) already
// compiles the test under a $value declaration and applies the
// §3.13.4.1/§3.16.4 data model; these rows pin the SEMANTICS on top:
//
//   1. Absent focus (§3.16.4): simple-type assertions evaluate with
//      NO context item — position()/last()/dot raise XPDY0002, the
//      assertion does not hold (assert-simple008/009/010).
//   2. fn:doc in a test resolves the URI against the SCHEMA's
//      compile-captured base URI and caches the load (assert011).
//   3. A dynamic error inside a test (xs:integer division by zero,
//      assert012) fails the assertion — the instance is invalid,
//      never a crash; the XPath 1.0 lanes keep 1 div 0 = INF.
//   4. Relative descent in a complexType assert is rooted at the
//      validated element's SUBTREE (d4_3_15ii14 — the nested
//      rootType recursion drives the assert at depth), while
//      ABSOLUTE paths ('/', '//') see NOTHING: the §3.13.4.1 cl.
//      1.3 data model contains only the validated element's
//      subtree, so '/' and '//' are unsuccessful references, not
//      errors (the d4_3_15ii14/ii31 cluster pins both directions
//      — count(//ele1) sees neither the document's 2 nor the
//      subtree's 1).
//   5. List-of-items carrier decomposition: every $x in data($value)
//      walks the itemized binding (listunion6).
//   6. castable as resolves unprefixed builtin names through
//      xpathDefaultNamespace (assert-simple003/004).
//
// Corpus rows: test/xsd/xsd11-dispositions/README.md, the
// assertion-facet residue class.
#include <gtest/gtest.h>

#include <string>

#include "leptris.h"

namespace {

const char* kDir = LEPTRIS_XSD11_ASSERT_SEM_DIR;

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

/* Absent focus: position() is undefined during simple-type
 * assertion validation — the assertion never holds. */
TEST(Xsd11AssertSem, AbsentFocusPosition) {
    EXPECT_NE(verdict_of("absent_pos.xsd", "absent_pos_n.xml"), 1);
}

/* Absent focus: last() likewise. */
TEST(Xsd11AssertSem, AbsentFocusLast) {
    EXPECT_NE(verdict_of("absent_last.xsd", "absent_last_n.xml"), 1);
}

/* Absent focus: the context item (dot) is undefined. */
TEST(Xsd11AssertSem, AbsentFocusContextItem) {
    EXPECT_NE(verdict_of("absent_dot.xsd", "absent_dot_n.xml"), 1);
}

/* Positive control: a $value-only assertion keeps holding — absent
 * focus removes the context, not the value. */
TEST(Xsd11AssertSem, ValueOnlyAssertionStillHolds) {
    EXPECT_EQ(verdict_of("absent_ok.xsd", "absent_ok_v.xml"), 1);
    EXPECT_NE(verdict_of("absent_ok.xsd", "absent_ok_n.xml"), 1);
}

/* fn:doc in a test: the relative URI resolves against the schema's
 * base URI; the loaded document's nodes take part in the test. */
TEST(Xsd11AssertSem, DocResolvesAgainstSchemaBaseUri) {
    EXPECT_EQ(verdict_of("doc_assert.xsd", "doc_assert_v.xml"), 1);
    EXPECT_NE(verdict_of("doc_assert.xsd", "doc_assert_n.xml"), 1);
}

/* xs:integer division by zero inside a test is a dynamic error —
 * the assertion fails, the instance is invalid. */
TEST(Xsd11AssertSem, IntegerDivByZeroFailsAssertion) {
    EXPECT_NE(verdict_of("divzero.xsd", "divzero_n.xml"), 1);
    EXPECT_EQ(verdict_of("divzero.xsd", "divzero_v.xml"), 1);
}

/* List carrier decomposition: every $x in data($value) walks the
 * itemized list binding, so one odd item fails the assertion. */
TEST(Xsd11AssertSem, ListCarrierDecomposition) {
    EXPECT_EQ(verdict_of("listunion6.xsd", "listunion6_v.xml"), 1);
    EXPECT_NE(verdict_of("listunion6.xsd", "listunion6_n.xml"), 1);
}

/* castable as resolves an unprefixed builtin name through
 * xpathDefaultNamespace: '10f4' is not a double lexical. */
TEST(Xsd11AssertSem, CastableUnprefixedBuiltinViaXdn) {
    EXPECT_EQ(verdict_of("castable_xdn.xsd", "castable_xdn_v.xml"), 1);
    EXPECT_NE(verdict_of("castable_xdn.xsd", "castable_xdn_n.xml"), 1);
}

/* Relative descent in a complexType assert is rooted at the
 * validated element's subtree (d4_3_15ii14): the nested rootType
 * recursion drives the assert at depth, and the outer ele1 (the
 * subtree_n shape) is invisible to .//ele1. */
TEST(Xsd11AssertSem, RecursiveDescentRootedAtSubtree) {
    EXPECT_EQ(verdict_of("subtree.xsd", "subtree_v.xml"), 1);
    EXPECT_NE(verdict_of("subtree.xsd", "subtree_n.xml"), 1);
}

/* Same rooting for .//@attr (d4_3_15ii15). */
TEST(Xsd11AssertSem, RecursiveDescentAttributeSubtree) {
    EXPECT_EQ(verdict_of("subtree_attr.xsd", "subtree_attr_v.xml"), 1);
    EXPECT_NE(verdict_of("subtree_attr.xsd", "subtree_attr_n.xml"), 1);
}

/* ABSOLUTE paths in a complexType assert see NOTHING
 * (d4_3_15ii14/ii31 cluster): count(//ele1) is 0 — the assertion
 * holds when nothing exists at that path. A document-rooting or
 * subtree-rooting engine counts 2 or 1 and wrongly validates. */
TEST(Xsd11AssertSem, AbsolutePathsSeeNothing) {
    EXPECT_EQ(verdict_of("absfloor.xsd", "absfloor_v.xml"), 1);
    EXPECT_NE(verdict_of("absfail.xsd", "absfail_n.xml"), 1);
}

/* Leak-shape regression (LSan run 45836902979): a FACET test that
 * is an absolute location path enters evaluate_location_path under
 * absent focus — the seed nodeset must be released on the XPDY0002
 * error path, and the rooted-lane short-circuit must adopt it
 * (absfloor/absfail above walk that return). Every value is
 * invalid: there is no context item to anchor the path (§3.16.4,
 * the d4_3_15ii22/ii24 shape). */
TEST(Xsd11AssertSem, FacetLocationPathErrorReleasesNodeset) {
    EXPECT_NE(verdict_of("absfacet.xsd", "absfacet_n.xml"), 1);
    EXPECT_NE(verdict_of("absfacet.xsd", "absfacet_n.xml"), 1);
}
