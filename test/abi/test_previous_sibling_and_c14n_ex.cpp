// test/abi/test_previous_sibling_and_c14n_ex.cpp — Bug fixes for #182, #183.
//
// #182: leptris_node_previous_sibling now works for any node type.
// #183: Extended C14N API (leptris_c14n_canonicalize_ex / _subtree_ex)
// with mode, inclusive namespaces, and with_comments toggle.

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
// Issue #182 — leptris_node_previous_sibling for non-elements
// =====================================================================

TEST(PreviousSibling, TextPreviousIsElement) {
    // The exact reproduce from issue #182.
    LeptrisDocument doc = Parse("<x><a/>middle<b/></x>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    // Walk forward: a -> "middle" -> b
    LeptrisNodeRef a = leptris_node_first_child(leptris_element_as_node(root));
    ASSERT_NE(a, nullptr);
    LeptrisNodeRef mid = leptris_node_next_sibling(a);
    ASSERT_NE(mid, nullptr);
    EXPECT_EQ(leptris_node_get_type(mid), 1);  // text
    LeptrisNodeRef b = leptris_node_next_sibling(mid);
    ASSERT_NE(b, nullptr);

    // Walk backward from b: previous should be "middle" text.
    LeptrisNodeRef mid2 = leptris_node_previous_sibling(b);
    ASSERT_NE(mid2, nullptr);
    EXPECT_EQ(leptris_node_get_type(mid2), 1);  // text

    // previous_sibling of "middle" should be <a/> (this was the bug).
    LeptrisNodeRef a2 = leptris_node_previous_sibling(mid2);
    ASSERT_NE(a2, nullptr);
    EXPECT_EQ(leptris_node_get_type(a2), 0);  // element
    EXPECT_STREQ(leptris_element_name(leptris_node_as_element(a2)), "a");

    leptris_document_free(doc);
}

TEST(PreviousSibling, CommentPreviousIsElement) {
    LeptrisDocument doc = Parse("<r><a/><!-- note --></r>");
    LeptrisElement r = leptris_document_root(doc);
    LeptrisNodeRef a = leptris_node_first_child(leptris_element_as_node(r));
    LeptrisNodeRef c = leptris_node_next_sibling(a);

    EXPECT_EQ(leptris_node_get_type(c), 2);  // comment
    LeptrisNodeRef prev = leptris_node_previous_sibling(c);
    ASSERT_NE(prev, nullptr);
    EXPECT_EQ(leptris_node_get_type(prev), 0);  // element
    EXPECT_STREQ(leptris_element_name(leptris_node_as_element(prev)), "a");

    leptris_document_free(doc);
}

TEST(PreviousSibling, FirstChildHasNoPrevious) {
    LeptrisDocument doc = Parse("<r><a/></r>");
    LeptrisElement r = leptris_document_root(doc);
    LeptrisNodeRef a = leptris_node_first_child(leptris_element_as_node(r));
    EXPECT_EQ(leptris_node_previous_sibling(a), nullptr);
    leptris_document_free(doc);
}

TEST(PreviousSibling, DetachedNodeReturnsNull) {
    LeptrisDocument doc = Parse("<r/>");
    LeptrisElement r = leptris_document_root(doc);
    EXPECT_EQ(leptris_node_previous_sibling(leptris_element_as_node(r)),
              nullptr);
    leptris_document_free(doc);
}

TEST(PreviousSibling, NullSafe) {
    EXPECT_EQ(leptris_node_previous_sibling(nullptr), nullptr);
}

// =====================================================================
// Issue #183 — Extended C14N API
// =====================================================================

TEST(C14NEx, CanonicalWithoutComments) {
    const char xml[] = "<r><!-- a comment --><a>text</a></r>";
    LeptrisDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize_ex(
        doc, LEPTRIS_C14N_1_0, LEPTRIS_C14N_MODE_CANONICAL, nullptr, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(std::string(out).find("<!-- a comment -->"),
              std::string::npos);  // comment stripped
    EXPECT_NE(std::string(out).find("<a>text</a>"), std::string::npos);
    leptris_free_string(out);

    leptris_document_free(doc);
}

TEST(C14NEx, CanonicalWithComments) {
    const char xml[] = "<r><!-- a comment --><a/></r>";
    LeptrisDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize_ex(
        doc, LEPTRIS_C14N_1_0, LEPTRIS_C14N_MODE_CANONICAL, nullptr, 1);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::string(out).find("<!-- a comment -->"),
              std::string::npos);  // comment kept
    leptris_free_string(out);

    leptris_document_free(doc);
}

TEST(C14NEx, SubtreeWithoutComments) {
    const char xml[] = "<r><sub><!-- c --><a/></sub></r>";
    LeptrisDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);
    LeptrisElement r = leptris_document_root(doc);
    LeptrisElement sub = (LeptrisElement)leptris_node_first_child(
        leptris_element_as_node(r));

    char* out = leptris_c14n_canonicalize_subtree_ex(
        sub, LEPTRIS_C14N_1_0, LEPTRIS_C14N_MODE_CANONICAL, nullptr, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(std::string(out).find("<!-- c -->"), std::string::npos);
    EXPECT_NE(std::string(out).find("<a>"), std::string::npos);
    leptris_free_string(out);

    leptris_document_free(doc);
}

TEST(C14NEx, ExclusiveModeDropsUnusedNamespaces) {
    // The key exclusive-C14N behavior: namespaces NOT visibly used
    // in the canonicalized subtree are dropped from the output.
    //
    // Subtree <inner/> visibly uses nothing — its output should
    // contain ZERO xmlns declarations.
    const char xml[] =
        "<SignedRoot xmlns:ds='http://ds' xmlns:foo='http://foo'>"
        "<inner/></SignedRoot>";
    LeptrisDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement inner = (LeptrisElement)leptris_node_first_child(
        leptris_element_as_node(root));

    char* excl = leptris_c14n_canonicalize_subtree_ex(
        inner, LEPTRIS_C14N_1_0, LEPTRIS_C14N_MODE_EXCLUSIVE, nullptr, 1);
    ASSERT_NE(excl, nullptr);
    std::string s(excl);
    /* No xmlns declarations because <inner/> visibly uses nothing. */
    EXPECT_EQ(s.find("xmlns:"), std::string::npos)
        << "exclusive output should not emit unused ns: " << s;
    EXPECT_EQ(s.find("xmlns="), std::string::npos)
        << "exclusive output should not emit default ns: " << s;
    leptris_free_string(excl);

    leptris_document_free(doc);
}

TEST(C14NEx, ExclusiveModeKeepsUsedNamespaces) {
    // Subtree visibly uses ds: prefix via the element's qualified
    // name. Exclusive output MUST emit the corresponding xmlns:ds.
    const char xml[] =
        "<SignedRoot xmlns:ds='http://www.w3.org/2000/09/xmldsig#'>"
        "<ds:Signature/></SignedRoot>";
    LeptrisDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);

    /* Force promote first to ensure the tree is materialized. */
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    LeptrisNodeRef child = leptris_node_first_child(leptris_element_as_node(root));
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(leptris_node_get_type(child), 0);  // element

    LeptrisElement sig = leptris_node_as_element(child);
    ASSERT_NE(sig, nullptr);
    EXPECT_STREQ(leptris_element_name(sig), "Signature");

    char* excl = leptris_c14n_canonicalize_subtree_ex(
        sig, LEPTRIS_C14N_1_0, LEPTRIS_C14N_MODE_EXCLUSIVE, nullptr, 1);
    ASSERT_NE(excl, nullptr);
    std::string s(excl);
    EXPECT_NE(s.find("xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\""),
              std::string::npos)
        << "exclusive output must include visibly-used ns: " << s;
    leptris_free_string(excl);

    leptris_document_free(doc);
}

