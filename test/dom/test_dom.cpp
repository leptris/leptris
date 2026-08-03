// test/dom/test_dom.cpp — DOM creation/traversal/modification specs.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>

namespace {

// Public API documents these as integers (see taurus_node_get_type docs).
constexpr int kNodeTypeElement  = 0;
constexpr int kNodeTypeText     = 1;
constexpr int kNodeTypeComment  = 2;
constexpr int kNodeTypeCDATA    = 3;
constexpr int kNodeTypePI       = 4;
constexpr int kNodeTypeDoctype  = 5;
constexpr int kNodeTypeAttribute = 6;

TEST(DomBasics, EmptyDocumentRoundTrips) {
    const char xml[] = "<r/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(taurus_element_name(root), "r");
    taurus_document_free(doc);
}

TEST(DomBasics, AttributeLookupByName) {
    const char xml[] = "<r a='1' b='2' c='3'/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    EXPECT_STREQ(taurus_element_attribute(root, "a"), "1");
    EXPECT_STREQ(taurus_element_attribute(root, "b"), "2");
    EXPECT_STREQ(taurus_element_attribute(root, "c"), "3");
    EXPECT_EQ(taurus_element_attribute(root, "missing"), nullptr);

    taurus_document_free(doc);
}

TEST(DomBasics, TraversesChildrenInDocumentOrder) {
    const char xml[] = "<r><a/><b/><c/></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement child = taurus_element_first_child_any(root);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "a");

    child = taurus_element_next_sibling_any(child);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "b");

    child = taurus_element_next_sibling_any(child);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "c");

    child = taurus_element_next_sibling_any(child);
    EXPECT_EQ(child, nullptr);

    taurus_document_free(doc);
}

TEST(DomBasics, NodeRefTraversalCoversAllNodeTypes) {
    // TaurusNodeRef traversal exposes every child regardless of type;
    // TaurusElement-only traversal skips text/comment/cdata siblings.
    const char xml[] = "<r><!--c-->text<x/></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    TaurusNodeRef n = taurus_node_first_child(taurus_element_as_node(root));
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), kNodeTypeComment);

    n = taurus_node_next_sibling(n);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), kNodeTypeText);

    n = taurus_node_next_sibling(n);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), kNodeTypeElement);

    taurus_document_free(doc);
}

}  // namespace

// ---- Freeze API (TODO 88) ------------------------------------------------

TEST(DocumentFreeze, FreshDocumentIsFrozenAfterParse) {
    /* The parser calls taurus_document_freeze_tree internally. */
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<root><child/></root>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(taurus_document_is_frozen(doc), 1);
    taurus_document_free(doc);
}

TEST(DocumentFreeze, ExplicitFreezeSetsFlag) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    /* Already frozen by parser, but explicit freeze should still work. */
    EXPECT_EQ(taurus_document_freeze(doc), TAURUS_OK);
    EXPECT_EQ(taurus_document_is_frozen(doc), 1);
    taurus_document_free(doc);
}

TEST(DocumentFreeze, NullDocReturnsSafe) {
    EXPECT_EQ(taurus_document_freeze(nullptr), TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_document_is_frozen(nullptr), 0);
}
