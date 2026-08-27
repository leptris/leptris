/* test/sax/test_iterparse.cpp — iterparse v2 (issue #586).
 *
 * v1 yields only top-level children of the root, hands out QNames
 * with no namespace context, and swallows truncation errors. The
 * three v2 capabilities: a full-document mode (every element, in
 * completion order, ephemeral handles), namespace-resolved iteration
 * (in-scope prefix → URI bindings captured at parse time, usable on
 * yielded elements and via iterator snapshot accessors), and an
 * error channel for malformed/truncated input. */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
#include "leptris/sax/sax.h"
}
#include <cstring>
#include <string>
#include <vector>

namespace {

TEST(IterparseV2, FullModeYieldsEveryElementInCompletionOrder) {
    const char xml[] = "<r><a><b/></a><c/></r>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    /* post-order completion: b, a, c, then the root r itself */
    const char* expect[] = {"b", "a", "c", "r"};
    for (const char* name : expect) {
        LeptrisElement e = leptris_iterparse_next(it);
        ASSERT_NE(e, nullptr) << "expected " << name;
        EXPECT_STREQ(leptris_element_name(e), name);
    }
    EXPECT_EQ(leptris_iterparse_next(it), nullptr);
    leptris_iterparse_free(it);
}

TEST(IterparseV2, FullModeYieldedElementCarriesItsSubtree) {
    const char xml[] = "<r><a><b>text</b></a></r>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    LeptrisElement b = leptris_iterparse_next(it);
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(leptris_element_name(b), "b");
    LeptrisElement a = leptris_iterparse_next(it);
    ASSERT_NE(a, nullptr);
    /* b was released with the advance; a is yielded with the b child
     * still attached (single-pool subtree, lxml END semantics). */
    EXPECT_STREQ(leptris_element_name(a), "a");
    EXPECT_EQ(leptris_element_child_count(a), 1u);
    leptris_iterparse_free(it);
}

TEST(IterparseV2, TopLevelModeUnchangedByV2) {
    const char xml[] = "<r><a/><b/></r>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_TOP_LEVEL);
    ASSERT_NE(it, nullptr);
    LeptrisElement e = leptris_iterparse_next(it);
    ASSERT_NE(e, nullptr);
    EXPECT_STREQ(leptris_element_name(e), "a");
    EXPECT_EQ(leptris_iterparse_next(it) != nullptr, true);  /* b */
    EXPECT_EQ(leptris_iterparse_next(it), nullptr);
    leptris_iterparse_free(it);
}

TEST(IterparseV2, NamespacesResolveOnYieldedElements) {
    const char xml[] =
        "<root xmlns:p=\"urn:p\" xmlns=\"urn:d\">"
        "<p:child xmlns:q=\"urn:q\" q:attr=\"v\"/>"
        "</root>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    LeptrisElement child = leptris_iterparse_next(it);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(leptris_element_name(child), "child");
    LeptrisNamespace ns = leptris_element_namespace(child);
    ASSERT_NE(ns, nullptr);
    const char* uri = leptris_namespace_uri(ns);
    ASSERT_NE(uri, nullptr);
    EXPECT_STREQ(uri, "urn:p");
    leptris_iterparse_free(it);
}

TEST(IterparseV2, InScopeSnapshotAnswersPrefixes) {
    const char xml[] =
        "<r xmlns:p=\"urn:p\" xmlns=\"urn:d\">"
        "<p:a><p:b xmlns:q=\"urn:q\"/></p:a>"
        "</r>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    /* post-order: p:b first — its scope: p → urn:p, q → urn:q,
     * default → urn:d. */
    LeptrisElement b = leptris_iterparse_next(it);
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(leptris_iterparse_ns_uri(it, "p"), "urn:p");
    EXPECT_STREQ(leptris_iterparse_ns_uri(it, "q"), "urn:q");
    EXPECT_STREQ(leptris_iterparse_ns_uri(it, nullptr), "urn:d");
    EXPECT_EQ(leptris_iterparse_ns_uri(it, "zzz"), nullptr);
    /* bulk form for FFI hosts */
    size_t n = leptris_iterparse_ns_count(it);
    EXPECT_EQ(n, 3u);
    leptris_iterparse_free(it);
}

TEST(IterparseV2, ScopeUnwindsOnElementEnd) {
    const char xml[] =
        "<r xmlns:p=\"urn:p\"><a><p:b/></a><p:c/></r>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    /* post-order: p:b, a, p:c, r */
    ASSERT_NE(leptris_iterparse_next(it), nullptr);   /* p:b */
    ASSERT_NE(leptris_iterparse_next(it), nullptr);   /* a  */
    LeptrisElement c = leptris_iterparse_next(it);    /* p:c */
    ASSERT_NE(c, nullptr);
    /* still inside r's scope */
    EXPECT_STREQ(leptris_iterparse_ns_uri(it, "p"), "urn:p");
    leptris_iterparse_free(it);
}

TEST(IterparseV2, TruncatedInputReportsError) {
    const char xml[] = "<root><child>text";   /* truncated */
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_TOP_LEVEL);
    ASSERT_NE(it, nullptr);
    /* v1: silent NULL forever. v2: NULL + a retrievable message. */
    EXPECT_EQ(leptris_iterparse_next(it), nullptr);
    const char* err = leptris_iterparse_error(it);
    ASSERT_NE(err, nullptr);
    EXPECT_GT(strlen(err), 0u);
    leptris_iterparse_free(it);
}

TEST(IterparseV2, WellFormedInputHasNoError) {
    const char xml[] = "<r><a/></r>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_TOP_LEVEL);
    ASSERT_NE(it, nullptr);
    ASSERT_NE(leptris_iterparse_next(it), nullptr);
    EXPECT_EQ(leptris_iterparse_next(it), nullptr);
    EXPECT_EQ(leptris_iterparse_error(it), nullptr);
    leptris_iterparse_free(it);
}

TEST(IterparseV2, FileVariantSupportsFullMode) {
    /* gtest's TempDir is portable (no unistd.h/mkstemp on Win32). */
    std::string path = std::string(testing::TempDir()) +
                       "leptris_iterparse_full.xml";
    FILE* f = fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fputs("<r><a><b/></a></r>", f);
    fclose(f);
    LeptrisIterparse it = leptris_iterparse_new_file_ex(
        path.c_str(), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    const char* expect[] = {"b", "a", "r"};
    for (const char* name : expect) {
        LeptrisElement e = leptris_iterparse_next(it);
        ASSERT_NE(e, nullptr);
        EXPECT_STREQ(leptris_element_name(e), name);
    }
    EXPECT_EQ(leptris_iterparse_next(it), nullptr);
    leptris_iterparse_free(it);
    remove(path.c_str());
}

}  // namespace
