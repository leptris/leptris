// test/abi/test_node_get_xpath.cpp — Specs for taurus_node_get_xpath
// (TODO 148 Phase 3).
//
// Canonical unique XPath string per Nokogiri conventions.

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

TEST(NodeGetXPath, NullReturnsNull) {
    EXPECT_EQ(taurus_node_get_xpath(nullptr), nullptr);
}

TEST(NodeGetXPath, RootIsSlash) {
    auto doc = Parse("<root/>");
    TaurusElement root = taurus_document_root(doc);
    char* p = taurus_node_get_xpath(taurus_element_as_node(root));
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "/root");
    taurus_free_string(p);
    taurus_document_free(doc);
}

TEST(NodeGetXPath, SingleChildNoIndex) {
    auto doc = Parse("<root><child/></root>");
    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_first_child(root, "child");
    char* p = taurus_node_get_xpath(taurus_element_as_node(child));
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "/root/child");
    taurus_free_string(p);
    taurus_document_free(doc);
}

TEST(NodeGetXPath, SameNamedSiblingsGetIndex) {
    auto doc = Parse("<root><a/><a/><a/></root>");
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child(root, "a");
    TaurusElement cur = a;
    for (int i = 0; i < 3; ++i) {
        char* p = taurus_node_get_xpath(taurus_element_as_node(cur));
        ASSERT_NE(p, nullptr);
        char expected[32];
        snprintf(expected, sizeof(expected), "/root/a[%d]", i + 1);
        EXPECT_STREQ(p, expected) << "i=" << i;
        taurus_free_string(p);
        cur = (TaurusElement)taurus_node_next_sibling(taurus_element_as_node(cur));
    }
    taurus_document_free(doc);
}

TEST(NodeGetXPath, NestedPath) {
    auto doc = Parse("<root><a><b><c/></b></a></root>");
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child(root, "a");
    TaurusElement b = taurus_element_first_child(a, "b");
    TaurusElement c = taurus_element_first_child(b, "c");
    char* p = taurus_node_get_xpath(taurus_element_as_node(c));
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "/root/a/b/c");
    taurus_free_string(p);
    taurus_document_free(doc);
}

TEST(NodeGetXPath, TextNodeUsesTextMarker) {
    auto doc = Parse("<root>hello</root>");
    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef text = taurus_node_first_child(taurus_element_as_node(root));
    ASSERT_NE(text, nullptr);
    char* p = taurus_node_get_xpath(text);
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "/root/text()");
    taurus_free_string(p);
    taurus_document_free(doc);
}

TEST(NodeGetXPath, CommentNodeUsesCommentMarker) {
    auto doc = Parse("<root><!--cmt--></root>");
    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef n = taurus_node_first_child(taurus_element_as_node(root));
    while (n && taurus_node_get_type(n) != 2 /* COMMENT */) {
        n = taurus_node_next_sibling(n);
    }
    ASSERT_NE(n, nullptr);
    char* p = taurus_node_get_xpath(n);
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "/root/comment()");
    taurus_free_string(p);
    taurus_document_free(doc);
}

TEST(NodeGetXPath, SameNodeSamePath) {
    auto doc = Parse("<r><a/><a/></r>");
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a1 = taurus_element_first_child(root, "a");
    char* p1 = taurus_node_get_xpath(taurus_element_as_node(a1));
    char* p2 = taurus_node_get_xpath(taurus_element_as_node(a1));
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_STREQ(p1, p2);
    taurus_free_string(p1);
    taurus_free_string(p2);
    taurus_document_free(doc);
}

TEST(NodeGetXPath, DifferentNodesDifferentPaths) {
    auto doc = Parse("<r><a/><a/></r>");
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a1 = taurus_element_first_child(root, "a");
    TaurusElement a2 = (TaurusElement)taurus_node_next_sibling(
        taurus_element_as_node(a1));
    char* p1 = taurus_node_get_xpath(taurus_element_as_node(a1));
    char* p2 = taurus_node_get_xpath(taurus_element_as_node(a2));
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_STRNE(p1, p2);
    taurus_free_string(p1);
    taurus_free_string(p2);
    taurus_document_free(doc);
}
