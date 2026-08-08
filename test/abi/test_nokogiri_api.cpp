// test/abi/test_nokogiri_api.cpp — Specs for the Nokogiri-compatible
// C API expansion (issues #167–#172).
//
// Each issue gets its own TEST block. The goal is to verify the new
// public entry points produce correct results for the use cases the
// Ruby FFI binding needs them for.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>
#include <string>

namespace {

// Helper: parse XML and return the doc, asserting success.
TaurusDocument Parse(const char* xml) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    EXPECT_NE(doc, nullptr);
    return doc;
}

// ============================================================================
// Issue #167 — Typed node creators + setters
// ============================================================================

TEST(NokogiriApi167, CreateTextAndAttach) {
    TaurusDocument doc = Parse("<root/>");
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);

    TaurusNodeRef text = taurus_text_node_create(doc, "hello");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(taurus_node_get_type(text), 1);  // TEXT

    TaurusStatus st = taurus_element_append_child(root,
                                                   (TaurusElement)text);
    EXPECT_EQ(st, TAURUS_OK);

    EXPECT_STREQ(taurus_text_node_get_content(text), "hello");

    taurus_document_free(doc);
}

TEST(NokogiriApi167, CreateCommentAndAttach) {
    TaurusDocument doc = Parse("<root/>");
    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef comment = taurus_comment_node_create(doc, "a note");
    ASSERT_NE(comment, nullptr);
    EXPECT_EQ(taurus_node_get_type(comment), 2);  // COMMENT

    taurus_element_append_child(root, (TaurusElement)comment);
    EXPECT_STREQ(taurus_comment_node_get_content(comment), "a note");

    taurus_document_free(doc);
}

TEST(NokogiriApi167, CreateCdataAndAttach) {
    TaurusDocument doc = Parse("<root/>");
    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef cdata = taurus_cdata_node_create(doc, "<raw> & unescaped");
    ASSERT_NE(cdata, nullptr);
    EXPECT_EQ(taurus_node_get_type(cdata), 3);  // CDATA

    taurus_element_append_child(root, (TaurusElement)cdata);
    EXPECT_STREQ(taurus_cdata_node_get_content(cdata), "<raw> & unescaped");

    taurus_document_free(doc);
}

TEST(NokogiriApi167, CreateProcessingInstruction) {
    TaurusDocument doc = Parse("<root/>");
    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef pi = taurus_pi_node_create(doc, "xml-stylesheet",
                                              "type='text/xsl'");
    ASSERT_NE(pi, nullptr);
    EXPECT_EQ(taurus_node_get_type(pi), 4);  // PI

    taurus_element_append_child(root, (TaurusElement)pi);
    EXPECT_STREQ(taurus_pi_node_get_target(pi), "xml-stylesheet");
    EXPECT_STREQ(taurus_pi_node_get_data(pi), "type='text/xsl'");

    taurus_document_free(doc);
}

TEST(NokogiriApi167, SetTextContentAfterAttach) {
    TaurusDocument doc = Parse("<root/>");
    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef text = taurus_text_node_create(doc, "old");
    taurus_element_append_child(root, (TaurusElement)text);

    EXPECT_EQ(taurus_text_node_set_content(text, "new"), TAURUS_OK);
    EXPECT_STREQ(taurus_text_node_get_content(text), "new");

    taurus_document_free(doc);
}

TEST(NokogiriApi167, SetCommentContentAfterAttach) {
    TaurusDocument doc = Parse("<r/>");
    TaurusElement r = taurus_document_root(doc);
    TaurusNodeRef c = taurus_comment_node_create(doc, "old");
    taurus_element_append_child(r, (TaurusElement)c);

    EXPECT_EQ(taurus_comment_node_set_content(c, "new"), TAURUS_OK);
    EXPECT_STREQ(taurus_comment_node_get_content(c), "new");

    taurus_document_free(doc);
}

TEST(NokogiriApi167, SettersRejectWrongType) {
    TaurusDocument doc = Parse("<r/>");
    TaurusNodeRef text = taurus_text_node_create(doc, "x");
    EXPECT_EQ(taurus_comment_node_set_content(text, "y"),
              TAURUS_ERROR_INVALID_ARG);
    EXPECT_EQ(taurus_text_node_set_content(nullptr, "z"),
              TAURUS_ERROR_NULL_ARG);
    taurus_document_free(doc);
}

// ============================================================================
// Issue #168 — taurus_node_parent + taurus_node_unlink (non-elements)
// ============================================================================

TEST(NokogiriApi168, NodeParentOnText) {
    TaurusDocument doc = Parse("<r>hello</r>");
    TaurusElement r = taurus_document_root(doc);
    TaurusNodeRef text = taurus_node_first_child(taurus_element_as_node(r));
    ASSERT_NE(text, nullptr);

    TaurusElement parent = taurus_node_parent(text);
    EXPECT_EQ(parent, r);

    taurus_document_free(doc);
}

