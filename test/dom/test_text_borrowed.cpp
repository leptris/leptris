// test/dom/test_text_borrowed.cpp — TODO 115 Phase B/C: borrowed text nodes.
//
// Verifies that the parser hands text nodes a non-owning view into the
// document's writable input buffer (no per-node pool allocation for
// content), that consumers see correct content despite the missing
// NUL terminator, and that lazy materialization produces a stable
// NUL-terminated view.

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "taurus.h"

extern "C" {
/* Internal headers — required to inspect the borrowed flag / content_len. */
#include "node.h"
#include "text.h"
}

TEST(TextBorrowed, ParsedTextNodeIsBorrowedFromInputBuffer) {
    const char xml[] = "<r>hello world</r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);

    TaurusNodeRef child = taurus_node_first_child(taurus_element_as_node(root));
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(child->type, TAURUS_NODE_TYPE_TEXT);

    TaurusTextNode* text = (TaurusTextNode*)child;
    EXPECT_EQ(text->borrowed, 1) << "text node should be borrowed from xml_buffer";
    EXPECT_EQ(text->content_len, std::strlen("hello world"));
    /* The borrowed pointer does NOT point at the stack-local caller
     * buffer: taurus_parse_string copies the input into doc->xml_buffer
     * (lifetime = document's) and the borrowed view lands inside that
     * copy. Just verify the pointer is non-NULL — the address range
     * itself is internal to the document. */
    EXPECT_NE(text->content, nullptr);

    /* The materialized pointer (after taurus_text_get_content) is
     * pool-resident and therefore different from the borrowed pointer. */
    const char* borrowed_ptr = text->content;
    const char* materialized_ptr = taurus_text_get_content(text);
    EXPECT_NE(materialized_ptr, borrowed_ptr)
        << "materialization must allocate a fresh NUL-terminated copy";
    EXPECT_EQ(text->borrowed, 0) << "get_content flips the borrowed flag";

    taurus_document_free(doc);
}

TEST(TextBorrowed, PublicAccessorsReturnCorrectContent) {
    const char xml[] = "<r>hello world</r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    EXPECT_STREQ(taurus_element_text(root), "hello world");

    TaurusNodeRef child = taurus_node_first_child(taurus_element_as_node(root));
    EXPECT_STREQ(taurus_text_node_get_content(child), "hello world");

    taurus_document_free(doc);
}

TEST(TextBorrowed, RoundTripsThroughSerialize) {
    const char xml[] = "<r>hello &amp; world</r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = taurus_document_serialize(doc, NULL);
    ASSERT_NE(serialized, nullptr);
    EXPECT_NE(std::string(serialized).find("hello &amp; world"), std::string::npos);
    taurus_free(serialized);

    taurus_document_free(doc);
}

TEST(TextBorrowed, EntityTextIsExpandedOnAccess) {
    /* Text with entities is stored borrowed on the fast parse path
     * and expanded lazily when taurus_text_get_content reads it.
     * This keeps entity-containing inputs on the zero-copy parse
     * path instead of forcing legacy-parser fallback. */
    const char xml[] = "<r>a&amp;b</r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef child = taurus_node_first_child(taurus_element_as_node(root));
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(child->type, TAURUS_NODE_TYPE_TEXT);

    TaurusTextNode* text = (TaurusTextNode*)child;
    const char* content = taurus_text_get_content(text);
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "a&b");

    taurus_document_free(doc);
}

TEST(TextBorrowed, MaterializationIsStableAcrossCalls) {
    /* After the first taurus_element_text call materializes a
     * NUL-terminated copy, the borrowed flag flips to 0 and subsequent
     * calls return the same pointer (no re-materialization). */
    const char xml[] = "<r>xyz</r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    const char* first = taurus_element_text(root);
    ASSERT_STREQ(first, "xyz");

    TaurusNodeRef child = taurus_node_first_child(taurus_element_as_node(root));
    TaurusTextNode* text = (TaurusTextNode*)child;
    EXPECT_EQ(text->borrowed, 0) << "materialization should flip the borrowed flag";

    const char* second = taurus_element_text(root);
    EXPECT_EQ(second, first) << "repeated calls return the same pointer";

    taurus_document_free(doc);
}
