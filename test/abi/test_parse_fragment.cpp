// test/abi/test_parse_fragment.cpp — Specs for taurus_parse_fragment
// (TODO 148 Phase 4).
//
// Parses XML fragments (multiple top-level nodes allowed) into a
// synthetic container element.

#include <gtest/gtest.h>
#include "taurus.h"
#include <cstring>
#include <string>

namespace {
TaurusDocument Parse(const char* xml) {
    TaurusStatus st = TAURUS_OK;
    return taurus_parse_string(xml, std::strlen(xml), &st);
}
}  // namespace

TEST(ParseFragment, NullArgsReturnNull) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument dest = Parse("<r/>");
    EXPECT_EQ(taurus_parse_fragment(nullptr, 4, dest, &st), nullptr);
    EXPECT_EQ(taurus_parse_fragment("<a/>", 4, nullptr, &st), nullptr);
    EXPECT_EQ(taurus_parse_fragment("<a/>", 0, dest, &st), nullptr);
    taurus_document_free(dest);
}

TEST(ParseFragment, SingleElementChild) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument dest = Parse("<root/>");
    TaurusElement frag = taurus_parse_fragment("<a/>", 4, dest, &st);
    ASSERT_NE(frag, nullptr);
    EXPECT_STREQ(taurus_element_name(frag), "#document-fragment");
    EXPECT_EQ(taurus_element_child_count(frag), 1u);
    TaurusElement a = taurus_element_first_child(frag, "a");
    EXPECT_NE(a, nullptr);
    taurus_document_free(dest);
}

TEST(ParseFragment, MultipleTopLevelElements) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument dest = Parse("<root/>");
    TaurusElement frag = taurus_parse_fragment("<a/><b/><c/>", 12, dest, &st);
    ASSERT_NE(frag, nullptr);
    EXPECT_EQ(taurus_element_child_count(frag), 3u);
    taurus_document_free(dest);
}

TEST(ParseFragment, MixedContentPreserved) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument dest = Parse("<root/>");
    const char* src = "text<a/><!--c-->";
    TaurusElement frag = taurus_parse_fragment(src, std::strlen(src), dest, &st);
    ASSERT_NE(frag, nullptr);
    // Walk via node API to verify order: text, element, comment.
    TaurusNodeRef n = taurus_node_first_child(taurus_element_as_node(frag));
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), 1 /* TEXT */);
    n = taurus_node_next_sibling(n);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), 0 /* ELEMENT */);
    n = taurus_node_next_sibling(n);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), 2 /* COMMENT */);
    taurus_document_free(dest);
}

TEST(ParseFragment, MoveChildrenIntoExistingTree) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument dest = Parse("<root><target/></root>");
    TaurusElement root = taurus_document_root(dest);
    TaurusElement target = taurus_element_first_child(root, "target");

    TaurusElement frag = taurus_parse_fragment("<a/><b/>", 8, dest, &st);
    ASSERT_NE(frag, nullptr);

    // Move all children of frag into target.
    TaurusNodeRef child = taurus_node_first_child(taurus_element_as_node(frag));
    while (child) {
        TaurusNodeRef next = taurus_node_next_sibling(child);
        taurus_element_append_child(target, (TaurusElement)child);
        child = next;
    }
    EXPECT_EQ(taurus_element_child_count(target), 2u);
    EXPECT_EQ(taurus_element_child_count(frag), 0u);

    taurus_document_free(dest);
}

TEST(ParseFragment, NestedSubtreePreserved) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument dest = Parse("<r/>");
    const char* src = "<outer x='1'><inner>text</inner></outer>";
    TaurusElement frag = taurus_parse_fragment(src, std::strlen(src), dest, &st);
    ASSERT_NE(frag, nullptr);
    TaurusElement outer = taurus_element_first_child(frag, "outer");
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(taurus_element_attribute_count(outer), 1u);
    EXPECT_STREQ(taurus_element_attribute(outer, "x"), "1");
    TaurusElement inner = taurus_element_first_child(outer, "inner");
    ASSERT_NE(inner, nullptr);
    EXPECT_STREQ(taurus_element_text(inner), "text");
    taurus_document_free(dest);
}

TEST(ParseFragment, InvalidXmlReturnsNull) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument dest = Parse("<r/>");
    // Unclosed tag.
    TaurusElement frag = taurus_parse_fragment("<a>", 3, dest, &st);
    EXPECT_EQ(frag, nullptr);
    EXPECT_NE(st, TAURUS_OK);
    taurus_document_free(dest);
}
