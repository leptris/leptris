// test/abi/test_dom_mutation_bugs.cpp — Regression tests for #213, #216, #217.
//
// #213: taurus_element_child_count / taurus_node_child_count always
//      returned 0 on parsed docs because direct_parse and flat_promote
//      did not maintain elem->child_count.
// #216: taurus_element_insert_after/_before silently rejected non-element
//      new_node (returned TAURUS_ERROR_INVALID_ARG).
// #217: taurus_element_append_child_internal did not unlink a child from
//      its current parent before re-parenting, corrupting both trees.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>
#include <string>

namespace {

TaurusDocument Parse(const char* xml) {
    TaurusStatus st = TAURUS_OK;
    return taurus_parse_string(xml, std::strlen(xml), &st);
}

// Node type codes from taurus_node_get_type (matches TaurusNodeTypeEnum).
constexpr int kElem    = 0;
constexpr int kText    = 1;
constexpr int kComment = 2;
constexpr int kCdata   = 3;
constexpr int kPi      = 4;

// Helper: collect child element names in order via the element walk.
static std::string childElementNames(TaurusElement parent) {
    std::string out;
    TaurusElement c = taurus_element_first_child_any(parent);
    while (c) {
        if (!out.empty()) out += ",";
        out += taurus_element_name(c);
        c = taurus_element_next_sibling_any(c);
    }
    return out;
}

// Helper: collect all child node types in order via the generic walk.
static std::string childNodeTypes(TaurusElement parent) {
    std::string out;
    TaurusNodeRef n = taurus_node_first_child(taurus_element_as_node(parent));
    while (n) {
        if (!out.empty()) out += ",";
        switch (taurus_node_get_type(n)) {
            case kElem:    out += "E"; break;
            case kText:    out += "T"; break;
            case kComment: out += "C"; break;
            case kCdata:   out += "D"; break;
            case kPi:      out += "P"; break;
            default:       out += "?"; break;
        }
        n = taurus_node_next_sibling(n);
    }
    return out;
}

}  // namespace

// =====================================================================
// Issue #213 — child_count returns 0 on parsed docs
// =====================================================================

TEST(ChildCountBug, DirectParsePath) {
    // Plain XML hits the direct_parse fast path.
    TaurusDocument doc = Parse("<root><a/><b/><c/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(taurus_element_child_count(root), 3u)
        << "direct_parse must maintain elem->child_count";
    taurus_document_free(doc);
}

TEST(ChildCountBug, SkipsNonElementChildren) {
    // Comment between elements is skipped (matches man-page contract:
    // child_count counts elements only).
    TaurusDocument doc = Parse("<root><a/><!--c--><b/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(taurus_element_child_count(root), 2u);
    taurus_document_free(doc);
}

TEST(ChildCountBug, NestedElements) {
    TaurusDocument doc = Parse(
        "<root>"
          "<a><x/><y/></a>"
          "<b/>"
        "</root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child_any(root);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(taurus_element_child_count(a), 2u);
    EXPECT_EQ(taurus_element_child_count(root), 2u);
    taurus_document_free(doc);
}

TEST(ChildCountBug, NodeChildCountMatches) {
    TaurusDocument doc = Parse("<root><a/><b/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_node_child_count(taurus_element_as_node(root)), 2u);
    taurus_document_free(doc);
}

// =====================================================================
// Issue #217 — append_child unlinks child from current parent
// =====================================================================

TEST(AppendChildUnlink, MovesChildBetweenParents) {
    TaurusDocument doc = Parse("<root><from><move/></from><to/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement from = taurus_element_first_child_any(root);
    TaurusElement to = taurus_element_next_sibling_any(from);
    TaurusElement move = taurus_element_first_child_any(from);

    EXPECT_EQ(taurus_element_append_child(to, move), TAURUS_OK);

    // 'from' no longer has 'move' as a child.
    EXPECT_EQ(taurus_element_child_count(from), 0u);
    EXPECT_EQ(childElementNames(from), "");

    // 'to' now has 'move' as its only child.
    EXPECT_EQ(taurus_element_child_count(to), 1u);
    EXPECT_EQ(childElementNames(to), "move");

    // Serializing should produce exactly one <move>.
    char* serialized = taurus_document_serialize(doc, NULL);
    ASSERT_NE(serialized, nullptr);
    std::string s(serialized);
    EXPECT_EQ(s.find("<move"), s.rfind("<move"))
        << "append_child must not leave a duplicate in the old parent";
    free(serialized);
    taurus_document_free(doc);
}

// =====================================================================
// Issue #216 — insert_after/_before support non-element new_node
// =====================================================================

TEST(InsertNonElement, InsertTextBefore) {
    TaurusDocument doc = Parse("<root><a/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child_any(root);

    TaurusNodeRef text = taurus_text_node_create(doc, "hello");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(taurus_element_insert_before(a, (TaurusElement)text), TAURUS_OK);

    EXPECT_EQ(childNodeTypes(root), "T,E");
    taurus_document_free(doc);
}

TEST(InsertNonElement, InsertCommentAfter) {
    TaurusDocument doc = Parse("<root><a/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child_any(root);

    TaurusNodeRef c = taurus_comment_node_create(doc, "note");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(taurus_element_insert_after(a, (TaurusElement)c), TAURUS_OK);

    EXPECT_EQ(childNodeTypes(root), "E,C");
    taurus_document_free(doc);
}

TEST(InsertNonElement, InsertCdataAndPi) {
    TaurusDocument doc = Parse("<root><a/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child_any(root);

    TaurusNodeRef cd = taurus_cdata_node_create(doc, "raw");
    ASSERT_NE(cd, nullptr);
    EXPECT_EQ(taurus_element_insert_after(a, (TaurusElement)cd), TAURUS_OK);

    TaurusNodeRef pi = taurus_pi_node_create(doc, "p", "v");
    ASSERT_NE(pi, nullptr);
    EXPECT_EQ(taurus_element_insert_after(a, (TaurusElement)pi), TAURUS_OK);

    // Order: a, pi, cdata (each insert_after puts new node right after a).
    EXPECT_EQ(childNodeTypes(root), "E,P,D");
    taurus_document_free(doc);
}

TEST(InsertNonElement, InsertTextMiddle) {
    TaurusDocument doc = Parse("<root><a/><b/></root>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child_any(root);
    TaurusElement b = taurus_element_next_sibling_any(a);

    TaurusNodeRef text = taurus_text_node_create(doc, "mid");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(taurus_element_insert_before(b, (TaurusElement)text), TAURUS_OK);

    EXPECT_EQ(childNodeTypes(root), "E,T,E");
    taurus_document_free(doc);
}
