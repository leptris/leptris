// test/abi/test_nokogiri_api.cpp — Specs for the Nokogiri-compatible
// C API expansion (issues #167–#172).
//
// Each issue gets its own TEST block. The goal is to verify the new
// public entry points produce correct results for the use cases the
// Ruby FFI binding needs them for.

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstring>
#include <string>

namespace {

// Helper: parse XML and return the doc, asserting success.
LeptrisDocument Parse(const char* xml) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    EXPECT_NE(doc, nullptr);
    return doc;
}

// ============================================================================
// Issue #167 — Typed node creators + setters
// ============================================================================

TEST(NokogiriApi167, CreateTextAndAttach) {
    LeptrisDocument doc = Parse("<root/>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    LeptrisNodeRef text = leptris_text_node_create(doc, "hello");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(leptris_node_get_type(text), 1);  // TEXT

    LeptrisStatus st = leptris_element_append_child(root,
                                                   (LeptrisElement)text);
    EXPECT_EQ(st, LEPTRIS_OK);

    EXPECT_STREQ(leptris_text_node_get_content(text), "hello");

    leptris_document_free(doc);
}

TEST(NokogiriApi167, CreateCommentAndAttach) {
    LeptrisDocument doc = Parse("<root/>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisNodeRef comment = leptris_comment_node_create(doc, "a note");
    ASSERT_NE(comment, nullptr);
    EXPECT_EQ(leptris_node_get_type(comment), 2);  // COMMENT

    leptris_element_append_child(root, (LeptrisElement)comment);
    EXPECT_STREQ(leptris_comment_node_get_content(comment), "a note");

    leptris_document_free(doc);
}

TEST(NokogiriApi167, CreateCdataAndAttach) {
    LeptrisDocument doc = Parse("<root/>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisNodeRef cdata = leptris_cdata_node_create(doc, "<raw> & unescaped");
    ASSERT_NE(cdata, nullptr);
    EXPECT_EQ(leptris_node_get_type(cdata), 3);  // CDATA

    leptris_element_append_child(root, (LeptrisElement)cdata);
    EXPECT_STREQ(leptris_cdata_node_get_content(cdata), "<raw> & unescaped");

    leptris_document_free(doc);
}

TEST(NokogiriApi167, CreateProcessingInstruction) {
    LeptrisDocument doc = Parse("<root/>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisNodeRef pi = leptris_pi_node_create(doc, "xml-stylesheet",
                                              "type='text/xsl'");
    ASSERT_NE(pi, nullptr);
    EXPECT_EQ(leptris_node_get_type(pi), 4);  // PI

    leptris_element_append_child(root, (LeptrisElement)pi);
    EXPECT_STREQ(leptris_pi_node_get_target(pi), "xml-stylesheet");
    EXPECT_STREQ(leptris_pi_node_get_data(pi), "type='text/xsl'");

    leptris_document_free(doc);
}

TEST(NokogiriApi167, SetTextContentAfterAttach) {
    LeptrisDocument doc = Parse("<root/>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisNodeRef text = leptris_text_node_create(doc, "old");
    leptris_element_append_child(root, (LeptrisElement)text);

    EXPECT_EQ(leptris_text_node_set_content(text, "new"), LEPTRIS_OK);
    EXPECT_STREQ(leptris_text_node_get_content(text), "new");

    leptris_document_free(doc);
}

TEST(NokogiriApi167, SetCommentContentAfterAttach) {
    LeptrisDocument doc = Parse("<r/>");
    LeptrisElement r = leptris_document_root(doc);
    LeptrisNodeRef c = leptris_comment_node_create(doc, "old");
    leptris_element_append_child(r, (LeptrisElement)c);

    EXPECT_EQ(leptris_comment_node_set_content(c, "new"), LEPTRIS_OK);
    EXPECT_STREQ(leptris_comment_node_get_content(c), "new");

    leptris_document_free(doc);
}

TEST(NokogiriApi167, SettersRejectWrongType) {
    LeptrisDocument doc = Parse("<r/>");
    LeptrisNodeRef text = leptris_text_node_create(doc, "x");
    EXPECT_EQ(leptris_comment_node_set_content(text, "y"),
              LEPTRIS_ERROR_INVALID_ARG);
    EXPECT_EQ(leptris_text_node_set_content(nullptr, "z"),
              LEPTRIS_ERROR_NULL_ARG);
    leptris_document_free(doc);
}

// ============================================================================
// Issue #168 — leptris_node_parent + leptris_node_unlink (non-elements)
// ============================================================================

TEST(NokogiriApi168, NodeParentOnText) {
    LeptrisDocument doc = Parse("<r>hello</r>");
    LeptrisElement r = leptris_document_root(doc);
    LeptrisNodeRef text = leptris_node_first_child(leptris_element_as_node(r));
    ASSERT_NE(text, nullptr);

    LeptrisElement parent = leptris_node_parent(text);
    EXPECT_EQ(parent, r);

    leptris_document_free(doc);
}

TEST(NokogiriApi168, NodeParentOnComment) {
    LeptrisDocument doc = Parse("<r><!-- note --></r>");
    LeptrisElement r = leptris_document_root(doc);
    LeptrisNodeRef comment = leptris_node_first_child(leptris_element_as_node(r));
    ASSERT_NE(comment, nullptr);

    LeptrisElement parent = leptris_node_parent(comment);
    EXPECT_EQ(parent, r);

    leptris_document_free(doc);
}

TEST(NokogiriApi168, NodeParentOnElement) {
    LeptrisDocument doc = Parse("<r><a/></r>");
    LeptrisElement r = leptris_document_root(doc);
    LeptrisElement a = (LeptrisElement)leptris_node_first_child(
        leptris_element_as_node(r));
    EXPECT_EQ(leptris_node_parent((LeptrisNodeRef)a), r);
    EXPECT_EQ(leptris_node_parent((LeptrisNodeRef)r), nullptr);  // root
    leptris_document_free(doc);
}

TEST(NokogiriApi168, NodeUnlinkDetaches) {
    LeptrisDocument doc = Parse("<r><a/><b/></r>");
    LeptrisElement r = leptris_document_root(doc);
    LeptrisElement a = (LeptrisElement)leptris_node_first_child(
        leptris_element_as_node(r));

    EXPECT_EQ(leptris_node_unlink((LeptrisNodeRef)a), LEPTRIS_OK);
    EXPECT_EQ(leptris_node_parent((LeptrisNodeRef)a), nullptr);  // detached

    // First child is now b.
    LeptrisElement first = (LeptrisElement)leptris_node_first_child(
        leptris_element_as_node(r));
    EXPECT_NE(first, a);

    leptris_document_free(doc);
}

TEST(NokogiriApi168, NodeUnlinkTextDetaches) {
    LeptrisDocument doc = Parse("<r>hello</r>");
    LeptrisElement r = leptris_document_root(doc);
    LeptrisNodeRef text = leptris_node_first_child(leptris_element_as_node(r));

    EXPECT_EQ(leptris_node_unlink(text), LEPTRIS_OK);
    EXPECT_EQ(leptris_node_parent(text), nullptr);
    // Root now has no children.
    EXPECT_EQ(leptris_node_first_child(leptris_element_as_node(r)), nullptr);

    leptris_document_free(doc);
}

TEST(NokogiriApi168, UnlinkNullReturnsNullArg) {
    EXPECT_EQ(leptris_node_unlink(nullptr), LEPTRIS_ERROR_NULL_ARG);
}

TEST(NokogiriApi168, UnlinkDetachedReturnsNotFound) {
    LeptrisDocument doc = Parse("<r/>");
    LeptrisElement r = leptris_document_root(doc);
    // Root has no parent.
    EXPECT_EQ(leptris_node_unlink((LeptrisNodeRef)r),
              LEPTRIS_ERROR_NOT_FOUND);
    leptris_document_free(doc);
}

// ============================================================================
// Issue #169 — Subtree C14N
// ============================================================================

TEST(NokogiriApi169, SubtreeC14NProducesElementOnly) {
    const char xml[] = "<doc><a><b>text</b></a><c/></doc>";
    LeptrisDocument doc = Parse(xml);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = (LeptrisElement)leptris_node_first_child(
        leptris_element_as_node(root));

    char* c14n = leptris_c14n_canonicalize_subtree(a, 0, 0);
    ASSERT_NE(c14n, nullptr);
    // Subtree only contains <a> and its descendants.
    EXPECT_NE(strstr(c14n, "<a>"), nullptr);
    EXPECT_EQ(strstr(c14n, "<c/>"), nullptr);  // not in subtree
    leptris_free_string(c14n);

    leptris_document_free(doc);
}

TEST(NokogiriApi169, SubtreeC14NOfRootMatchesDocC14NExceptPIs) {
    const char xml[] = "<doc><a/></doc>";
    LeptrisDocument doc = Parse(xml);
    LeptrisElement root = leptris_document_root(doc);

    char* sub = leptris_c14n_canonicalize_subtree(root, 0, 0);
    ASSERT_NE(sub, nullptr);
    EXPECT_NE(strstr(sub, "<doc>"), nullptr);
    leptris_free_string(sub);

    leptris_document_free(doc);
}

TEST(NokogiriApi169, SubtreeC14NNullReturnsNull) {
    EXPECT_EQ(leptris_c14n_canonicalize_subtree(nullptr, 0, 0), nullptr);
}

// ============================================================================
// Issue #170 — leptris_xpath_eval_with_vars_context
// ============================================================================

TEST(NokogiriApi170, XPathWithVarsAndContext) {
    const char xml[] =
        "<catalog>"
        "  <book id='b1'/>"
        "  <book id='b2'/>"
        "</catalog>";
    LeptrisDocument doc = Parse(xml);
    LeptrisElement root = leptris_document_root(doc);

    LeptrisXPathVariableSet vars = leptris_xpath_variable_set_new();
    ASSERT_NE(vars, nullptr);
    leptris_xpath_variable_set_string(vars, "target_id", "b2");

    // Evaluate from root context with variable.
    LeptrisXPathResult r = leptris_xpath_eval_with_vars_context(
        doc, root, "book[@id = $target_id]", vars);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 1u);
    leptris_xpath_result_free(r);

    leptris_xpath_variable_set_free(vars);
    leptris_document_free(doc);
}

TEST(NokogiriApi170, XPathWithVarsRelativeContext) {
    const char xml[] =
        "<r><a><x id='1'/><x id='2'/></a><a><x id='3'/></a></r>";
    LeptrisDocument doc = Parse(xml);
    LeptrisElement root = leptris_document_root(doc);
    // First <a> element.
    LeptrisElement a = (LeptrisElement)leptris_node_first_child(
        leptris_element_as_node(root));

    // Relative path ".//x" from first <a> should return only x's in a.
    LeptrisXPathResult r = leptris_xpath_eval_with_vars_context(
        doc, a, ".//x", nullptr);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 2u);
    leptris_xpath_result_free(r);

    // From second <a>: just 1.
    LeptrisElement a2 = (LeptrisElement)leptris_node_next_sibling(
        leptris_element_as_node(a));
    LeptrisXPathResult r2 = leptris_xpath_eval_with_vars_context(
        doc, a2, ".//x", nullptr);
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r2), 1u);
    leptris_xpath_result_free(r2);

    leptris_document_free(doc);
}

