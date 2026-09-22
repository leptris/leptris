// test/dom/test_text_borrowed.cpp — TODO 115 Phase B/C: borrowed text nodes.
//
// Verifies that the parser hands text nodes a non-owning view into the
// document's writable input buffer (no per-node pool allocation for
// content), that consumers see correct content despite a possibly
// missing NUL terminator, and that leptris_text_get_content produces a
// stable NUL-terminated view.
//
// #1299: the get_content serve mode is lane-dependent. When the parser
// terminated the run in place (HTML builder, interleaved XML lane),
// get_content serves the doc-owned pointer directly — no allocation,
// the borrowed flag stays 1. Otherwise (classic XML lane, deferred
// NUL) it materializes a pool-owned copy and flips the flag to 0.
// Both modes must uphold the documented contract: correct bytes,
// NUL-terminated view, pointer-stable across calls.

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <cstdlib>
#include "leptris.h"

extern "C" {
/* Internal headers — required to inspect the borrowed flag / content_len. */
#include "node.h"
#include "text.h"
}

TEST(TextBorrowed, ParsedTextNodeIsBorrowedFromInputBuffer) {
    const char xml[] = "<r>hello world</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    LeptrisNodeRef child = leptris_node_first_child(leptris_element_as_node(root));
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(child->type, LEPTRIS_NODE_TYPE_TEXT);

    LeptrisTextNode* text = (LeptrisTextNode*)child;
    EXPECT_EQ(text->borrowed, 1) << "text node should be borrowed from xml_buffer";
    EXPECT_EQ(text->content_len, std::strlen("hello world"));
    /* The borrowed pointer does NOT point at the stack-local caller
     * buffer: leptris_parse_string copies the input into doc->xml_buffer
     * (lifetime = document's) and the borrowed view lands inside that
     * copy. Just verify the pointer is non-NULL — the address range
     * itself is internal to the document. */
    EXPECT_NE(text->content, nullptr);

    /* Serve mode is lane-dependent (#1299): a run terminated in place
     * (HTML builder, interleaved lane) is served directly — the
     * returned pointer IS the borrowed pointer and the flag stays 1;
     * the classic lane materializes a fresh pool copy and flips to 0.
     * Either way the view must be correct and pointer-stable. */
    const char* first = leptris_text_get_content(text);
    EXPECT_STREQ(first, "hello world");
    EXPECT_EQ(leptris_text_get_content(text), first)
        << "served view must be pointer-stable across calls";

    leptris_document_free(doc);
}

TEST(TextBorrowed, PublicAccessorsReturnCorrectContent) {
    const char xml[] = "<r>hello world</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_STREQ(leptris_element_text(root), "hello world");

    LeptrisNodeRef child = leptris_node_first_child(leptris_element_as_node(root));
    EXPECT_STREQ(leptris_text_node_get_content(child), "hello world");

    leptris_document_free(doc);
}

TEST(TextBorrowed, RoundTripsThroughSerialize) {
    const char xml[] = "<r>hello &amp; world</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = leptris_document_serialize(doc, NULL);
    ASSERT_NE(serialized, nullptr);
    EXPECT_NE(std::string(serialized).find("hello &amp; world"), std::string::npos);
    leptris_free(serialized);

    leptris_document_free(doc);
}

TEST(TextBorrowed, EntityTextIsExpandedOnAccess) {
    /* Text with entities is stored borrowed on the fast parse path
     * and expanded lazily when leptris_text_get_content reads it.
     * This keeps entity-containing inputs on the zero-copy parse
     * path instead of forcing legacy-parser fallback. */
    const char xml[] = "<r>a&amp;b</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisNodeRef child = leptris_node_first_child(leptris_element_as_node(root));
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(child->type, LEPTRIS_NODE_TYPE_TEXT);

    LeptrisTextNode* text = (LeptrisTextNode*)child;
    const char* content = leptris_text_get_content(text);
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "a&b");

    leptris_document_free(doc);
}

TEST(TextBorrowed, MaterializationIsStableAcrossCalls) {
    /* After the first leptris_element_text call materializes a
     * NUL-terminated copy, the borrowed flag flips to 0 and subsequent
     * calls return the same pointer (no re-materialization). */
    const char xml[] = "<r>xyz</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    const char* first = leptris_element_text(root);
    ASSERT_STREQ(first, "xyz");

    LeptrisNodeRef child = leptris_node_first_child(leptris_element_as_node(root));
    LeptrisTextNode* text = (LeptrisTextNode*)child;
    /* The flip is a materialize-path side effect, not the contract
     * (#1299): in-place serve keeps borrowed at 1 — the mode is
     * owned by ServeContractHoldsInBothParseLanes below. */

    const char* second = leptris_element_text(root);
    EXPECT_EQ(second, first) << "repeated calls return the same pointer";

    leptris_document_free(doc);
}

/* #1299: both serve modes uphold the get_content contract for parsed
 * text. The interleaved lane is exercised with the env gate forced
 * in-process (same toggle discipline as test/parser/test_interleaved.cpp;
 * empty-string values count as off for MSVC). */
#ifdef _WIN32
static void tb_set_env(const char* k, const char* v) { _putenv_s(k, v); }
static void tb_unset_env(const char* k) { _putenv_s(k, ""); }
#else
static void tb_set_env(const char* k, const char* v) { setenv(k, v, 1); }
static void tb_unset_env(const char* k) { unsetenv(k); }
#endif

TEST(TextBorrowed, ServeContractHoldsInBothParseLanes) {
    const char* xml = "<r>hello world</r>";
    for (int lane = 0; lane <= 1; lane++) {
        SCOPED_TRACE(lane ? "interleaved" : "classic");
        if (lane) tb_set_env("LEPTRIS_INTERLEAVED", "1");
        else tb_unset_env("LEPTRIS_INTERLEAVED");
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
        ASSERT_NE(doc, nullptr);
        LeptrisElement root = leptris_document_root(doc);
        ASSERT_NE(root, nullptr);
        LeptrisNodeRef child =
            leptris_node_first_child(leptris_element_as_node(root));
        ASSERT_NE(child, nullptr);
        ASSERT_EQ(child->type, LEPTRIS_NODE_TYPE_TEXT);

        LeptrisTextNode* text = (LeptrisTextNode*)child;
        /* non-owning in both lanes */
        EXPECT_EQ(text->borrowed, 1);
        EXPECT_EQ(text->content_len, std::strlen("hello world"));

        const char* a = leptris_text_get_content(text);
        ASSERT_NE(a, nullptr);
        EXPECT_STREQ(a, "hello world");
        EXPECT_EQ(leptris_text_get_content(text), a)
            << "served view must be pointer-stable across calls";
        /* still correct after the first access */
        EXPECT_STREQ(leptris_element_text(root), "hello world");

        leptris_document_free(doc);
    }
    tb_unset_env("LEPTRIS_INTERLEAVED");
}