TEST(NokogiriApi168, NodeParentOnComment) {
    TaurusDocument doc = Parse("<r><!-- note --></r>");
    TaurusElement r = taurus_document_root(doc);
    TaurusNodeRef comment = taurus_node_first_child(taurus_element_as_node(r));
    ASSERT_NE(comment, nullptr);

    TaurusElement parent = taurus_node_parent(comment);
    EXPECT_EQ(parent, r);

    taurus_document_free(doc);
}

TEST(NokogiriApi168, NodeParentOnElement) {
    TaurusDocument doc = Parse("<r><a/></r>");
    TaurusElement r = taurus_document_root(doc);
    TaurusElement a = (TaurusElement)taurus_node_first_child(
        taurus_element_as_node(r));
    EXPECT_EQ(taurus_node_parent((TaurusNodeRef)a), r);
    EXPECT_EQ(taurus_node_parent((TaurusNodeRef)r), nullptr);  // root
    taurus_document_free(doc);
}

TEST(NokogiriApi168, NodeUnlinkDetaches) {
    TaurusDocument doc = Parse("<r><a/><b/></r>");
    TaurusElement r = taurus_document_root(doc);
    TaurusElement a = (TaurusElement)taurus_node_first_child(
        taurus_element_as_node(r));

    EXPECT_EQ(taurus_node_unlink((TaurusNodeRef)a), TAURUS_OK);
    EXPECT_EQ(taurus_node_parent((TaurusNodeRef)a), nullptr);  // detached

    // First child is now b.
    TaurusElement first = (TaurusElement)taurus_node_first_child(
        taurus_element_as_node(r));
    EXPECT_NE(first, a);

    taurus_document_free(doc);
}

TEST(NokogiriApi168, NodeUnlinkTextDetaches) {
    TaurusDocument doc = Parse("<r>hello</r>");
    TaurusElement r = taurus_document_root(doc);
    TaurusNodeRef text = taurus_node_first_child(taurus_element_as_node(r));

    EXPECT_EQ(taurus_node_unlink(text), TAURUS_OK);
    EXPECT_EQ(taurus_node_parent(text), nullptr);
    // Root now has no children.
    EXPECT_EQ(taurus_node_first_child(taurus_element_as_node(r)), nullptr);

    taurus_document_free(doc);
}

TEST(NokogiriApi168, UnlinkNullReturnsNullArg) {
    EXPECT_EQ(taurus_node_unlink(nullptr), TAURUS_ERROR_NULL_ARG);
}

TEST(NokogiriApi168, UnlinkDetachedReturnsNotFound) {
    TaurusDocument doc = Parse("<r/>");
    TaurusElement r = taurus_document_root(doc);
    // Root has no parent.
    EXPECT_EQ(taurus_node_unlink((TaurusNodeRef)r),
              TAURUS_ERROR_NOT_FOUND);
    taurus_document_free(doc);
}

// ============================================================================
// Issue #169 — Subtree C14N
// ============================================================================

TEST(NokogiriApi169, SubtreeC14NProducesElementOnly) {
    const char xml[] = "<doc><a><b>text</b></a><c/></doc>";
    TaurusDocument doc = Parse(xml);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = (TaurusElement)taurus_node_first_child(
        taurus_element_as_node(root));

    char* c14n = taurus_c14n_canonicalize_subtree(a, 0, 0);
    ASSERT_NE(c14n, nullptr);
    // Subtree only contains <a> and its descendants.
    EXPECT_NE(strstr(c14n, "<a>"), nullptr);
    EXPECT_EQ(strstr(c14n, "<c/>"), nullptr);  // not in subtree
    taurus_free_string(c14n);

    taurus_document_free(doc);
}

TEST(NokogiriApi169, SubtreeC14NOfRootMatchesDocC14NExceptPIs) {
    const char xml[] = "<doc><a/></doc>";
    TaurusDocument doc = Parse(xml);
    TaurusElement root = taurus_document_root(doc);

    char* sub = taurus_c14n_canonicalize_subtree(root, 0, 0);
    ASSERT_NE(sub, nullptr);
    EXPECT_NE(strstr(sub, "<doc>"), nullptr);
    taurus_free_string(sub);

    taurus_document_free(doc);
}

TEST(NokogiriApi169, SubtreeC14NNullReturnsNull) {
    EXPECT_EQ(taurus_c14n_canonicalize_subtree(nullptr, 0, 0), nullptr);
}

// ============================================================================
// Issue #170 — taurus_xpath_eval_with_vars_context
// ============================================================================

TEST(NokogiriApi170, XPathWithVarsAndContext) {
    const char xml[] =
        "<catalog>"
        "  <book id='b1'/>"
        "  <book id='b2'/>"
        "</catalog>";
    TaurusDocument doc = Parse(xml);
    TaurusElement root = taurus_document_root(doc);

    TaurusXPathVariableSet vars = taurus_xpath_variable_set_new();
    ASSERT_NE(vars, nullptr);
    taurus_xpath_variable_set_string(vars, "target_id", "b2");

    // Evaluate from root context with variable.
    TaurusXPathResult r = taurus_xpath_eval_with_vars_context(
        doc, root, "book[@id = $target_id]", vars);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 1u);
    taurus_xpath_result_free(r);

    taurus_xpath_variable_set_free(vars);
    taurus_document_free(doc);
}

