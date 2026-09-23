// test/dom/test_text_borrowed.cpp — text node storage invariants.
//
// History: TODO 115 Phase B introduced borrowed (non-owning) text
// content; #1299 documented lane-dependent get_content serve modes.
// #1285 slice 4 COLLAPSED the model: content is ALWAYS NUL-terminated
// (every parse lane terminates the run in the doc-owned buffer before
// carving the node), the pool/borrowed fields are gone (node 64 -> 48
// bytes), and leptris_text_get_content is a plain field read. These
// tests now pin the slice-4 invariants.

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

TEST(TextBorrowed, ParsedTextNodeIsTerminatedInPlace) {
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
    EXPECT_EQ(text->content_len, std::strlen("hello world"));
    /* #1285 slice 4: the run is NUL-terminated IN PLACE in the
     * doc-owned input copy — no pool copy, no serve mode. */
    ASSERT_NE(text->content, nullptr);
    EXPECT_EQ(text->content[text->content_len], '\0')
        << "run must be terminated at [content_len]";
    /* get_content is a plain field read: same pointer, stable. */
    EXPECT_EQ(leptris_text_get_content(text), text->content);
    EXPECT_STREQ(leptris_text_get_content(text), "hello world");

    leptris_document_free(doc);
}

TEST(TextBorrowed, NodeStructIsBasePointerAndThreeInt32) {
    /* #1285 slice 4: pool + borrowed removed, content_len is uint32.
     * The width-independent invariant: base + ONE content pointer +
     * three int32s (len, next, parent), rounded to pointer alignment
     * — nothing else. 64 -> 48 on LP64; 32 on ILP32. The issue's
     * 32B sketch assumed a 12-byte base and a 64-bit host; going
     * lower needs content as an int32 pool offset (slice 4b). The
     * parse-time bulk strides (dp text block, il lane table) hardcode
     * this layout economics — a silent field growth would erode the
     * #1222 parse win this slice exists for. */
    /* C++11: no constexpr lambda — inline the round-ups. The
     * content pointer needs pointer alignment INSIDE the struct:
     * base rounds up to the pointer's alignment first. */
    constexpr size_t kA = alignof(void*);
    constexpr size_t kBaseA =
        (sizeof(LeptrisNode) + kA - 1) / kA * kA;
    constexpr size_t kRaw = kBaseA + sizeof(void*) + 3 * sizeof(int32_t);
    constexpr size_t kAligned = (kRaw + kA - 1) / kA * kA;
    static_assert(sizeof(LeptrisTextNode) == kAligned,
                  "LeptrisTextNode must be base + one content pointer + "
                  "uint32 len + int32 next + int32 parent (aligned) — "
                  "no pool/borrowed fields (#1285 slice 4)");
    EXPECT_EQ(sizeof(LeptrisTextNode), kAligned);
#if defined(__LP64__) || defined(_WIN64)
    EXPECT_EQ(sizeof(LeptrisTextNode), (size_t)40);
#else
    EXPECT_EQ(sizeof(LeptrisTextNode), (size_t)28);
#endif
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
    EXPECT_EQ(leptris_text_get_content(text), text->content)
        << "get_content is a plain field read (#1285 slice 4)";

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
        EXPECT_EQ(text->content_len, std::strlen("hello world"));
        /* terminated in place in BOTH lanes (#1285 slice 4) */
        ASSERT_NE(text->content, nullptr);
        EXPECT_EQ(text->content[text->content_len], '\0');

        const char* a = leptris_text_get_content(text);
        ASSERT_NE(a, nullptr);
        EXPECT_STREQ(a, "hello world");
        EXPECT_EQ(a, text->content)
            << "get_content returns the in-place pointer in both lanes";
        EXPECT_STREQ(leptris_element_text(root), "hello world");

        leptris_document_free(doc);
    }
    tb_unset_env("LEPTRIS_INTERLEAVED");
}
