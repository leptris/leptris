// test/abi/test_dom_deep_copy.cpp — Specs for taurus_element_copy /
// taurus_document_copy (TODO 148 Phase 1).
//
// Detached deep copy of elements (single or cross-document) and
// full-document copy. Backs Node#dup / #clone / Document#dup in
// the Ruby binding.

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

TEST(ElementCopy, NullArgsReturnNull) {
    TaurusDocument doc = Parse("<r/>");
    EXPECT_EQ(taurus_element_copy(nullptr, doc), nullptr);
    EXPECT_EQ(taurus_element_copy(taurus_document_root(doc), nullptr), nullptr);
    taurus_document_free(doc);
}

TEST(ElementCopy, DetachedCopyHasNoParent) {
    TaurusDocument doc = Parse("<root><a><b/></a></root>");
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child(root, "a");
    TaurusElement a_copy = taurus_element_copy(a, doc);
    ASSERT_NE(a_copy, nullptr);
    EXPECT_EQ(taurus_node_parent(taurus_element_as_node(a_copy)), nullptr);
    taurus_document_free(doc);
}

TEST(ElementCopy, SubtreeIsDeepCopied) {
    TaurusDocument doc = Parse("<root><a x='1' y='2'><b>text</b><c/></a></root>");
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child(root, "a");
    TaurusElement copy = taurus_element_copy(a, doc);
    ASSERT_NE(copy, nullptr);

    // Same name + attrs.
    EXPECT_STREQ(taurus_element_name(copy), "a");
    EXPECT_EQ(taurus_element_attribute_count(copy), 2u);
    EXPECT_STREQ(taurus_element_attribute(copy, "x"), "1");
    EXPECT_STREQ(taurus_element_attribute(copy, "y"), "2");

    // Children preserved.
    EXPECT_EQ(taurus_element_child_count(copy), 2u);
    TaurusElement b = taurus_element_first_child(copy, "b");
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(taurus_element_text(b), "text");

    // Mutating source doesn't affect copy.
    TaurusElement src_b = taurus_element_first_child(a, "b");
    ASSERT_NE(src_b, nullptr);
    EXPECT_NE(src_b, b);
    taurus_document_free(doc);
}

TEST(ElementCopy, CrossDocumentCopySucceeds) {
    TaurusDocument src = Parse("<r><a><b/></a></r>");
    TaurusDocument dest = Parse("<dest/>");
    TaurusElement r = taurus_document_root(src);
    TaurusElement a = taurus_element_first_child(r, "a");
    TaurusElement copy = taurus_element_copy(a, dest);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(taurus_node_parent(taurus_element_as_node(copy)), nullptr);
    // Attach to dest and verify ownership transfers cleanly.
    EXPECT_EQ(taurus_element_append_child(taurus_document_root(dest), copy),
              TAURUS_OK);
    taurus_document_free(src);
    taurus_document_free(dest);
}

TEST(DocumentCopy, NullArgReturnsNull) {
    EXPECT_EQ(taurus_document_copy(nullptr), nullptr);
}

TEST(DocumentCopy, RoundTripsStructureAndDeclaration) {
    TaurusDocument src = Parse(
        "<?xml version='1.0' encoding='UTF-8'?>"
        "<root><a/><b/></root>");
    TaurusDocument dest = taurus_document_copy(src);
    ASSERT_NE(dest, nullptr);
    EXPECT_STREQ(taurus_document_encoding(dest), "UTF-8");
    TaurusElement root = taurus_document_root(dest);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(taurus_element_child_count(root), 2u);
    taurus_document_free(src);
    taurus_document_free(dest);
}

TEST(DocumentCopy, MutatingSourceDoesNotAffectCopy) {
    TaurusDocument src = Parse("<r><a/></r>");
    TaurusDocument dest = taurus_document_copy(src);
    TaurusElement src_root = taurus_document_root(src);
    TaurusElement dest_root = taurus_document_root(dest);
    ASSERT_NE(src_root, dest_root);
    // Append a child to src after copy.
    TaurusElement c = taurus_element_create(src, "c");
    taurus_element_append_child(src_root, c);
    // dest must be unchanged.
    EXPECT_EQ(taurus_element_child_count(dest_root), 1u);
    EXPECT_EQ(taurus_element_child_count(src_root), 2u);
    taurus_document_free(src);
    taurus_document_free(dest);
}