TEST(NokogiriApi170, XPathWithVarsRelativeContext) {
    const char xml[] =
        "<r><a><x id='1'/><x id='2'/></a><a><x id='3'/></a></r>";
    TaurusDocument doc = Parse(xml);
    TaurusElement root = taurus_document_root(doc);
    // First <a> element.
    TaurusElement a = (TaurusElement)taurus_node_first_child(
        taurus_element_as_node(root));

    // Relative path ".//x" from first <a> should return only x's in a.
    TaurusXPathResult r = taurus_xpath_eval_with_vars_context(
        doc, a, ".//x", nullptr);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 2u);
    taurus_xpath_result_free(r);

    // From second <a>: just 1.
    TaurusElement a2 = (TaurusElement)taurus_node_next_sibling(
        taurus_element_as_node(a));
    TaurusXPathResult r2 = taurus_xpath_eval_with_vars_context(
        doc, a2, ".//x", nullptr);
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r2), 1u);
    taurus_xpath_result_free(r2);

    taurus_document_free(doc);
}

TEST(NokogiriApi170, XPathWithVarsContextNullContextUsesRoot) {
    const char xml[] = "<r><a/></r>";
    TaurusDocument doc = Parse(xml);
    TaurusXPathResult r = taurus_xpath_eval_with_vars_context(
        doc, nullptr, "count(a)", nullptr);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_type(r), TAURUS_XPATH_NUMBER);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

// ============================================================================
// Issue #171 — Namespace declaration enumeration
// ============================================================================

TEST(NokogiriApi171, EnumerateNamespaceDeclarations) {
    const char xml[] =
        "<r xmlns='http://default' "
        "   xmlns:foo='http://foo' "
        "   xmlns:bar='http://bar'/>";
    TaurusDocument doc = Parse(xml);
    TaurusElement root = taurus_document_root(doc);

    EXPECT_EQ(taurus_element_namespace_count(root), 3u);
    EXPECT_EQ(taurus_element_namespace_decl_prefix(root, 0), nullptr);
    EXPECT_STREQ(taurus_element_namespace_decl_uri(root, 0),
                 "http://default");
    EXPECT_STREQ(taurus_element_namespace_decl_prefix(root, 1), "foo");
    EXPECT_STREQ(taurus_element_namespace_decl_uri(root, 1), "http://foo");
    EXPECT_STREQ(taurus_element_namespace_decl_prefix(root, 2), "bar");
    EXPECT_STREQ(taurus_element_namespace_decl_uri(root, 2), "http://bar");

    // Out of range.
    EXPECT_EQ(taurus_element_namespace_decl_prefix(root, 99), nullptr);
    EXPECT_EQ(taurus_element_namespace_decl_uri(root, 99), nullptr);

    taurus_document_free(doc);
}

TEST(NokogiriApi171, NoNamespacesReturnsZero) {
    TaurusDocument doc = Parse("<r/>");
    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_element_namespace_count(root), 0u);
    EXPECT_EQ(taurus_element_namespace_decl_prefix(root, 0), nullptr);
    EXPECT_EQ(taurus_element_namespace_decl_uri(root, 0), nullptr);
    taurus_document_free(doc);
}

// ============================================================================
// Issue #172 — node_line + node_compare
// ============================================================================

TEST(NokogiriApi172, NodeLineReturnsOneForParsedRoot) {
    // Issue #223: line is tracked at parse time and frozen into the
    // node. The root element of a doc starting on line 1 reports 1.
    TaurusDocument doc = Parse("<r>\n  <a/>\n</r>");
    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_node_line((TaurusNodeRef)root), 1);
    // Inner element starts on line 2 (after the leading newline).
    TaurusElement a = taurus_element_first_child(root, "a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(taurus_node_line((TaurusNodeRef)a), 2);
    taurus_document_free(doc);
}

TEST(NokogiriApi172, NodeLineNullReturnsZero) {
    EXPECT_EQ(taurus_node_line(nullptr), 0);
}

TEST(NokogiriApi172, NodeCompareEqualReturnsZero) {
    TaurusDocument doc = Parse("<r/>");
    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_node_compare((TaurusNodeRef)root, (TaurusNodeRef)root),
              0);
    taurus_document_free(doc);
}

TEST(NokogiriApi172, NodeCompareNullReturnsZero) {
    EXPECT_EQ(taurus_node_compare(nullptr, nullptr), 0);
}

TEST(NokogiriApi172, NodeCompareDistinctReturnsSigned) {
    TaurusDocument doc = Parse("<r><a/><b/></r>");
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = (TaurusElement)taurus_node_first_child(
        taurus_element_as_node(root));
    TaurusElement b = (TaurusElement)taurus_node_next_sibling(
        taurus_element_as_node(a));
    // Distinct elements compare non-zero.
    int c = taurus_node_compare((TaurusNodeRef)a, (TaurusNodeRef)b);
    EXPECT_NE(c, 0);
    taurus_document_free(doc);
}

}  // namespace
