#include <gtest/gtest.h>

#include "../leptris/xpath/collation.h"

#include <cstdlib>
#include <cstring>

namespace {

/* UCA ordering over DUCET, pinned by the canonical examples every
 * implementation must agree on: base letters order the alphabet,
 * accents order within a letter (secondary), case orders within
 * accent (tertiary, uppercase first in DUCET). */
TEST(Collation, UcaBaseAccentCaseOrdering) {
    leptris_collation c;
    ASSERT_EQ(leptris_collation_from_uri(
        "http://www.w3.org/2013/collation/UCA", &c), 0);

    EXPECT_LT(leptris_collation_compare("a", 1, "b", 1, &c), 0);
    EXPECT_GT(leptris_collation_compare("b", 1, "a", 1, &c), 0);
    EXPECT_EQ(leptris_collation_compare("a", 1, "a", 1, &c), 0);

    /* accented forms sort between the base letter and the next
     * letter: a < à < b at full strength. */
    EXPECT_LT(leptris_collation_compare("a", 1, "\xC3\xA0", 2, &c), 0);
    EXPECT_LT(leptris_collation_compare("\xC3\xA0", 2, "b", 1, &c), 0);

    /* tertiary separates case; DUCET's tertiary table orders the
     * lowercase form before the uppercase form (t 0002 vs 0008). */
    EXPECT_GT(leptris_collation_compare("A", 1, "a", 1, &c), 0);
}

TEST(Collation, StrengthParameters) {
    leptris_collation primary;
    ASSERT_EQ(leptris_collation_from_uri(
        "http://www.w3.org/2013/collation/UCA?strength=primary", &primary), 0);
    EXPECT_EQ(primary.strength, LEPTRIS_COLL_STRENGTH_PRIMARY);
    /* primary-strength: accents and case are level 2/3 — ignored. */
    EXPECT_EQ(leptris_collation_compare("a", 1, "A", 1, &primary), 0);
    EXPECT_EQ(leptris_collation_compare("a", 1, "\xC3\xA0", 2, &primary), 0);
    EXPECT_LT(leptris_collation_compare("a", 1, "b", 1, &primary), 0);

    leptris_collation secondary;
    ASSERT_EQ(leptris_collation_from_uri(
        "http://www.w3.org/2013/collation/UCA?strength=secondary", &secondary), 0);
    EXPECT_EQ(secondary.strength, LEPTRIS_COLL_STRENGTH_SECONDARY);
    /* secondary: accents matter, case does not. */
    EXPECT_LT(leptris_collation_compare("a", 1, "\xC3\xA0", 2, &secondary), 0);
    EXPECT_EQ(leptris_collation_compare("a", 1, "A", 1, &secondary), 0);
}

TEST(Collation, DecomposedEquivalence) {
    leptris_collation c;
    ASSERT_EQ(leptris_collation_from_uri(
        "http://www.w3.org/2013/collation/UCA", &c), 0);
    /* NFD of "à" (U+00E0) is a + U+0300; normalization=yes is the
     * F&O default, so composed and decomposed forms are equal. */
    EXPECT_EQ(leptris_collation_compare("\xC3\xA0", 2, "a\xCC\x80", 3, &c), 0);
}

TEST(Collation, SortKeyIsPrefixOrdered) {
    leptris_collation c;
    ASSERT_EQ(leptris_collation_from_uri(
        "http://www.w3.org/2013/collation/UCA", &c), 0);
    unsigned char *ka = NULL, *kab = NULL;
    size_t la = 0, lab = 0;
    ASSERT_EQ(leptris_collation_sortkey("a", 1, &c, &ka, &la), 0);
    ASSERT_EQ(leptris_collation_sortkey("ab", 2, &c, &kab, &lab), 0);
    EXPECT_LT(la, lab);
    /* Keys are prefix-free by construction (level separators are the
     * only 0x00 bytes), so key("a") is NOT a byte-prefix of
     * key("ab") — but it orders strictly before it. */
    size_t n = la < lab ? la : lab;
    EXPECT_LT(memcmp(ka, kab, n), 0);
    free(ka);
    free(kab);
}

TEST(Collation, UriParsing) {
    leptris_collation c;
    /* codepoint + ascii-ci keep working */
    EXPECT_EQ(leptris_collation_from_uri(
        "http://www.w3.org/2005/xpath-functions/collation/codepoint", &c), 0);
    EXPECT_EQ(c.kind, LEPTRIS_COLL_CODEPOINT);
    EXPECT_EQ(leptris_collation_from_uri(
        "http://www.w3.org/2005/xpath-functions/collation/"
        "html-ascii-case-insensitive", &c), 0);
    EXPECT_EQ(c.kind, LEPTRIS_COLL_ASCII_CI);
    /* unknown URI rejected */
    EXPECT_NE(leptris_collation_from_uri("http://example.invalid/coll", &c), 0);
    /* unknown parameter value rejected */
    EXPECT_NE(leptris_collation_from_uri(
        "http://www.w3.org/2013/collation/UCA?strength=ultimate", &c), 0);
    EXPECT_NE(leptris_collation_from_uri(
        "http://www.w3.org/2013/collation/UCA?alternate=sideways", &c), 0);
}

TEST(Collation, CodepointAndAsciiCiCompare) {
    leptris_collation cp, ci;
    ASSERT_EQ(leptris_collation_from_uri(
        "http://www.w3.org/2005/xpath-functions/collation/codepoint", &cp), 0);
    ASSERT_EQ(leptris_collation_from_uri(
        "http://www.w3.org/2005/xpath-functions/collation/"
        "html-ascii-case-insensitive", &ci), 0);
    EXPECT_LT(leptris_collation_compare("A", 1, "a", 1, &cp), 0); /* 0x41 < 0x61 */
    EXPECT_EQ(leptris_collation_compare("A", 1, "a", 1, &ci), 0);
}

}  // namespace
