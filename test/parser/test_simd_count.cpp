#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
void leptris_text_count3(const char* s, size_t len, char c0, char c1,
                         char c2, size_t* n0, size_t* n1, size_t* n2);
void leptris_copy_count3(char* dst, const char* src, size_t len,
                         char c0, char c1, char c2,
                         size_t* n0, size_t* n1, size_t* n2);
}

/* The count3 kernels size the parse arena from these counts — a
 * miscount under-sizes the arena and exhausts mid-parse (the CI
 * catch on record). Exactness at every length, including the SIMD
 * body / scalar-tail boundaries and saturation-scale densities, is
 * the contract. */
namespace {

void expect_counts(const char* s, size_t len) {
    size_t e0 = 0, e1 = 0, e2 = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '<') e0++;
        if (s[i] == '"') e1++;
        if (s[i] == '\'') e2++;
    }
    size_t a = 1, b = 1, c = 1;
    leptris_text_count3(s, len, '<', '"', '\'', &a, &b, &c);
    EXPECT_EQ(a, e0) << "text_count3 c0 len=" << len;
    EXPECT_EQ(b, e1) << "text_count3 c1 len=" << len;
    EXPECT_EQ(c, e2) << "text_count3 c2 len=" << len;

    std::vector<char> dst(len + 64, 0xAA);
    a = b = c = 1;
    leptris_copy_count3(dst.data(), s, len, '<', '"', '\'', &a, &b, &c);
    EXPECT_EQ(a, e0) << "copy_count3 c0 len=" << len;
    EXPECT_EQ(b, e1) << "copy_count3 c1 len=" << len;
    EXPECT_EQ(c, e2) << "copy_count3 c2 len=" << len;
    EXPECT_EQ(0, memcmp(dst.data(), s, len)) << "copy_count3 copy len=" << len;
}

}  // namespace

TEST(Count3Kernels, ExactAtEveryLengthUpToTailBoundaries) {
    /* PRNG-free deterministic pattern with all three classes. */
    std::string pattern = "<a b=\"c\" d='e' --> &amp;\n\t";
    for (size_t len = 0; len <= 128; len++) {
        std::string s;
        for (size_t i = 0; i < len; i++) s += pattern[i % pattern.size()];
        expect_counts(s.data(), s.size());
    }
}

TEST(Count3Kernels, AllMatchDenseInputs) {
    /* Saturation hazard: every byte matches one class. */
    std::string all_lt(5000, '<');
    expect_counts(all_lt.data(), all_lt.size());
    std::string all_dq(5000, '"');
    expect_counts(all_dq.data(), all_dq.size());
    std::string all_sq(70000, '\'');  /* > u16 lane lifetime */
    expect_counts(all_sq.data(), all_sq.size());
}

TEST(Count3Kernels, LargeMixedInput) {
    std::string pattern = "<e a1=\"v\" a2='w'/>\n<!-- < -- ' \" -->";
    std::string s;
    for (int i = 0; i < 3000; i++) s += pattern;
    expect_counts(s.data(), s.size());
}

/* count3_high — the #1842 fused sizing+encoding pre-scan: the same
 * three counts as text_count3 plus whether ANY byte >= 0x80 exists.
 * Pure-ASCII input is valid UTF-8 by construction, so the parser's
 * separate SWAR validation pass disappears for the common case.
 * Counts must match text_count3 EXACTLY at every length (the same
 * arena-sizing contract), including across the SIMD body / scalar
 * tail boundary and with high bytes straddling it. */
extern "C" {
void leptris_text_count3_high(const char* s, size_t len, char c0,
                              char c1, char c2, size_t* n0, size_t* n1,
                              size_t* n2, int* saw_high);
}

namespace {

void expect_high(const char* s, size_t len) {
    size_t e0 = 0, e1 = 0, e2 = 0, ehi = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '<') e0++;
        if (s[i] == '"') e1++;
        if (s[i] == '\'') e2++;
        if ((unsigned char)s[i] >= 0x80) ehi++;
    }
    size_t a = 1, b = 1, c = 1;
    int hi = -1;
    leptris_text_count3_high(s, len, '<', '"', '\'', &a, &b, &c, &hi);
    EXPECT_EQ(a, e0) << "count3_high c0 len=" << len;
    EXPECT_EQ(b, e1) << "count3_high c1 len=" << len;
    EXPECT_EQ(c, e2) << "count3_high c2 len=" << len;
    EXPECT_EQ(hi, ehi > 0 ? 1 : 0) << "count3_high saw_high len=" << len;
    // cross-check against the plain kernel
    size_t p0 = 0, p1 = 0, p2 = 0;
    leptris_text_count3(s, len, '<', '"', '\'', &p0, &p1, &p2);
    EXPECT_EQ(a, p0);
    EXPECT_EQ(b, p1);
    EXPECT_EQ(c, p2);
}

TEST(SimdCountHigh, AsciiIsHighFree) {
    const char s[] = "<root a=\"1\" b='2'>text</root>";
    expect_high(s, sizeof(s) - 1);
}

TEST(SimdCountHigh, Utf8BytesDetected) {
    const char s[] = "<r a=\"é\">naïve — text</r>";
    expect_high(s, sizeof(s) - 1);
}

TEST(SimdCountHigh, HighByteAtEveryBoundary) {
    // a 2-byte UTF-8 char walked across every position relative to
    // the 16-byte SIMD body and the 3-char tail loop
    std::string base = "<x a='1' b=\"2\">t</x>";
    for (size_t pad = 0; pad < 40; pad++) {
        std::string s(pad, 'y');
        s += "<é>";
        expect_high(s.data(), s.size());
    }
}

TEST(SimdCountHigh, EmptyAndTiny) {
    expect_high("", 0);
    const char* t = "<";
    expect_high(t, 1);
    const char* h = "\xc3\xa9";
    expect_high(h, 2);
}
}  // namespace
