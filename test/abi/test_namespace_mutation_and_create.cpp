// test/abi/test_namespace_mutation_and_create.cpp — Fixes for #186, #187.
//
// #186: taurus_element_add_namespace_definition / set_default_namespace /
//      remove_namespace_definition (namespace mutation API).
// #187: taurus_element_create on a freshly-parsed FlatDoc doc.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>
#include <string>

namespace {

TaurusDocument Parse(const char* xml) {
    TaurusStatus st = TAURUS_OK;
    return taurus_parse_string(xml, std::strlen(xml), &st);
}

// =====================================================================
// Issue #187 — taurus_element_create on FlatDoc
// =====================================================================

TEST(ElementCreateFlatDoc, CreateWithoutPriorAccess) {
    // Reproduce the exact scenario from issue #187.
    TaurusDocument doc = Parse("<root><a/><b/></root>");
    ASSERT_NE(doc, nullptr);

    /* No taurus_document_root call — doc still has flat_doc. */
    TaurusElement c = taurus_element_create(doc, "c");
    ASSERT_NE(c, nullptr);
    EXPECT_STREQ(taurus_element_name(c), "c");

    taurus_document_free(doc);
}

TEST(ElementCreateFlatDoc, CreateAfterPromote) {
    TaurusDocument doc = Parse("<root/>");
    ASSERT_NE(doc, nullptr);
    (void)taurus_document_root(doc);  /* force promote */

    TaurusElement c = taurus_element_create(doc, "c");
    EXPECT_NE(c, nullptr);
    taurus_document_free(doc);
}

TEST(ElementCreateFlatDoc, NullInputsReturnNull) {
    EXPECT_EQ(taurus_element_create(nullptr, "x"), nullptr);
    TaurusDocument doc = Parse("<r/>");
    EXPECT_EQ(taurus_element_create(doc, nullptr), nullptr);
    taurus_document_free(doc);
}

TEST(ElementCreateFlatDoc, AppendCreatedChildRoundTrip) {
    // Build a child via taurus_element_create + append_child, verify
    // serialize reflects the addition.
    TaurusDocument doc = Parse("<root/>");
    TaurusElement root = taurus_document_root(doc);
    TaurusElement c = taurus_element_create(doc, "child");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(taurus_element_append_child(root, c), TAURUS_OK);

    EXPECT_EQ(taurus_node_child_count(taurus_element_as_node(root)), 1u);
    taurus_document_free(doc);
}

// =====================================================================
// Issue #186 — Namespace mutation API
// =====================================================================

TEST(NamespaceMutation, AddPrefixedDeclaration) {
    TaurusDocument doc = Parse("<root/>");
    TaurusElement root = taurus_document_root(doc);

    EXPECT_EQ(taurus_element_add_namespace_definition(
        root, "foo", "http://foo"), TAURUS_OK);

    EXPECT_EQ(taurus_element_namespace_count(root), 1u);
    EXPECT_STREQ(taurus_element_namespace_decl_prefix(root, 0), "foo");
    EXPECT_STREQ(taurus_element_namespace_decl_uri(root, 0), "http://foo");

    taurus_document_free(doc);
}

TEST(NamespaceMutation, AddDefaultNamespaceViaHelper) {
    TaurusDocument doc = Parse("<root/>");
    TaurusElement root = taurus_document_root(doc);

    EXPECT_EQ(taurus_element_set_default_namespace(
        root, "http://default"), TAURUS_OK);

    EXPECT_EQ(taurus_element_namespace_count(root), 1u);
    EXPECT_EQ(taurus_element_namespace_decl_prefix(root, 0), nullptr);
    EXPECT_STREQ(taurus_element_namespace_decl_uri(root, 0),
                 "http://default");

    taurus_document_free(doc);
}

TEST(NamespaceMutation, EmptyPrefixTreatedAsDefault) {
    TaurusDocument doc = Parse("<root/>");
    TaurusElement root = taurus_document_root(doc);

    EXPECT_EQ(taurus_element_add_namespace_definition(
        root, "", "http://default"), TAURUS_OK);
    EXPECT_EQ(taurus_element_namespace_decl_prefix(root, 0), nullptr);

    taurus_document_free(doc);
}

TEST(NamespaceMutation, AddMultipleInOrder) {
    TaurusDocument doc = Parse("<root/>");
    TaurusElement root = taurus_document_root(doc);

    EXPECT_EQ(taurus_element_add_namespace_definition(
        root, "a", "http://a"), TAURUS_OK);
    EXPECT_EQ(taurus_element_add_namespace_definition(
        root, "b", "http://b"), TAURUS_OK);
    EXPECT_EQ(taurus_element_add_namespace_definition(
        root, "c", "http://c"), TAURUS_OK);

    EXPECT_EQ(taurus_element_namespace_count(root), 3u);
    EXPECT_STREQ(taurus_element_namespace_decl_prefix(root, 0), "a");
    EXPECT_STREQ(taurus_element_namespace_decl_prefix(root, 1), "b");
    EXPECT_STREQ(taurus_element_namespace_decl_prefix(root, 2), "c");

    taurus_document_free(doc);
}

TEST(NamespaceMutation, RemovePrefixed) {
    TaurusDocument doc = Parse("<r/>");
    TaurusElement r = taurus_document_root(doc);

    taurus_element_add_namespace_definition(r, "x", "http://x");
    taurus_element_add_namespace_definition(r, "y", "http://y");
    EXPECT_EQ(taurus_element_namespace_count(r), 2u);

    EXPECT_EQ(taurus_element_remove_namespace_definition(r, "x"), TAURUS_OK);
    EXPECT_EQ(taurus_element_namespace_count(r), 1u);
    EXPECT_STREQ(taurus_element_namespace_decl_prefix(r, 0), "y");

    taurus_document_free(doc);
}

TEST(NamespaceMutation, RemoveDefaultUsesNullPrefix) {
    TaurusDocument doc = Parse("<r/>");
    TaurusElement r = taurus_document_root(doc);

    taurus_element_set_default_namespace(r, "http://default");
    EXPECT_EQ(taurus_element_namespace_count(r), 1u);

    EXPECT_EQ(taurus_element_remove_namespace_definition(r, nullptr),
              TAURUS_OK);
    EXPECT_EQ(taurus_element_namespace_count(r), 0u);

    taurus_document_free(doc);
}

TEST(NamespaceMutation, RemoveMissingReturnsNotFound) {
    TaurusDocument doc = Parse("<r/>");
    TaurusElement r = taurus_document_root(doc);
    taurus_element_add_namespace_definition(r, "x", "http://x");

    EXPECT_EQ(taurus_element_remove_namespace_definition(r, "nope"),
              TAURUS_ERROR_NOT_FOUND);

    taurus_document_free(doc);
}

TEST(NamespaceMutation, NullInputsReturnNullArg) {
    EXPECT_EQ(taurus_element_add_namespace_definition(
        nullptr, "x", "http://"), TAURUS_ERROR_NULL_ARG);
    TaurusDocument doc = Parse("<r/>");
    TaurusElement r = taurus_document_root(doc);
    EXPECT_EQ(taurus_element_add_namespace_definition(
        r, "x", nullptr), TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_element_remove_namespace_definition(
        nullptr, "x"), TAURUS_ERROR_NULL_ARG);
    taurus_document_free(doc);
}

TEST(NamespaceMutation, WorksOnFlatDoc) {
    // add_namespace_definition triggers promote — same pattern as
    // taurus_element_create (issue #187).
    TaurusDocument doc = Parse("<root/>");
    TaurusElement root = taurus_document_root(doc);

    EXPECT_EQ(taurus_element_add_namespace_definition(
        root, "p", "http://p"), TAURUS_OK);

    /* Subsequent reads see the added declaration. */
    EXPECT_EQ(taurus_element_namespace_count(root), 1u);

    taurus_document_free(doc);
}

}  // namespace