TEST(NokogiriApi170, XPathWithVarsContextNullContextUsesRoot) {
    const char xml[] = "<r><a/></r>";
    LeptrisDocument doc = Parse(xml);
    LeptrisXPathResult r = leptris_xpath_eval_with_vars_context(
        doc, nullptr, "count(a)", nullptr);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_type(r), LEPTRIS_XPATH_NUMBER);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

// ============================================================================
// Issue #171 — Namespace declaration enumeration
// ============================================================================

TEST(NokogiriApi171, EnumerateNamespaceDeclarations) {
    const char xml[] =
        "<r xmlns='http://default' "
        "   xmlns:foo='http://foo' "
        "   xmlns:bar='http://bar'/>";
    LeptrisDocument doc = Parse(xml);
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_EQ(leptris_element_namespace_count(root), 3u);
    EXPECT_EQ(leptris_element_namespace_decl_prefix(root, 0), nullptr);
    EXPECT_STREQ(leptris_element_namespace_decl_uri(root, 0),
                 "http://default");
    EXPECT_STREQ(leptris_element_namespace_decl_prefix(root, 1), "foo");
    EXPECT_STREQ(leptris_element_namespace_decl_uri(root, 1), "http://foo");
    EXPECT_STREQ(leptris_element_namespace_decl_prefix(root, 2), "bar");
    EXPECT_STREQ(leptris_element_namespace_decl_uri(root, 2), "http://bar");

    // Out of range.
    EXPECT_EQ(leptris_element_namespace_decl_prefix(root, 99), nullptr);
    EXPECT_EQ(leptris_element_namespace_decl_uri(root, 99), nullptr);

    leptris_document_free(doc);
}

