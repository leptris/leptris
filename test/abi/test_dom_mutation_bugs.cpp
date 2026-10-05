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

// ============================================================================
// #1528: a node arriving from a DIFFERENT document was spliced in as a
// raw pointer. The splice target keeps nodes (and their text content)
// from the source document's pool — when the caller frees that source
// (Document.parse scratch docs), every adopted node dangles and the
// next native walk detonates. The repro family: SAX-built tree, moxml
// in-place cleanup (replace/move/insert), then compiled XPath eval.
// The fix adopts cross-document nodes by deep copy (the set_root
// precedent, #371); same-document moves stay zero-copy.
// ============================================================================

namespace {

// Splice scratch_root into live per fn, free the scratch document,
// then exercise the tree the way the cleanup phase does: a compiled
// XPath evaluation followed by serialization.
static void splice_free_eval(
    void (*splice)(LeptrisDocument live, LeptrisElement target,
                   LeptrisElement scratch_root),
    const char* expected_xml) {
    LeptrisDocument live = Parse("<r><a/><b/></r>");
    ASSERT_NE(live, nullptr);
    LeptrisDocument scratch = Parse("<x>replacement</x>");
    ASSERT_NE(scratch, nullptr);

    LeptrisElement target = leptris_document_root(live);
    ASSERT_NE(target, nullptr);
    LeptrisElement scratch_root = leptris_document_root(scratch);
    ASSERT_NE(scratch_root, nullptr);

    splice(live, target, scratch_root);

    // The scratch document dies here — exactly the moxml lifecycle
    // where the parsed fragment's document drops out of scope.
    leptris_document_free(scratch);

    // Detonation site from the issue: compiled XPath over the
    // mutated tree.
    LeptrisXPathCompiled c = leptris_xpath_compile("count(//x)");
    ASSERT_NE(c, nullptr);
    LeptrisXPathResult r = leptris_xpath_compiled_eval(c, live, nullptr);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);
    leptris_xpath_compiled_free(c);

    LeptrisXPathCompiled s = leptris_xpath_compile("string(//x)");
    ASSERT_NE(s, nullptr);
    LeptrisXPathResult sr = leptris_xpath_compiled_eval(s, live, nullptr);
    ASSERT_NE(sr, nullptr);
    char* text = leptris_xpath_result_string(sr);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "replacement");
    leptris_free_string(text);
    leptris_xpath_result_free(sr);
    leptris_xpath_compiled_free(s);

    // Serialization walks the same spine the axes do.
    char* xml = leptris_document_serialize(live, NULL);
    ASSERT_NE(xml, nullptr);
    EXPECT_STREQ(xml, expected_xml);
    leptris_free_string(xml);

    leptris_document_free(live);
}

}  // namespace

TEST(CrossDocumentAdoption, InsertAfterSurvivesScratchFree) {
    splice_free_eval([](LeptrisDocument, LeptrisElement target,
                        LeptrisElement scratch_root) {
        LeptrisElement a = leptris_element_first_child_any(target);
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(leptris_element_insert_after(a, scratch_root),
                  LEPTRIS_OK);
    },
    "<r><a/><x>replacement</x><b/></r>");
}

TEST(CrossDocumentAdoption, InsertBeforeSurvivesScratchFree) {
    splice_free_eval([](LeptrisDocument, LeptrisElement target,
                        LeptrisElement scratch_root) {
        LeptrisElement b = leptris_element_first_child_any(target);
        ASSERT_NE(b, nullptr);
        b = leptris_element_next_sibling_any(b);
        ASSERT_NE(b, nullptr);
        EXPECT_EQ(leptris_element_insert_before(b, scratch_root),
                  LEPTRIS_OK);
    },
    "<r><a/><x>replacement</x><b/></r>");
}

TEST(CrossDocumentAdoption, AppendChildSurvivesScratchFree) {
    splice_free_eval([](LeptrisDocument, LeptrisElement target,
                        LeptrisElement scratch_root) {
        EXPECT_EQ(leptris_element_append_child(target, scratch_root),
                  LEPTRIS_OK);
    },
    "<r><a/><b/><x>replacement</x></r>");
}

TEST(CrossDocumentAdoption, PrependChildSurvivesScratchFree) {
    splice_free_eval([](LeptrisDocument, LeptrisElement target,
                        LeptrisElement scratch_root) {
        EXPECT_EQ(leptris_element_prepend_child(target, scratch_root),
                  LEPTRIS_OK);
    },
    "<r><x>replacement</x><a/><b/></r>");
}

TEST(CrossDocumentAdoption, ReplaceKeepsSameDocumentMoveZeroCopy) {
    // A same-document move must stay the raw splice (identity and
    // pointer preserved) — the adoption path is cross-document only.
    LeptrisDocument doc = Parse("<r><a/><b/><c/></r>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child_any(root);
    LeptrisElement b = leptris_element_next_sibling_any(a);

    EXPECT_EQ(leptris_element_insert_after(a, b), LEPTRIS_OK);
    char* xml = leptris_document_serialize(doc, NULL);
    ASSERT_NE(xml, nullptr);
    EXPECT_STREQ(xml, "<r><a/><b/><c/></r>");
    leptris_free_string(xml);
    leptris_document_free(doc);
}
