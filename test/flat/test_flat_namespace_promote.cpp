// test/flat/test_flat_namespace_promote.cpp — Verify namespace-aware
// flat promote (TODO 145 Phase 1).
//
// Previously, input with xmlns:* routed through the legacy parser.
// After Phase 1, the flat fast path covers xmlns docs too — the
// promote pass moves xmlns declarations from the regular attribute
// list to elem->namespaces and splits qualified element names into
// prefix + local_name.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>
#include <string>

namespace {

TaurusDocument Parse(const char* xml) {
    TaurusStatus st = TAURUS_OK;
    return taurus_parse_string(xml, std::strlen(xml), &st);
}

// Find attribute value by walking the indexed list. The public API
// has no single-call name-based getter; we use the indexed accessors.
const char* AttrValueByName(TaurusElement e, const char* name) {
    size_t n = taurus_element_attribute_count(e);
    for (size_t i = 0; i < n; i++) {
        const char* an = taurus_element_attribute_name_at(e, i);
        if (an && std::strcmp(an, name) == 0) {
            return taurus_element_attribute_value_at(e, i);
        }
    }
    return nullptr;
}

TEST(FlatNamespacePromote, DefaultNamespaceDeclaration) {
    const char xml[] = "<root xmlns='http://default'><a/></root>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);

    /* xmlns moved to elem->namespaces; regular attr list is empty. */
    EXPECT_EQ(taurus_element_attribute_count(root), 0u);
    EXPECT_EQ(taurus_element_namespace_count(root), 1u);
    EXPECT_EQ(taurus_element_namespace_decl_prefix(root, 0), nullptr);
    EXPECT_STREQ(taurus_element_namespace_decl_uri(root, 0),
                 "http://default");

    taurus_document_free(doc);
}

TEST(FlatNamespacePromote, PrefixedNamespaceDeclaration) {
    const char xml[] =
        "<root xmlns:ds='http://ds' xmlns:foo='http://foo'><a/></root>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    EXPECT_EQ(taurus_element_namespace_count(root), 2u);
    EXPECT_STREQ(taurus_element_namespace_decl_prefix(root, 0), "ds");
    EXPECT_STREQ(taurus_element_namespace_decl_uri(root, 0), "http://ds");
    EXPECT_STREQ(taurus_element_namespace_decl_prefix(root, 1), "foo");
    EXPECT_STREQ(taurus_element_namespace_decl_uri(root, 1), "http://foo");

    taurus_document_free(doc);
}

TEST(FlatNamespacePromote, PrefixedElementName) {
    /* <ds:Signature/> — element name is split into prefix="ds" and
     * local name="Signature". */
    const char xml[] =
        "<SignedRoot xmlns:ds='http://www.w3.org/2000/09/xmldsig#'>"
        "<ds:Signature/></SignedRoot>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef child = taurus_node_first_child(taurus_element_as_node(root));
    ASSERT_NE(child, nullptr);
    TaurusElement sig = taurus_node_as_element(child);
    ASSERT_NE(sig, nullptr);

    EXPECT_STREQ(taurus_element_name(sig), "Signature");

    taurus_document_free(doc);
}

TEST(FlatNamespacePromote, NamespacedAttributeRoundTrip) {
    /* Mix of xmlns declaration and regular attribute. */
    const char xml[] =
        "< SignedRoot_test xmlns:xml='http://www.w3.org/XML/1998/namespace' a='1'/>";  // invalid: space after <
    /* Re-test without the bug: */
    const char xml2[] =
        "<r xmlns:xml='http://www.w3.org/XML/1998/namespace' a='1'/>";
    TaurusDocument doc = Parse(xml2);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    /* Regular attribute preserved; xmlns:xml moved to namespaces. */
    EXPECT_EQ(taurus_element_attribute_count(root), 1u);
    EXPECT_STREQ(AttrValueByName(root, "a"), "1");
    EXPECT_EQ(taurus_element_namespace_count(root), 1u);

    taurus_document_free(doc);
}

TEST(FlatNamespacePromote, ExclusiveC14NWithFlatPath) {
    /* End-to-end: parse namespaced doc via flat path, then run
     * exclusive C14N. The exclusive output must emit only visibly-
     * used namespaces. */
    const char xml[] =
        "<SignedRoot xmlns:ds='http://www.w3.org/2000/09/xmldsig#' "
        "xmlns:unused='http://unused'>"
        "<ds:Signature/></SignedRoot>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef child = taurus_node_first_child(taurus_element_as_node(root));
    TaurusElement sig = taurus_node_as_element(child);

    char* excl = taurus_c14n_canonicalize_subtree_ex(
        sig, TAURUS_C14N_1_0, TAURUS_C14N_MODE_EXCLUSIVE, nullptr, 1);
    ASSERT_NE(excl, nullptr);
    std::string s(excl);
    /* ds visibly used by element name → must emit. */
    EXPECT_NE(s.find("xmlns:ds="), std::string::npos);
    /* unused not visibly used → must NOT emit (the whole point of
     * exclusive C14N). */
    EXPECT_EQ(s.find("xmlns:unused="), std::string::npos);
    taurus_free_string(excl);

    taurus_document_free(doc);
}

TEST(FlatNamespacePromote, NestedNamespacesInherit) {
    /* Child element inherits parent's namespace declarations. */
    const char xml[] =
        "<root xmlns:foo='http://foo'><foo:child/></root>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef child_node = taurus_node_first_child(taurus_element_as_node(root));
    TaurusElement child = taurus_node_as_element(child_node);
    ASSERT_NE(child, nullptr);

    /* Child has no xmlns declarations of its own. */
    EXPECT_EQ(taurus_element_namespace_count(child), 0u);
    /* But namespace_for_prefix walks up and finds foo on root. */
    EXPECT_STREQ(taurus_element_namespace_for_prefix(child, "foo"),
                 "http://foo");

    taurus_document_free(doc);
}

}  // namespace
