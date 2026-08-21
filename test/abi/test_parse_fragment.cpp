// test/abi/test_parse_fragment.cpp — Specs for leptris_parse_fragment
// (TODO 148 Phase 4).
//
// Parses XML fragments (multiple top-level nodes allowed) into a
// synthetic container element.

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

TEST(ParseFragment, NullArgsReturnNull) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument dest = Parse("<r/>");
    EXPECT_EQ(leptris_parse_fragment(nullptr, 4, dest, &st), nullptr);
    EXPECT_EQ(leptris_parse_fragment("<a/>", 4, nullptr, &st), nullptr);
    EXPECT_EQ(leptris_parse_fragment("<a/>", 0, dest, &st), nullptr);
    leptris_document_free(dest);
}

TEST(ParseFragment, SingleElementChild) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument dest = Parse("<root/>");
    LeptrisElement frag = leptris_parse_fragment("<a/>", 4, dest, &st);
    ASSERT_NE(frag, nullptr);
    EXPECT_STREQ(leptris_element_name(frag), "#document-fragment");
    EXPECT_EQ(leptris_element_child_count(frag), 1u);
    LeptrisElement a = leptris_element_first_child(frag, "a");
    EXPECT_NE(a, nullptr);
    leptris_document_free(dest);
}

TEST(ParseFragment, MultipleTopLevelElements) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument dest = Parse("<root/>");
    LeptrisElement frag = leptris_parse_fragment("<a/><b/><c/>", 12, dest, &st);
    ASSERT_NE(frag, nullptr);
    EXPECT_EQ(leptris_element_child_count(frag), 3u);
    leptris_document_free(dest);
}

TEST(ParseFragment, MixedContentPreserved) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument dest = Parse("<root/>");
    const char* src = "text<a/><!--c-->";
    LeptrisElement frag = leptris_parse_fragment(src, std::strlen(src), dest, &st);
    ASSERT_NE(frag, nullptr);
    // Walk via node API to verify order: text, element, comment.
    LeptrisNodeRef n = leptris_node_first_child(leptris_element_as_node(frag));
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(leptris_node_get_type(n), 1 /* TEXT */);
    n = leptris_node_next_sibling(n);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(leptris_node_get_type(n), 0 /* ELEMENT */);
    n = leptris_node_next_sibling(n);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(leptris_node_get_type(n), 2 /* COMMENT */);
    leptris_document_free(dest);
}

TEST(ParseFragment, MoveChildrenIntoExistingTree) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument dest = Parse("<root><target/></root>");
    LeptrisElement root = leptris_document_root(dest);
    LeptrisElement target = leptris_element_first_child(root, "target");

    LeptrisElement frag = leptris_parse_fragment("<a/><b/>", 8, dest, &st);
    ASSERT_NE(frag, nullptr);

    // Move all children of frag into target.
    LeptrisNodeRef child = leptris_node_first_child(leptris_element_as_node(frag));
    while (child) {
        LeptrisNodeRef next = leptris_node_next_sibling(child);
        leptris_element_append_child(target, (LeptrisElement)child);
        child = next;
    }
    EXPECT_EQ(leptris_element_child_count(target), 2u);
    EXPECT_EQ(leptris_element_child_count(frag), 0u);

    leptris_document_free(dest);
}

TEST(ParseFragment, NestedSubtreePreserved) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument dest = Parse("<r/>");
    const char* src = "<outer x='1'><inner>text</inner></outer>";
    LeptrisElement frag = leptris_parse_fragment(src, std::strlen(src), dest, &st);
    ASSERT_NE(frag, nullptr);
    LeptrisElement outer = leptris_element_first_child(frag, "outer");
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(leptris_element_attribute_count(outer), 1u);
    EXPECT_STREQ(leptris_element_attribute(outer, "x"), "1");
    LeptrisElement inner = leptris_element_first_child(outer, "inner");
    ASSERT_NE(inner, nullptr);
    EXPECT_STREQ(leptris_element_text(inner), "text");
    leptris_document_free(dest);
}

TEST(ParseFragment, InvalidXmlReturnsNull) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument dest = Parse("<r/>");
    // Unclosed tag.
    LeptrisElement frag = leptris_parse_fragment("<a>", 3, dest, &st);
    EXPECT_EQ(frag, nullptr);
    EXPECT_NE(st, LEPTRIS_OK);
    leptris_document_free(dest);
}
