// test/flat/test_flat_fast.cpp — Phase E fast-path specs (TODO 139).
//
// Verifies the flat-mode query helpers return the same answers as
// the promote-then-walk path:
//   - flat_fast_count_elements_all matches the actual element count
//   - flat_fast_count_elements_named matches XPath count(//name)
//   - flat_fast_root_name matches taurus_element_name(root)
//
// Also verifies the helpers gracefully handle:
//   - Already-promoted documents (return 0/NULL)
//   - Documents produced via the legacy parser
//   - Empty/malformed documents

#include <gtest/gtest.h>

extern "C" {
#include "taurus.h"
#include "flat_doc.h"
#include "flat_fast.h"
}

#include <cstring>
#include <string>

namespace {

TaurusDocument Parse(const char* xml) {
    TaurusStatus st = TAURUS_OK;
    return taurus_parse_string(xml, std::strlen(xml), &st);
}

TEST(FlatFast, CountElementsAllMatchesActual) {
    // 6 element nodes: catalog + 5 items.
    const char xml[] =
        "<catalog>"
        "  <item/><item/><item/>"
        "  <nested><deep/></nested>"
        "</catalog>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(flat_fast_count_elements_all(doc), 6u);
    taurus_document_free(doc);
}

TEST(FlatFast, CountElementsNamedMatchesXPath) {
    const char xml[] =
        "<r>"
        "  <book id='1'/>"
        "  <book id='2'/>"
        "  <book id='3'/>"
        "  <other/>"
        "</r>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);

    /* Phase E fast path doesn't promote. */
    size_t fast_count = flat_fast_count_elements_named(doc, "book");
    EXPECT_EQ(fast_count, 3u);

    /* Cross-check with XPath count(//book) -- this WILL promote. */
    TaurusElement root = taurus_document_root(doc);
    TaurusXPathResult r = taurus_xpath_eval(doc, root, "count(//book)");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 3.0);
    taurus_xpath_result_free(r);

    /* Fast path no longer works after promote. */
    EXPECT_EQ(flat_fast_count_elements_named(doc, "book"), 0u);

    taurus_document_free(doc);
}

TEST(FlatFast, RootNameMatchesElementName) {
    const char xml[] = "<custom_root><child/></custom_root>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);

    const char* fast_name = flat_fast_root_name(doc);
    ASSERT_NE(fast_name, nullptr);
    /* The fast path returns a non-NUL-terminated view; compare
     * against the expected length. */
    EXPECT_EQ(memcmp(fast_name, "custom_root", 11), 0);

    /* Cross-check via the promote path. */
    TaurusElement root = taurus_document_root(doc);
    EXPECT_STREQ(taurus_element_name(root), "custom_root");

    /* Fast path returns NULL after promote. */
    EXPECT_EQ(flat_fast_root_name(doc), nullptr);

    taurus_document_free(doc);
}

TEST(FlatFast, ReturnsZeroAfterPromote) {
    /* All fast paths must return 0/NULL once the doc has been
     * promoted -- the FlatDoc is freed during promote. */
    const char xml[] = "<r><a/><b/></r>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);

    /* Force promote. */
    (void)taurus_document_root(doc);

    EXPECT_EQ(flat_fast_count_elements_all(doc), 0u);
    EXPECT_EQ(flat_fast_count_elements_named(doc, "r"), 0u);
    EXPECT_EQ(flat_fast_root_name(doc), nullptr);

    taurus_document_free(doc);
}

TEST(FlatFast, ReturnsZeroForLegacyParsedDoc) {
    /* Input with "&amp;" routes through the legacy parser, so
     * doc->flat_doc is NULL from the start. */
    const char xml[] = "<r><a>x&amp;y</a></r>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);

    EXPECT_EQ(flat_fast_count_elements_all(doc), 0u);
    EXPECT_EQ(flat_fast_count_elements_named(doc, "a"), 0u);
    EXPECT_EQ(flat_fast_root_name(doc), nullptr);

    taurus_document_free(doc);
}

TEST(FlatFast, NullNameReturnsZero) {
    TaurusDocument doc = Parse("<r/>");
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(flat_fast_count_elements_named(doc, nullptr), 0u);
    taurus_document_free(doc);
}

TEST(FlatFast, NullDocIsSafe) {
    EXPECT_EQ(flat_fast_count_elements_all(nullptr), 0u);
    EXPECT_EQ(flat_fast_count_elements_named(nullptr, "x"), 0u);
    EXPECT_EQ(flat_fast_root_name(nullptr), nullptr);
}

TEST(FlatFast, CountElementsNamedFindsNested) {
    /* Nested elements with the same name -- the flat walk counts
     * ALL of them, not just direct children. */
    const char xml[] =
        "<a><b><b><b/></b></b></a>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(flat_fast_count_elements_named(doc, "a"), 1u);
    EXPECT_EQ(flat_fast_count_elements_named(doc, "b"), 3u);
    taurus_document_free(doc);
}

TEST(FlatFast, CountElementsNamedSkipsNonMatching) {
    const char xml[] = "<r><foo/><bar/><baz/></r>";
    TaurusDocument doc = Parse(xml);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(flat_fast_count_elements_named(doc, "qux"), 0u);
    EXPECT_EQ(flat_fast_count_elements_named(doc, "foo"), 1u);
    EXPECT_EQ(flat_fast_count_elements_named(doc, "bar"), 1u);
    taurus_document_free(doc);
}

}  // namespace
