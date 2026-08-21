// test/abi/test_dom_deep_copy.cpp — Specs for leptris_element_copy /
// leptris_document_copy (TODO 148 Phase 1).
//
// Detached deep copy of elements (single or cross-document) and
// full-document copy. Backs Node#dup / #clone / Document#dup in
// the Ruby binding.

#include <gtest/gtest.h>
#include "leptris.h"
#include <cstring>
#include <string>

namespace {
LeptrisDocument Parse(const char* xml) {
    LeptrisStatus st = LEPTRIS_OK;
    return leptris_parse_string(xml, std::strlen(xml), &st);
}
}  // namespace

TEST(ElementCopy, NullArgsReturnNull) {
    LeptrisDocument doc = Parse("<r/>");
    EXPECT_EQ(leptris_element_copy(nullptr, doc), nullptr);
    EXPECT_EQ(leptris_element_copy(leptris_document_root(doc), nullptr), nullptr);
    leptris_document_free(doc);
}

TEST(ElementCopy, DetachedCopyHasNoParent) {
    LeptrisDocument doc = Parse("<root><a><b/></a></root>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child(root, "a");
    LeptrisElement a_copy = leptris_element_copy(a, doc);
    ASSERT_NE(a_copy, nullptr);
    EXPECT_EQ(leptris_node_parent(leptris_element_as_node(a_copy)), nullptr);
    leptris_document_free(doc);
}

TEST(ElementCopy, SubtreeIsDeepCopied) {
    LeptrisDocument doc = Parse("<root><a x='1' y='2'><b>text</b><c/></a></root>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child(root, "a");
    LeptrisElement copy = leptris_element_copy(a, doc);
    ASSERT_NE(copy, nullptr);

    // Same name + attrs.
    EXPECT_STREQ(leptris_element_name(copy), "a");
    EXPECT_EQ(leptris_element_attribute_count(copy), 2u);
    EXPECT_STREQ(leptris_element_attribute(copy, "x"), "1");
    EXPECT_STREQ(leptris_element_attribute(copy, "y"), "2");

    // Children preserved.
    EXPECT_EQ(leptris_element_child_count(copy), 2u);
    LeptrisElement b = leptris_element_first_child(copy, "b");
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(leptris_element_text(b), "text");

    // Mutating source doesn't affect copy.
    LeptrisElement src_b = leptris_element_first_child(a, "b");
    ASSERT_NE(src_b, nullptr);
    EXPECT_NE(src_b, b);
    leptris_document_free(doc);
}

TEST(ElementCopy, CrossDocumentCopySucceeds) {
    LeptrisDocument src = Parse("<r><a><b/></a></r>");
    LeptrisDocument dest = Parse("<dest/>");
    LeptrisElement r = leptris_document_root(src);
    LeptrisElement a = leptris_element_first_child(r, "a");
    LeptrisElement copy = leptris_element_copy(a, dest);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(leptris_node_parent(leptris_element_as_node(copy)), nullptr);
    // Attach to dest and verify ownership transfers cleanly.
    EXPECT_EQ(leptris_element_append_child(leptris_document_root(dest), copy),
              LEPTRIS_OK);
    leptris_document_free(src);
    leptris_document_free(dest);
}

TEST(DocumentCopy, NullArgReturnsNull) {
    EXPECT_EQ(leptris_document_copy(nullptr), nullptr);
}

TEST(DocumentCopy, RoundTripsStructureAndDeclaration) {
    LeptrisDocument src = Parse(
        "<?xml version='1.0' encoding='UTF-8'?>"
        "<root><a/><b/></root>");
    LeptrisDocument dest = leptris_document_copy(src);
    ASSERT_NE(dest, nullptr);
    EXPECT_STREQ(leptris_document_encoding(dest), "UTF-8");
    LeptrisElement root = leptris_document_root(dest);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(leptris_element_child_count(root), 2u);
    leptris_document_free(src);
    leptris_document_free(dest);
}

TEST(DocumentCopy, MutatingSourceDoesNotAffectCopy) {
    LeptrisDocument src = Parse("<r><a/></r>");
    LeptrisDocument dest = leptris_document_copy(src);
    LeptrisElement src_root = leptris_document_root(src);
    LeptrisElement dest_root = leptris_document_root(dest);
    ASSERT_NE(src_root, dest_root);
    // Append a child to src after copy.
    LeptrisElement c = leptris_element_create(src, "c");
    leptris_element_append_child(src_root, c);
    // dest must be unchanged.
    EXPECT_EQ(leptris_element_child_count(dest_root), 1u);
    EXPECT_EQ(leptris_element_child_count(src_root), 2u);
    leptris_document_free(src);
    leptris_document_free(dest);
}