TEST(C14NEx, InclusiveNsPrefixesForceInclude) {
    // Caller asks exclusive mode to keep "xenc" prefix even though
    // it's not visibly used by the subtree.
    const char xml[] =
        "<SignedRoot xmlns:ds='http://ds' xmlns:xenc='http://xenc'>"
        "<ds:Signature/></SignedRoot>";
    LeptrisDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    LeptrisNodeRef child = leptris_node_first_child(leptris_element_as_node(root));
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(leptris_node_get_type(child), 0);
    LeptrisElement sig = leptris_node_as_element(child);
    ASSERT_NE(sig, nullptr);

    const char* prefixes[] = {"xenc", nullptr};
    char* excl = leptris_c14n_canonicalize_subtree_ex(
        sig, LEPTRIS_C14N_1_0, LEPTRIS_C14N_MODE_EXCLUSIVE, prefixes, 1);
    ASSERT_NE(excl, nullptr);
    std::string s(excl);
    EXPECT_NE(s.find("xmlns:ds="), std::string::npos);
    EXPECT_NE(s.find("xmlns:xenc="), std::string::npos);
    leptris_free_string(excl);
    leptris_document_free(doc);
}

TEST(C14NEx, InclusiveAndVisibleNoDuplicate) {
    // Issue #194: when a prefix is BOTH visibly used AND in the
    // inclusive list, the output must emit it exactly ONCE. The old
    // implementation emitted it twice, producing invalid XML.
    const char xml[] =
        "<SignedRoot xmlns:ds='http://ds'>"
        "<ds:Signature/></SignedRoot>";
    LeptrisDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisNodeRef child = leptris_node_first_child(leptris_element_as_node(root));
    LeptrisElement sig = leptris_node_as_element(child);

    /* Pass "ds" in the inclusive list — it's also visibly used by
     * the element name. */
    const char* prefixes[] = {"ds", nullptr};
    char* excl = leptris_c14n_canonicalize_subtree_ex(
        sig, LEPTRIS_C14N_1_0, LEPTRIS_C14N_MODE_EXCLUSIVE, prefixes, 1);
    ASSERT_NE(excl, nullptr);
    std::string s(excl);
    /* Must contain exactly ONE xmlns:ds declaration. */
    size_t first = s.find("xmlns:ds=");
    EXPECT_NE(first, std::string::npos);
    size_t second = s.find("xmlns:ds=", first + 1);
    EXPECT_EQ(second, std::string::npos)
        << "duplicate xmlns:ds in output: " << s;
    leptris_free_string(excl);
    leptris_document_free(doc);
}

