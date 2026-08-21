// test/abi/test_node_get_xpath.cpp — Specs for leptris_node_get_xpath
// (TODO 148 Phase 3).
//
// Canonical unique XPath string per Nokogiri conventions.

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

TEST(NodeGetXPath, NullReturnsNull) {
    EXPECT_EQ(leptris_node_get_xpath(nullptr), nullptr);
}

TEST(NodeGetXPath, RootIsSlash) {
    auto doc = Parse("<root/>");
    LeptrisElement root = leptris_document_root(doc);
    char* p = leptris_node_get_xpath(leptris_element_as_node(root));
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "/root");
    leptris_free_string(p);
    leptris_document_free(doc);
}

TEST(NodeGetXPath, SingleChildNoIndex) {
    auto doc = Parse("<root><child/></root>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement child = leptris_element_first_child(root, "child");
    char* p = leptris_node_get_xpath(leptris_element_as_node(child));
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "/root/child");
    leptris_free_string(p);
    leptris_document_free(doc);
}

TEST(NodeGetXPath, SameNamedSiblingsGetIndex) {
    auto doc = Parse("<root><a/><a/><a/></root>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child(root, "a");
    LeptrisElement cur = a;
    for (int i = 0; i < 3; ++i) {
        char* p = leptris_node_get_xpath(leptris_element_as_node(cur));
        ASSERT_NE(p, nullptr);
        char expected[32];
        snprintf(expected, sizeof(expected), "/root/a[%d]", i + 1);
        EXPECT_STREQ(p, expected) << "i=" << i;
        leptris_free_string(p);
        cur = (LeptrisElement)leptris_node_next_sibling(leptris_element_as_node(cur));
    }
    leptris_document_free(doc);
}

TEST(NodeGetXPath, NestedPath) {
    auto doc = Parse("<root><a><b><c/></b></a></root>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child(root, "a");
    LeptrisElement b = leptris_element_first_child(a, "b");
    LeptrisElement c = leptris_element_first_child(b, "c");
    char* p = leptris_node_get_xpath(leptris_element_as_node(c));
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "/root/a/b/c");
    leptris_free_string(p);
    leptris_document_free(doc);
}

TEST(NodeGetXPath, TextNodeUsesTextMarker) {
    auto doc = Parse("<root>hello</root>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisNodeRef text = leptris_node_first_child(leptris_element_as_node(root));
    ASSERT_NE(text, nullptr);
    char* p = leptris_node_get_xpath(text);
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "/root/text()");
    leptris_free_string(p);
    leptris_document_free(doc);
}

TEST(NodeGetXPath, CommentNodeUsesCommentMarker) {
    auto doc = Parse("<root><!--cmt--></root>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisNodeRef n = leptris_node_first_child(leptris_element_as_node(root));
    while (n && leptris_node_get_type(n) != 2 /* COMMENT */) {
        n = leptris_node_next_sibling(n);
    }
    ASSERT_NE(n, nullptr);
    char* p = leptris_node_get_xpath(n);
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "/root/comment()");
    leptris_free_string(p);
    leptris_document_free(doc);
}

TEST(NodeGetXPath, SameNodeSamePath) {
    auto doc = Parse("<r><a/><a/></r>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a1 = leptris_element_first_child(root, "a");
    char* p1 = leptris_node_get_xpath(leptris_element_as_node(a1));
    char* p2 = leptris_node_get_xpath(leptris_element_as_node(a1));
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_STREQ(p1, p2);
    leptris_free_string(p1);
    leptris_free_string(p2);
    leptris_document_free(doc);
}

TEST(NodeGetXPath, DifferentNodesDifferentPaths) {
    auto doc = Parse("<r><a/><a/></r>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a1 = leptris_element_first_child(root, "a");
    LeptrisElement a2 = (LeptrisElement)leptris_node_next_sibling(
        leptris_element_as_node(a1));
    char* p1 = leptris_node_get_xpath(leptris_element_as_node(a1));
    char* p2 = leptris_node_get_xpath(leptris_element_as_node(a2));
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_STRNE(p1, p2);
    leptris_free_string(p1);
    leptris_free_string(p2);
    leptris_document_free(doc);
}
