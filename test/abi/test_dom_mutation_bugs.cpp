// test/abi/test_dom_mutation_bugs.cpp — Regression tests for #213, #216, #217.
//
// #213: leptris_element_child_count / leptris_node_child_count always
//      returned 0 on parsed docs because direct_parse and flat_promote
//      did not maintain elem->child_count.
// #216: leptris_element_insert_after/_before silently rejected non-element
//      new_node (returned LEPTRIS_ERROR_INVALID_ARG).
// #217: leptris_element_append_child_internal did not unlink a child from
//      its current parent before re-parenting, corrupting both trees.

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstring>
#include <string>

namespace {

LeptrisDocument Parse(const char* xml) {
    LeptrisStatus st = LEPTRIS_OK;
    return leptris_parse_string(xml, std::strlen(xml), &st);
}

// Node type codes from leptris_node_get_type (matches LeptrisNodeTypeEnum).
constexpr int kElem    = 0;
constexpr int kText    = 1;
constexpr int kComment = 2;
constexpr int kCdata   = 3;
constexpr int kPi      = 4;

// Helper: collect child element names in order via the element walk.
static std::string childElementNames(LeptrisElement parent) {
    std::string out;
    LeptrisElement c = leptris_element_first_child_any(parent);
    while (c) {
        if (!out.empty()) out += ",";
        out += leptris_element_name(c);
        c = leptris_element_next_sibling_any(c);
    }
    return out;
}

// Helper: collect all child node types in order via the generic walk.
static std::string childNodeTypes(LeptrisElement parent) {
    std::string out;
    LeptrisNodeRef n = leptris_node_first_child(leptris_element_as_node(parent));
    while (n) {
        if (!out.empty()) out += ",";
        switch (leptris_node_get_type(n)) {
            case kElem:    out += "E"; break;
            case kText:    out += "T"; break;
            case kComment: out += "C"; break;
            case kCdata:   out += "D"; break;
            case kPi:      out += "P"; break;
            default:       out += "?"; break;
        }
        n = leptris_node_next_sibling(n);
    }
    return out;
}

}  // namespace

// =====================================================================
// Issue #213 — child_count returns 0 on parsed docs
// =====================================================================

TEST(ChildCountBug, DirectParsePath) {
    // Plain XML hits the direct_parse fast path.
    LeptrisDocument doc = Parse("<root><a/><b/><c/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(leptris_element_child_count(root), 3u)
        << "direct_parse must maintain elem->child_count";
    leptris_document_free(doc);
}

TEST(ChildCountBug, SkipsNonElementChildren) {
    // Comment between elements is skipped (matches man-page contract:
    // child_count counts elements only).
    LeptrisDocument doc = Parse("<root><a/><!--c--><b/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(leptris_element_child_count(root), 2u);
    leptris_document_free(doc);
}

TEST(ChildCountBug, NestedElements) {
    LeptrisDocument doc = Parse(
        "<root>"
          "<a><x/><y/></a>"
          "<b/>"
        "</root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child_any(root);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(leptris_element_child_count(a), 2u);
    EXPECT_EQ(leptris_element_child_count(root), 2u);
    leptris_document_free(doc);
}

TEST(ChildCountBug, NodeChildCountMatches) {
    LeptrisDocument doc = Parse("<root><a/><b/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_node_child_count(leptris_element_as_node(root)), 2u);
    leptris_document_free(doc);
}

// =====================================================================
// Issue #217 — append_child unlinks child from current parent
// =====================================================================

TEST(AppendChildUnlink, MovesChildBetweenParents) {
    LeptrisDocument doc = Parse("<root><from><move/></from><to/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement from = leptris_element_first_child_any(root);
    LeptrisElement to = leptris_element_next_sibling_any(from);
    LeptrisElement move = leptris_element_first_child_any(from);

    EXPECT_EQ(leptris_element_append_child(to, move), LEPTRIS_OK);

    // 'from' no longer has 'move' as a child.
    EXPECT_EQ(leptris_element_child_count(from), 0u);
    EXPECT_EQ(childElementNames(from), "");

    // 'to' now has 'move' as its only child.
    EXPECT_EQ(leptris_element_child_count(to), 1u);
    EXPECT_EQ(childElementNames(to), "move");

    // Serializing should produce exactly one <move>.
    char* serialized = leptris_document_serialize(doc, NULL);
    ASSERT_NE(serialized, nullptr);
    std::string s(serialized);
    EXPECT_EQ(s.find("<move"), s.rfind("<move"))
        << "append_child must not leave a duplicate in the old parent";
    free(serialized);
    leptris_document_free(doc);
}

// =====================================================================
// Issue #216 — insert_after/_before support non-element new_node
// =====================================================================

TEST(InsertNonElement, InsertTextBefore) {
    LeptrisDocument doc = Parse("<root><a/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child_any(root);

    LeptrisNodeRef text = leptris_text_node_create(doc, "hello");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(leptris_element_insert_before(a, (LeptrisElement)text), LEPTRIS_OK);

    EXPECT_EQ(childNodeTypes(root), "T,E");
    leptris_document_free(doc);
}

TEST(InsertNonElement, InsertCommentAfter) {
    LeptrisDocument doc = Parse("<root><a/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child_any(root);

    LeptrisNodeRef c = leptris_comment_node_create(doc, "note");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_element_insert_after(a, (LeptrisElement)c), LEPTRIS_OK);

    EXPECT_EQ(childNodeTypes(root), "E,C");
    leptris_document_free(doc);
}

TEST(InsertNonElement, InsertCdataAndPi) {
    LeptrisDocument doc = Parse("<root><a/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child_any(root);

    LeptrisNodeRef cd = leptris_cdata_node_create(doc, "raw");
    ASSERT_NE(cd, nullptr);
    EXPECT_EQ(leptris_element_insert_after(a, (LeptrisElement)cd), LEPTRIS_OK);

    LeptrisNodeRef pi = leptris_pi_node_create(doc, "p", "v");
    ASSERT_NE(pi, nullptr);
    EXPECT_EQ(leptris_element_insert_after(a, (LeptrisElement)pi), LEPTRIS_OK);

    // Order: a, pi, cdata (each insert_after puts new node right after a).
    EXPECT_EQ(childNodeTypes(root), "E,P,D");
    leptris_document_free(doc);
}

TEST(InsertNonElement, InsertTextMiddle) {
    LeptrisDocument doc = Parse("<root><a/><b/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child_any(root);
    LeptrisElement b = leptris_element_next_sibling_any(a);

    LeptrisNodeRef text = leptris_text_node_create(doc, "mid");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(leptris_element_insert_before(b, (LeptrisElement)text), LEPTRIS_OK);

    EXPECT_EQ(childNodeTypes(root), "E,T,E");
    leptris_document_free(doc);
}
