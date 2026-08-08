// test/abi/test_v0_5_14_bugs.cpp — Regression specs for issues #222, #223
// and the minor visibility gaps surfaced in the v0.5.13 user audit.
//
// #213, #216, #217 were verified fixed in v0.5.13; the new specs in
// test_dom_mutation_bugs.cpp already cover them. This file covers the
// NEWLY-fixed bugs from the v0.5.14 cycle.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>
#include <string>

namespace {

TaurusDocument Parse(const char* xml) {
    TaurusStatus st = TAURUS_OK;
    return taurus_parse_string(xml, std::strlen(xml), &st);
}

}  // namespace

// =====================================================================
// Issue #222 — namespace read API on parsed docs
// =====================================================================

TEST(NamespaceReadBug, DefaultNamespaceResolves) {
    auto doc = Parse("<root xmlns='http://default'><child/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    const char* ns = taurus_element_namespace(root);
    EXPECT_STREQ(ns, "http://default");
    taurus_document_free(doc);
}

TEST(NamespaceReadBug, PrefixedNamespaceResolvesViaLookup) {
    auto doc = Parse("<root xmlns:foo='http://foo'><foo:child/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);

    // The declaration is enumerable.
    EXPECT_EQ(taurus_element_namespace_count(root), 1u);
    EXPECT_STREQ(taurus_element_namespace_decl_prefix(root, 0), "foo");
    EXPECT_STREQ(taurus_element_namespace_decl_uri(root, 0), "http://foo");

    // namespace_for_prefix must find the declared prefix.
    const char* uri = taurus_element_namespace_for_prefix(root, "foo");
    EXPECT_STREQ(uri, "http://foo");

    // An unknown prefix returns NULL.
    EXPECT_EQ(taurus_element_namespace_for_prefix(root, "missing"), nullptr);

    taurus_document_free(doc);
}

TEST(NamespaceReadBug, NamespaceInheritsFromParent) {
    auto doc = Parse(
        "<root xmlns:foo='http://foo'>"
          "<foo:child>"
            "<foo:grandchild/>"
          "</foo:child>"
        "</root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_first_child(root, "child");
    ASSERT_NE(child, nullptr);
    TaurusElement grandchild = taurus_element_first_child(child, "grandchild");
    ASSERT_NE(grandchild, nullptr);

    // grandchild's namespace must resolve via parent lookup chain.
    const char* ns = taurus_element_namespace(grandchild);
    EXPECT_STREQ(ns, "http://foo");
    taurus_document_free(doc);
}

// =====================================================================
// Issue #223 — taurus_node_line
// =====================================================================

TEST(NodeLineBug, RootReportsLine1) {
    auto doc = Parse("<root>\n  <a/>\n</root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_node_line(taurus_element_as_node(root)), 1);
    taurus_document_free(doc);
}

TEST(NodeLineBug, ChildOnSecondLineReportsTwo) {
    auto doc = Parse("<root>\n  <a/>\n</root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child(root, "a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(taurus_node_line(taurus_element_as_node(a)), 2);
    taurus_document_free(doc);
}

TEST(NodeLineBug, MultilineDocLinesIncrease) {
    auto doc = Parse(
        "<root>\n"
        "  <a/>\n"
        "  <b/>\n"
        "  <c/>\n"
        "</root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child(root, "a");
    TaurusElement b = taurus_element_first_child(root, "b");
    TaurusElement c = taurus_element_first_child(root, "c");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(taurus_node_line(taurus_element_as_node(a)), 2);
    EXPECT_EQ(taurus_node_line(taurus_element_as_node(b)), 3);
    EXPECT_EQ(taurus_node_line(taurus_element_as_node(c)), 4);
    taurus_document_free(doc);
}

TEST(NodeLineBug, ProgrammaticNodeReportsZero) {
    auto doc = Parse("<root/>");
    TaurusElement created = taurus_element_create(doc, "fresh");
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(taurus_node_line(taurus_element_as_node(created)), 0);
    taurus_document_free(doc);
}

TEST(NodeLineBug, NullReturnsZero) {
    EXPECT_EQ(taurus_node_line(nullptr), 0);
}

// =====================================================================
// Minor — taurus_element_has_attribute
// =====================================================================

TEST(ElementHasAttribute, Returns1ForPresentAttribute) {
    auto doc = Parse("<r a='1' b='2'/>");
    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_element_has_attribute(root, "a"), 1);
    EXPECT_EQ(taurus_element_has_attribute(root, "b"), 1);
    taurus_document_free(doc);
}

TEST(ElementHasAttribute, Returns0ForMissingAttribute) {
    auto doc = Parse("<r a='1'/>");
    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_element_has_attribute(root, "missing"), 0);
    taurus_document_free(doc);
}

TEST(ElementHasAttribute, NullArgsReturn0) {
    EXPECT_EQ(taurus_element_has_attribute(nullptr, "a"), 0);
    auto doc = Parse("<r/>");
    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_element_has_attribute(root, nullptr), 0);
    taurus_document_free(doc);
}

// =====================================================================
// Minor — taurus_xinclude_get_encoding now exported
// =====================================================================

TEST(XincludeEncodingAccessor, ReturnsNullForNonIncludeElement) {
    auto doc = Parse("<root/>");
    TaurusElement root = taurus_document_root(doc);
    // Not an xi:include element → NULL.
    EXPECT_EQ(taurus_xinclude_get_encoding(root), nullptr);
    taurus_document_free(doc);
}

TEST(XincludeEncodingAccessor, ReturnsEncodingAttr) {
    auto doc = Parse(
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
          "<xi:include href='a.txt' parse='text' encoding='UTF-8'/>"
        "</root>");
    TaurusElement root = taurus_document_root(doc);
    TaurusElement xi = taurus_element_first_child(root, "include");
    ASSERT_NE(xi, nullptr);
    const char* enc = taurus_xinclude_get_encoding(xi);
    EXPECT_STREQ(enc, "UTF-8");
    taurus_document_free(doc);
}