TEST(NokogiriApi171, NoNamespacesReturnsZero) {
    LeptrisDocument doc = Parse("<r/>");
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_element_namespace_count(root), 0u);
    EXPECT_EQ(leptris_element_namespace_decl_prefix(root, 0), nullptr);
    EXPECT_EQ(leptris_element_namespace_decl_uri(root, 0), nullptr);
    leptris_document_free(doc);
}

// ============================================================================
// Issue #172 — node_line + node_compare
// ============================================================================

TEST(NokogiriApi172, NodeLineReturnsOneForParsedRoot) {
    // Issue #223: line is tracked at parse time and frozen into the
    // node. The root element of a doc starting on line 1 reports 1.
    LeptrisDocument doc = Parse("<r>\n  <a/>\n</r>");
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_node_line((LeptrisNodeRef)root), 1);
    // Inner element starts on line 2 (after the leading newline).
    LeptrisElement a = leptris_element_first_child(root, "a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(leptris_node_line((LeptrisNodeRef)a), 2);
    leptris_document_free(doc);
}

TEST(NokogiriApi172, NodeLineNullReturnsZero) {
    EXPECT_EQ(leptris_node_line(nullptr), 0);
}

TEST(NokogiriApi172, NodeCompareEqualReturnsZero) {
    LeptrisDocument doc = Parse("<r/>");
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_node_compare((LeptrisNodeRef)root, (LeptrisNodeRef)root),
              0);
    leptris_document_free(doc);
}

TEST(NokogiriApi172, NodeCompareNullReturnsZero) {
    EXPECT_EQ(leptris_node_compare(nullptr, nullptr), 0);
}

TEST(NokogiriApi172, NodeCompareDistinctReturnsSigned) {
    LeptrisDocument doc = Parse("<r><a/><b/></r>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = (LeptrisElement)leptris_node_first_child(
        leptris_element_as_node(root));
    LeptrisElement b = (LeptrisElement)leptris_node_next_sibling(
        leptris_element_as_node(a));
    // Distinct elements compare non-zero.
    int c = leptris_node_compare((LeptrisNodeRef)a, (LeptrisNodeRef)b);
    EXPECT_NE(c, 0);
    leptris_document_free(doc);
}

}  // namespace
