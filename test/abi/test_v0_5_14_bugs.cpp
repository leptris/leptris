// test/abi/test_v0_5_14_bugs.cpp — Regression specs for issues #222, #223
// and the minor visibility gaps surfaced in the v0.5.13 user audit.
//
// #213, #216, #217 were verified fixed in v0.5.13; the new specs in
// test_dom_mutation_bugs.cpp already cover them. This file covers the
// NEWLY-fixed bugs from the v0.5.14 cycle.

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

// =====================================================================
// Issue #222 — namespace read API on parsed docs
// =====================================================================

TEST(NamespaceReadBug, DefaultNamespaceResolves) {
    auto doc = Parse("<root xmlns='http://default'><child/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    const char* ns = leptris_element_namespace(root);
    EXPECT_STREQ(ns, "http://default");
    leptris_document_free(doc);
}

TEST(NamespaceReadBug, PrefixedNamespaceResolvesViaLookup) {
    auto doc = Parse("<root xmlns:foo='http://foo'><foo:child/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    // The declaration is enumerable.
    EXPECT_EQ(leptris_element_namespace_count(root), 1u);
    EXPECT_STREQ(leptris_element_namespace_decl_prefix(root, 0), "foo");
    EXPECT_STREQ(leptris_element_namespace_decl_uri(root, 0), "http://foo");

    // namespace_for_prefix must find the declared prefix.
    const char* uri = leptris_element_namespace_for_prefix(root, "foo");
    EXPECT_STREQ(uri, "http://foo");

    // An unknown prefix returns NULL.
    EXPECT_EQ(leptris_element_namespace_for_prefix(root, "missing"), nullptr);

    leptris_document_free(doc);
}

TEST(NamespaceReadBug, NamespaceInheritsFromParent) {
    auto doc = Parse(
        "<root xmlns:foo='http://foo'>"
          "<foo:child>"
            "<foo:grandchild/>"
          "</foo:child>"
        "</root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement child = leptris_element_first_child(root, "child");
    ASSERT_NE(child, nullptr);
    LeptrisElement grandchild = leptris_element_first_child(child, "grandchild");
    ASSERT_NE(grandchild, nullptr);

    // grandchild's namespace must resolve via parent lookup chain.
    const char* ns = leptris_element_namespace(grandchild);
    EXPECT_STREQ(ns, "http://foo");
    leptris_document_free(doc);
}

// =====================================================================
// Issue #223 — leptris_node_line
// =====================================================================

/* LEPTRIS_PARSE_DROP_WS_TEXT (pugixml-parity mode): default keeps
 * whitespace-only text nodes (libxml2-faithful, byte round-trips);
 * the flag drops them and starts mixed runs at the first non-ws
 * byte. Both modes pinned. */
TEST(ParseFlags, DropWsTextSemantics) {
    const char pretty[] = "<r>\n  <a/>\n  <b>x</b>\n</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument def = leptris_parse_string(pretty, strlen(pretty), &st);
    ASSERT_NE(def, nullptr);
    LeptrisDocument drop = leptris_parse_string_flags(
        pretty, strlen(pretty), LEPTRIS_PARSE_DROP_WS_TEXT, &st);
    ASSERT_NE(drop, nullptr);

    auto cc = [](LeptrisDocument d) {
        int c = 0;
        LeptrisNodeRef n = leptris_node_first_child(
            leptris_element_as_node(leptris_document_root(d)));
        while (n) { c++; n = leptris_node_next_sibling(n); }
        return c;
    };
    /* default: <a/> + ws + <b/> + ws + <b>'s text "x" ... children of r:
     * ws, a, ws, b, ws = 5 (b's text is b's child) */
    EXPECT_EQ(cc(def), 5);
    /* flagged: ws runs dropped = a, b = 2 */
    EXPECT_EQ(cc(drop), 2);
    /* mixed run "x" inside <b> survives in both */
    LeptrisElement b = leptris_element_first_child(leptris_document_root(drop), "b");
    ASSERT_NE(b, nullptr);
    LeptrisNodeRef bn = leptris_node_first_child(leptris_element_as_node(b));
    ASSERT_NE(bn, nullptr);
    EXPECT_STREQ(leptris_text_node_get_content(bn), "x");

    leptris_document_free(def);
    leptris_document_free(drop);
}

TEST(ParseFlags, DefaultFlagsEqualPlainParse) {
    const char xml[] = "<r>\n  <a/>\n</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument a = leptris_parse_string(xml, strlen(xml), &st);
    LeptrisDocument b = leptris_parse_string_flags(
        xml, strlen(xml), LEPTRIS_PARSE_DEFAULT, &st);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    char* sa = leptris_document_serialize(a, NULL);
    char* sb = leptris_document_serialize(b, NULL);
    ASSERT_NE(sa, nullptr);
    ASSERT_NE(sb, nullptr);
    EXPECT_STREQ(sa, sb);
    free(sa);
    free(sb);
    leptris_document_free(a);
    leptris_document_free(b);
}

/* Regression: the retained-arena free list recycles dirty pages, so
 * every parse-created node MUST explicitly NULL binding_wrapper —
 * pre-retention, fresh mmap pages zeroed it by luck. Parse twice
 * (second parse gets recycled memory) and check every node type. */
TEST(NodeBindingWrapper, NullOnAllParseCreatedNodesAcrossArenaReuse) {
    const char xml[] =
        "<r>text<c/><!--co--><![CDATA[cd]]><?pi data?></r>";
    for (int round = 0; round < 2; round++) {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
        ASSERT_NE(doc, nullptr);
        LeptrisElement r = leptris_document_root(doc);
        ASSERT_NE(r, nullptr);
        LeptrisNodeRef n = leptris_node_first_child(leptris_element_as_node(r));
        int checked = 0;
        while (n) {
            EXPECT_EQ(leptris_node_get_binding_wrapper(n), nullptr)
                << "round " << round << " node " << checked;
            n = leptris_node_next_sibling(n);
            checked++;
        }
        ASSERT_GE(checked, 4);   /* text, element, comment, cdata, pi */
        EXPECT_EQ(leptris_node_get_binding_wrapper(
                      leptris_element_as_node(r)), nullptr);
        leptris_document_free(doc);
    }
}

/* Lazy resolution (parse stores byte offsets; leptris_node_line
 * resolves against the doc's newline table and caches): repeated
 * queries must be stable, resolution order must not matter, and
 * text/comment nodes must resolve through their parent edge. */
TEST(NodeLineBug, LazyResolutionIsIdempotentAndOrderIndependent) {
    auto doc = Parse(
        "<root>\n"
        "  <a>text on line 2</a>\n"
        "  <!-- comment on line 3 -->\n"
        "  <b/>\n"
        "</root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child(root, "a");
    LeptrisElement b = leptris_element_first_child(root, "b");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_EQ(leptris_node_line(leptris_element_as_node(a)), 2);
    EXPECT_EQ(leptris_node_line(leptris_element_as_node(a)), 2);  /* cached */
    EXPECT_EQ(leptris_node_line(leptris_element_as_node(b)), 4);
    EXPECT_EQ(leptris_node_line(leptris_element_as_node(a)), 2);  /* after other */
    EXPECT_EQ(leptris_node_line(leptris_element_as_node(root)), 1);

    /* Text child of <a> shares line 2. */
    LeptrisNodeRef n = leptris_node_first_child(leptris_element_as_node(a));
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(leptris_node_line(n), 2);
    EXPECT_EQ(leptris_node_line(n), 2);
    leptris_document_free(doc);
}

TEST(NodeLineBug, RootReportsLine1) {
    auto doc = Parse("<root>\n  <a/>\n</root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_node_line(leptris_element_as_node(root)), 1);
    leptris_document_free(doc);
}

TEST(NodeLineBug, ChildOnSecondLineReportsTwo) {
    auto doc = Parse("<root>\n  <a/>\n</root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child(root, "a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(leptris_node_line(leptris_element_as_node(a)), 2);
    leptris_document_free(doc);
}

TEST(NodeLineBug, MultilineDocLinesIncrease) {
    auto doc = Parse(
        "<root>\n"
        "  <a/>\n"
        "  <b/>\n"
        "  <c/>\n"
        "</root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child(root, "a");
    LeptrisElement b = leptris_element_first_child(root, "b");
    LeptrisElement c = leptris_element_first_child(root, "c");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_node_line(leptris_element_as_node(a)), 2);
    EXPECT_EQ(leptris_node_line(leptris_element_as_node(b)), 3);
    EXPECT_EQ(leptris_node_line(leptris_element_as_node(c)), 4);
    leptris_document_free(doc);
}

TEST(NodeLineBug, ProgrammaticNodeReportsZero) {
    auto doc = Parse("<root/>");
    LeptrisElement created = leptris_element_create(doc, "fresh");
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(leptris_node_line(leptris_element_as_node(created)), 0);
    leptris_document_free(doc);
}

TEST(NodeLineBug, NullReturnsZero) {
    EXPECT_EQ(leptris_node_line(nullptr), 0);
}

// =====================================================================
// Minor — leptris_element_has_attribute
// =====================================================================

TEST(ElementHasAttribute, Returns1ForPresentAttribute) {
    auto doc = Parse("<r a='1' b='2'/>");
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_element_has_attribute(root, "a"), 1);
    EXPECT_EQ(leptris_element_has_attribute(root, "b"), 1);
    leptris_document_free(doc);
}

TEST(ElementHasAttribute, Returns0ForMissingAttribute) {
    auto doc = Parse("<r a='1'/>");
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_element_has_attribute(root, "missing"), 0);
    leptris_document_free(doc);
}

TEST(ElementHasAttribute, NullArgsReturn0) {
    EXPECT_EQ(leptris_element_has_attribute(nullptr, "a"), 0);
    auto doc = Parse("<r/>");
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_element_has_attribute(root, nullptr), 0);
    leptris_document_free(doc);
}

// =====================================================================
// Minor — leptris_xinclude_get_encoding now exported
// =====================================================================

TEST(XincludeEncodingAccessor, ReturnsNullForNonIncludeElement) {
    auto doc = Parse("<root/>");
    LeptrisElement root = leptris_document_root(doc);
    // Not an xi:include element → NULL.
    EXPECT_EQ(leptris_xinclude_get_encoding(root), nullptr);
    leptris_document_free(doc);
}

TEST(XincludeEncodingAccessor, ReturnsEncodingAttr) {
    auto doc = Parse(
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
          "<xi:include href='a.txt' parse='text' encoding='UTF-8'/>"
        "</root>");
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement xi = leptris_element_first_child(root, "include");
    ASSERT_NE(xi, nullptr);
    const char* enc = leptris_xinclude_get_encoding(xi);
    EXPECT_STREQ(enc, "UTF-8");
    leptris_document_free(doc);
}
