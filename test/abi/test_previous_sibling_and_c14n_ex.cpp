// test/abi/test_previous_sibling_and_c14n_ex.cpp — Bug fixes for #182, #183.
//
// #182: taurus_node_previous_sibling now works for any node type.
// #183: Extended C14N API (taurus_c14n_canonicalize_ex / _subtree_ex)
// with mode, inclusive namespaces, and with_comments toggle.

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
// Issue #182 — taurus_node_previous_sibling for non-elements
// =====================================================================

TEST(PreviousSibling, TextPreviousIsElement) {
    // The exact reproduce from issue #182.
    TaurusDocument doc = Parse("<x><a/>middle<b/></x>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);

    // Walk forward: a -> "middle" -> b
    TaurusNodeRef a = taurus_node_first_child(taurus_element_as_node(root));
    ASSERT_NE(a, nullptr);
    TaurusNodeRef mid = taurus_node_next_sibling(a);
    ASSERT_NE(mid, nullptr);
    EXPECT_EQ(taurus_node_get_type(mid), 1);  // text
    TaurusNodeRef b = taurus_node_next_sibling(mid);
    ASSERT_NE(b, nullptr);

    // Walk backward from b: previous should be "middle" text.
    TaurusNodeRef mid2 = taurus_node_previous_sibling(b);
    ASSERT_NE(mid2, nullptr);
    EXPECT_EQ(taurus_node_get_type(mid2), 1);  // text

    // previous_sibling of "middle" should be <a/> (this was the bug).
    TaurusNodeRef a2 = taurus_node_previous_sibling(mid2);
    ASSERT_NE(a2, nullptr);
    EXPECT_EQ(taurus_node_get_type(a2), 0);  // element
    EXPECT_STREQ(taurus_element_name(taurus_node_as_element(a2)), "a");

    taurus_document_free(doc);
}

TEST(PreviousSibling, CommentPreviousIsElement) {
    TaurusDocument doc = Parse("<r><a/><!-- note --></r>");
    TaurusElement r = taurus_document_root(doc);
    TaurusNodeRef a = taurus_node_first_child(taurus_element_as_node(r));
    TaurusNodeRef c = taurus_node_next_sibling(a);

    EXPECT_EQ(taurus_node_get_type(c), 2);  // comment
    TaurusNodeRef prev = taurus_node_previous_sibling(c);
    ASSERT_NE(prev, nullptr);
    EXPECT_EQ(taurus_node_get_type(prev), 0);  // element
    EXPECT_STREQ(taurus_element_name(taurus_node_as_element(prev)), "a");

    taurus_document_free(doc);
}

TEST(PreviousSibling, FirstChildHasNoPrevious) {
    TaurusDocument doc = Parse("<r><a/></r>");
    TaurusElement r = taurus_document_root(doc);
    TaurusNodeRef a = taurus_node_first_child(taurus_element_as_node(r));
    EXPECT_EQ(taurus_node_previous_sibling(a), nullptr);
    taurus_document_free(doc);
}

TEST(PreviousSibling, DetachedNodeReturnsNull) {
    TaurusDocument doc = Parse("<r/>");
    TaurusElement r = taurus_document_root(doc);
    EXPECT_EQ(taurus_node_previous_sibling(taurus_element_as_node(r)),
              nullptr);
    taurus_document_free(doc);
}

TEST(PreviousSibling, NullSafe) {
    EXPECT_EQ(taurus_node_previous_sibling(nullptr), nullptr);
}

// =====================================================================
// Issue #183 — Extended C14N API
// =====================================================================

TEST(C14NEx, CanonicalWithoutComments) {
    const char xml[] = "<r><!-- a comment --><a>text</a></r>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);

    char* out = taurus_c14n_canonicalize_ex(
        doc, TAURUS_C14N_1_0, TAURUS_C14N_MODE_CANONICAL, nullptr, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(std::string(out).find("<!-- a comment -->"),
              std::string::npos);  // comment stripped
    EXPECT_NE(std::string(out).find("<a>text</a>"), std::string::npos);
    taurus_free_string(out);

    taurus_document_free(doc);
}

TEST(C14NEx, CanonicalWithComments) {
    const char xml[] = "<r><!-- a comment --><a/></r>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);

    char* out = taurus_c14n_canonicalize_ex(
        doc, TAURUS_C14N_1_0, TAURUS_C14N_MODE_CANONICAL, nullptr, 1);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::string(out).find("<!-- a comment -->"),
              std::string::npos);  // comment kept
    taurus_free_string(out);

    taurus_document_free(doc);
}

TEST(C14NEx, SubtreeWithoutComments) {
    const char xml[] = "<r><sub><!-- c --><a/></sub></r>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement r = taurus_document_root(doc);
    TaurusElement sub = (TaurusElement)taurus_node_first_child(
        taurus_element_as_node(r));

    char* out = taurus_c14n_canonicalize_subtree_ex(
        sub, TAURUS_C14N_1_0, TAURUS_C14N_MODE_CANONICAL, nullptr, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(std::string(out).find("<!-- c -->"), std::string::npos);
    EXPECT_NE(std::string(out).find("<a>"), std::string::npos);
    taurus_free_string(out);

    taurus_document_free(doc);
}

TEST(C14NEx, ExclusiveModeFallsBackToCanonical) {
    // Exclusive C14N semantics land in a future release; the API
    // surface works and returns canonical-form output for now.
    const char xml[] = "<r xmlns='http://default'><a/></r>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);

    char* out = taurus_c14n_canonicalize_ex(
        doc, TAURUS_C14N_1_0, TAURUS_C14N_MODE_EXCLUSIVE, nullptr, 1);
    EXPECT_NE(out, nullptr);
    if (out) taurus_free_string(out);

    taurus_document_free(doc);
}

TEST(C14NEx, NullDocReturnsNull) {
    EXPECT_EQ(taurus_c14n_canonicalize_ex(
        nullptr, TAURUS_C14N_1_0, TAURUS_C14N_MODE_CANONICAL, nullptr, 1),
        nullptr);
    EXPECT_EQ(taurus_c14n_canonicalize_subtree_ex(
        nullptr, TAURUS_C14N_1_0, TAURUS_C14N_MODE_CANONICAL, nullptr, 1),
        nullptr);
}

TEST(C14NEx, InclusiveNsPrefixesAccepted) {
    // The inclusive_ns_prefixes parameter is currently accepted but
    // not used (canonical mode). Verify the call doesn't crash.
    const char xml[] = "<r><a/></r>";
    TaurusDocument doc = Parse(xml);
    const char* prefixes[] = {"ds", "xenc", nullptr};
    char* out = taurus_c14n_canonicalize_ex(
        doc, TAURUS_C14N_1_0, TAURUS_C14N_MODE_CANONICAL, prefixes, 1);
    EXPECT_NE(out, nullptr);
    if (out) taurus_free_string(out);
    taurus_document_free(doc);
}

}  // namespace
