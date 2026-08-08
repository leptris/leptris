// test/abi/test_flat_promote_line.cpp — Specs for source-line tracking
// through the flat_promote fallback path (TODO 148 Phase 6).
//
// Calls flat_parse + flat_promote directly (the way taurus_parse does
// when direct_parse fails). Verifies line numbers survive the
// FlatNode → TaurusNode copy.

#include <gtest/gtest.h>

extern "C" {
#include "taurus.h"
#include "flat_parser.h"
#include "flat_promote.h"
}

#include <cstring>
#include <string>

namespace {
TaurusDocument FlatRoundTrip(const char* xml) {
    size_t len = std::strlen(xml);
    FlatDoc* flat = flat_parse(xml, len);
    if (!flat) return nullptr;
    return flat_promote(flat);
}
}  // namespace

TEST(FlatPromoteLine, RootReportsLine1) {
    auto doc = FlatRoundTrip("<r/>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(taurus_node_line(taurus_element_as_node(root)), 1);
    taurus_document_free(doc);
}

TEST(FlatPromoteLine, InnerElementOnSecondLine) {
    auto doc = FlatRoundTrip("<r>\n  <a/>\n</r>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child(root, "a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(taurus_node_line(taurus_element_as_node(a)), 2);
    taurus_document_free(doc);
}

TEST(FlatPromoteLine, MultilineDocIncrements) {
    auto doc = FlatRoundTrip(
        "<r>\n"
        "  <a/>\n"
        "  <b/>\n"
        "  <c/>\n"
        "</r>");
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

TEST(FlatPromoteLine, TextNodeCarriesLine) {
    // Text line is the line where the text node STARTS (matching
    // direct_parse's convention — the snapshot is taken before
    // consuming the bytes, so leading newlines don't shift it).
    auto doc = FlatRoundTrip("<r>\n  text</r>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef n = taurus_node_first_child(taurus_element_as_node(root));
    int text_line = 0;
    while (n) {
        if (taurus_node_get_type(n) == 1 /* TEXT */) {
            text_line = taurus_node_line(n);
            break;
        }
        n = taurus_node_next_sibling(n);
    }
    EXPECT_EQ(text_line, 1);
    taurus_document_free(doc);
}

TEST(FlatPromoteLine, CommentNodeCarriesLine) {
    auto doc = FlatRoundTrip("<r>\n  <!--c-->\n</r>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef n = taurus_node_first_child(taurus_element_as_node(root));
    int comment_line = 0;
    while (n) {
        if (taurus_node_get_type(n) == 2 /* COMMENT */) {
            comment_line = taurus_node_line(n);
            break;
        }
        n = taurus_node_next_sibling(n);
    }
    EXPECT_EQ(comment_line, 2);
    taurus_document_free(doc);
}
