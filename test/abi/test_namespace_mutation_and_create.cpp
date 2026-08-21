// test/abi/test_namespace_mutation_and_create.cpp — Fixes for #186, #187.
//
// #186: leptris_element_add_namespace_definition / set_default_namespace /
//      remove_namespace_definition (namespace mutation API).
// #187: leptris_element_create on a freshly-parsed FlatDoc doc.

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstring>
#include <string>

namespace {

LeptrisDocument Parse(const char* xml) {
    LeptrisStatus st = LEPTRIS_OK;
    return leptris_parse_string(xml, std::strlen(xml), &st);
}

// =====================================================================
// Issue #187 — leptris_element_create on FlatDoc
// =====================================================================

TEST(ElementCreateFlatDoc, CreateWithoutPriorAccess) {
    // Reproduce the exact scenario from issue #187.
    LeptrisDocument doc = Parse("<root><a/><b/></root>");
    ASSERT_NE(doc, nullptr);

    /* No leptris_document_root call — doc still has flat_doc. */
    LeptrisElement c = leptris_element_create(doc, "c");
    ASSERT_NE(c, nullptr);
    EXPECT_STREQ(leptris_element_name(c), "c");

    leptris_document_free(doc);
}

TEST(ElementCreateFlatDoc, CreateAfterPromote) {
    LeptrisDocument doc = Parse("<root/>");
    ASSERT_NE(doc, nullptr);
    (void)leptris_document_root(doc);  /* force promote */

    LeptrisElement c = leptris_element_create(doc, "c");
    EXPECT_NE(c, nullptr);
    leptris_document_free(doc);
}

TEST(ElementCreateFlatDoc, NullInputsReturnNull) {
    EXPECT_EQ(leptris_element_create(nullptr, "x"), nullptr);
    LeptrisDocument doc = Parse("<r/>");
    EXPECT_EQ(leptris_element_create(doc, nullptr), nullptr);
    leptris_document_free(doc);
}

TEST(ElementCreateFlatDoc, AppendCreatedChildRoundTrip) {
    // Build a child via leptris_element_create + append_child, verify
    // serialize reflects the addition.
    LeptrisDocument doc = Parse("<root/>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement c = leptris_element_create(doc, "child");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_element_append_child(root, c), LEPTRIS_OK);

    EXPECT_EQ(leptris_node_child_count(leptris_element_as_node(root)), 1u);
    leptris_document_free(doc);
}

// =====================================================================
// Issue #186 — Namespace mutation API
// =====================================================================

TEST(NamespaceMutation, AddPrefixedDeclaration) {
    LeptrisDocument doc = Parse("<root/>");
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_EQ(leptris_element_add_namespace_definition(
        root, "foo", "http://foo"), LEPTRIS_OK);

    EXPECT_EQ(leptris_element_namespace_count(root), 1u);
    EXPECT_STREQ(leptris_element_namespace_decl_prefix(root, 0), "foo");
    EXPECT_STREQ(leptris_element_namespace_decl_uri(root, 0), "http://foo");

    leptris_document_free(doc);
}

TEST(NamespaceMutation, AddDefaultNamespaceViaHelper) {
    LeptrisDocument doc = Parse("<root/>");
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_EQ(leptris_element_set_default_namespace(
        root, "http://default"), LEPTRIS_OK);

    EXPECT_EQ(leptris_element_namespace_count(root), 1u);
    EXPECT_EQ(leptris_element_namespace_decl_prefix(root, 0), nullptr);
    EXPECT_STREQ(leptris_element_namespace_decl_uri(root, 0),
                 "http://default");

    leptris_document_free(doc);
}

TEST(NamespaceMutation, EmptyPrefixTreatedAsDefault) {
    LeptrisDocument doc = Parse("<root/>");
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_EQ(leptris_element_add_namespace_definition(
        root, "", "http://default"), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_namespace_decl_prefix(root, 0), nullptr);

    leptris_document_free(doc);
}

TEST(NamespaceMutation, AddMultipleInOrder) {
    LeptrisDocument doc = Parse("<root/>");
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_EQ(leptris_element_add_namespace_definition(
        root, "a", "http://a"), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_add_namespace_definition(
        root, "b", "http://b"), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_add_namespace_definition(
        root, "c", "http://c"), LEPTRIS_OK);

    EXPECT_EQ(leptris_element_namespace_count(root), 3u);
    EXPECT_STREQ(leptris_element_namespace_decl_prefix(root, 0), "a");
    EXPECT_STREQ(leptris_element_namespace_decl_prefix(root, 1), "b");
    EXPECT_STREQ(leptris_element_namespace_decl_prefix(root, 2), "c");

    leptris_document_free(doc);
}

TEST(NamespaceMutation, RemovePrefixed) {
    LeptrisDocument doc = Parse("<r/>");
    LeptrisElement r = leptris_document_root(doc);

    leptris_element_add_namespace_definition(r, "x", "http://x");
    leptris_element_add_namespace_definition(r, "y", "http://y");
    EXPECT_EQ(leptris_element_namespace_count(r), 2u);

    EXPECT_EQ(leptris_element_remove_namespace_definition(r, "x"), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_namespace_count(r), 1u);
    EXPECT_STREQ(leptris_element_namespace_decl_prefix(r, 0), "y");

    leptris_document_free(doc);
}

TEST(NamespaceMutation, RemoveDefaultUsesNullPrefix) {
    LeptrisDocument doc = Parse("<r/>");
    LeptrisElement r = leptris_document_root(doc);

    leptris_element_set_default_namespace(r, "http://default");
    EXPECT_EQ(leptris_element_namespace_count(r), 1u);

    EXPECT_EQ(leptris_element_remove_namespace_definition(r, nullptr),
              LEPTRIS_OK);
    EXPECT_EQ(leptris_element_namespace_count(r), 0u);

    leptris_document_free(doc);
}

TEST(NamespaceMutation, RemoveMissingReturnsNotFound) {
    LeptrisDocument doc = Parse("<r/>");
    LeptrisElement r = leptris_document_root(doc);
    leptris_element_add_namespace_definition(r, "x", "http://x");

    EXPECT_EQ(leptris_element_remove_namespace_definition(r, "nope"),
              LEPTRIS_ERROR_NOT_FOUND);

    leptris_document_free(doc);
}

TEST(NamespaceMutation, NullInputsReturnNullArg) {
    EXPECT_EQ(leptris_element_add_namespace_definition(
        nullptr, "x", "http://"), LEPTRIS_ERROR_NULL_ARG);
    LeptrisDocument doc = Parse("<r/>");
    LeptrisElement r = leptris_document_root(doc);
    EXPECT_EQ(leptris_element_add_namespace_definition(
        r, "x", nullptr), LEPTRIS_ERROR_NULL_ARG);
    EXPECT_EQ(leptris_element_remove_namespace_definition(
        nullptr, "x"), LEPTRIS_ERROR_NULL_ARG);
    leptris_document_free(doc);
}

TEST(NamespaceMutation, WorksOnFlatDoc) {
    // add_namespace_definition triggers promote — same pattern as
    // leptris_element_create (issue #187).
    LeptrisDocument doc = Parse("<root/>");
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_EQ(leptris_element_add_namespace_definition(
        root, "p", "http://p"), LEPTRIS_OK);

    /* Subsequent reads see the added declaration. */
    EXPECT_EQ(leptris_element_namespace_count(root), 1u);

    leptris_document_free(doc);
}

}  // namespace