TEST(C14NEx, ExclusiveOnEmptyDoc) {
    // Exclusive mode on a doc without namespaces — should produce
    // the same output as canonical (no ns filtering needed).
    const char xml[] = "<r><a/></r>";
    LeptrisDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize_ex(
        doc, LEPTRIS_C14N_1_0, LEPTRIS_C14N_MODE_EXCLUSIVE, nullptr, 1);
    EXPECT_NE(out, nullptr);
    if (out) leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14NEx, NullDocReturnsNull) {
    EXPECT_EQ(leptris_c14n_canonicalize_ex(
        nullptr, LEPTRIS_C14N_1_0, LEPTRIS_C14N_MODE_CANONICAL, nullptr, 1),
        nullptr);
    EXPECT_EQ(leptris_c14n_canonicalize_subtree_ex(
        nullptr, LEPTRIS_C14N_1_0, LEPTRIS_C14N_MODE_CANONICAL, nullptr, 1),
        nullptr);
}

TEST(C14NEx, InclusiveNsPrefixesAccepted) {
    // The inclusive_ns_prefixes parameter is currently accepted but
    // not used (canonical mode). Verify the call doesn't crash.
    const char xml[] = "<r><a/></r>";
    LeptrisDocument doc = Parse(xml);
    const char* prefixes[] = {"ds", "xenc", nullptr};
    char* out = leptris_c14n_canonicalize_ex(
        doc, LEPTRIS_C14N_1_0, LEPTRIS_C14N_MODE_CANONICAL, prefixes, 1);
    EXPECT_NE(out, nullptr);
    if (out) leptris_free_string(out);
    leptris_document_free(doc);
}

}  // namespace
