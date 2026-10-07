// test/parser/test_whitespace_shape.cpp — #1559 cross-platform DOM
// contract for inter-element whitespace.
//
// Same input must yield the same DOM on every platform build
// (x86_64-linux, aarch64-linux, s390x, macOS Intel/Apple Silicon,
// ILP32): the default parse PRESERVES whitespace-only text nodes
// (direct_parse runs with drop_ws_text = 0), and pretty-printed
// documents keep exactly one text child per indentation run.
// Relaton/moxl consumers observe field presence through these
// nodes, so a lane or SIMD path that drops them flips downstream
// joins on one architecture only. These specs pin the shape so any
// diverging build leg goes red on its own runner.

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstring>

namespace {

struct ChildShape {
    int kind;                // LEPTRIS_NODE_TYPE_*
    const char* text;        // expected content when kind == TEXT
};

// Walk root's children in order and assert count + kind + content.
void expect_child_shape(LeptrisDocument doc, const ChildShape* want,
                        size_t n) {
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    LeptrisNodeRef c = leptris_node_first_child(leptris_element_as_node(root));
    for (size_t i = 0; i < n; i++) {
        ASSERT_NE(c, nullptr) << "child " << i << " missing";
        EXPECT_EQ(leptris_node_get_type(c), want[i].kind)
            << "child " << i << " kind";
        if (want[i].kind == (int)LEPTRIS_NODE_TYPE_TEXT)
            EXPECT_STREQ(leptris_text_node_get_content(c), want[i].text)
                << "child " << i << " content";
        c = leptris_node_next_sibling(c);
    }
    EXPECT_EQ(c, nullptr) << "extra children past " << n;
}

}  // namespace

/* The #1559 repro shape: newline + indentation between elements.
 * Every platform must keep all three ws-only text children. */
TEST(WhitespaceShape, PrettyPrintedChildrenPreserved) {
    const char xml[] = "<r>\n  <a/>\n  <b/>\n</r>\n";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    const ChildShape want[] = {
        {(int)LEPTRIS_NODE_TYPE_TEXT, "\n  "},
        {(int)LEPTRIS_NODE_TYPE_ELEMENT, nullptr},
        {(int)LEPTRIS_NODE_TYPE_TEXT, "\n  "},
        {(int)LEPTRIS_NODE_TYPE_ELEMENT, nullptr},
        {(int)LEPTRIS_NODE_TYPE_TEXT, "\n"},
    };
    expect_child_shape(doc, want, 5);
    leptris_document_free(doc);
}

/* Bibdata-shaped: nested pretty printing — each level keeps its
 * indentation runs (relaton's field-presence signal). */
TEST(WhitespaceShape, PrettyPrintedNestedBibitem) {
    const char xml[] =
        "<bibitem>\n"
        "  <title>\n"
        "    <span>Standard</span>\n"
        "  </title>\n"
        "  <extent/>\n"
        "</bibitem>\n";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    const ChildShape bib[] = {
        {(int)LEPTRIS_NODE_TYPE_TEXT, "\n  "},
        {(int)LEPTRIS_NODE_TYPE_ELEMENT, nullptr},
        {(int)LEPTRIS_NODE_TYPE_TEXT, "\n  "},
        {(int)LEPTRIS_NODE_TYPE_ELEMENT, nullptr},
        {(int)LEPTRIS_NODE_TYPE_TEXT, "\n"},
    };
    expect_child_shape(doc, bib, 5);

    /* title: "\n    ", span, "\n  " — mixed content keeps runs. */
    LeptrisNodeRef c = leptris_node_first_child(leptris_element_as_node(root));
    c = leptris_node_next_sibling(c);   /* past leading text */
    ASSERT_EQ(leptris_node_get_type(c), (int)LEPTRIS_NODE_TYPE_ELEMENT);
    LeptrisNodeRef tc = leptris_node_first_child(c);
    ASSERT_NE(tc, nullptr);
    ASSERT_EQ(leptris_node_get_type(tc), (int)LEPTRIS_NODE_TYPE_TEXT);
    EXPECT_STREQ(leptris_text_node_get_content(tc), "\n    ");
    tc = leptris_node_next_sibling(tc);
    ASSERT_EQ(leptris_node_get_type(tc), (int)LEPTRIS_NODE_TYPE_ELEMENT);
    tc = leptris_node_next_sibling(tc);
    ASSERT_NE(tc, nullptr);
    EXPECT_EQ(leptris_node_get_type(tc), (int)LEPTRIS_NODE_TYPE_TEXT);
    EXPECT_STREQ(leptris_text_node_get_content(tc), "\n  ");
    EXPECT_EQ(leptris_node_next_sibling(tc), nullptr);

    /* extent: no children at all (two siblings past title:
     * text("\n  ") then extent). */
    c = leptris_node_next_sibling(leptris_node_next_sibling(c));
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(leptris_node_get_type(c), (int)LEPTRIS_NODE_TYPE_ELEMENT);
    EXPECT_EQ(leptris_node_first_child(c), nullptr);
    leptris_document_free(doc);
}

/* The collapsed spelling the issue used as the control: no
 * whitespace, no text children — every platform. */
TEST(WhitespaceShape, CollapsedChildrenHaveNoTextNodes) {
    const char xml[] = "<r><a/><b/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    const ChildShape want[] = {
        {(int)LEPTRIS_NODE_TYPE_ELEMENT, nullptr},
        {(int)LEPTRIS_NODE_TYPE_ELEMENT, nullptr},
    };
    expect_child_shape(doc, want, 2);
    leptris_document_free(doc);
}

/* Whitespace INSIDE an element is content, not inter-element —
 * never eligible for the drop flag's fast path. */
TEST(WhitespaceShape, SoleChildWhitespaceIsContent) {
    const char xml[] = "<p>\n  </p>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    const ChildShape want[] = {
        {(int)LEPTRIS_NODE_TYPE_TEXT, "\n  "},
    };
    expect_child_shape(doc, want, 1);
    leptris_document_free(doc);
}

/* CRLF runs normalize to LF (XML 1.0 §2.11 line-ending
 * normalization) — same DOM on every platform. */
TEST(WhitespaceShape, CrlfRunsPreserved) {
    const char xml[] = "<r>\r\n  <a/>\r\n</r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    const ChildShape want[] = {
        {(int)LEPTRIS_NODE_TYPE_TEXT, "\n  "},
        {(int)LEPTRIS_NODE_TYPE_ELEMENT, nullptr},
        {(int)LEPTRIS_NODE_TYPE_TEXT, "\n"},
    };
    expect_child_shape(doc, want, 3);
    leptris_document_free(doc);
}
