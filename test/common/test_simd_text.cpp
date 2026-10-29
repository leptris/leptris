// test/common/test_simd_text.cpp — AOT SIMD text scan specs (TODO 175).
//
// Exercises leptris_text_contains / find / find3 across lengths 0..80
// (straddling the 16/32-byte SIMD chunk boundaries and the scalar
// fallback thresholds), plus the CPU-level detection.

#include <gtest/gtest.h>

extern "C" {
#include "simd_text.h"
#include "cpu.h"
}

#include <string>

namespace {

TEST(SimdText, CpuDetectReturnsValidLevel) {
    leptris_cpu_level lvl = leptris_cpu_detect();
    EXPECT_GE(lvl, LEPTRIS_CPU_SCALAR);
    EXPECT_LE(lvl, LEPTRIS_CPU_NEON);
    EXPECT_STRNE(leptris_cpu_level_name(lvl), nullptr);
}

TEST(SimdText, ContainsAcrossLengths) {
    std::string base = "abcdefghijklmnopqrstuvwxyz";
    for (size_t len = 0; len <= 80; len++) {
        std::string s;
        for (size_t i = 0; i < len; i++) s += base[i % base.size()];
        /* needle present at every position */
        for (size_t pos = 0; pos < len; pos++) {
            std::string t = s;
            t[pos] = '&';
            EXPECT_EQ(leptris_text_contains(t.data(), t.size(), '&'), 1)
                << "len=" << len << " pos=" << pos;
        }
        /* needle absent */
        EXPECT_EQ(leptris_text_contains(s.data(), s.size(), '&'), 0)
            << "len=" << len;
    }
}

TEST(SimdText, ContainsZeroLength) {
    EXPECT_EQ(leptris_text_contains("", 0, '&'), 0);
}

TEST(SimdText, ContainsSingleChar) {
    EXPECT_EQ(leptris_text_contains("&", 1, '&'), 1);
    EXPECT_EQ(leptris_text_contains("x", 1, '&'), 0);
}

TEST(SimdText, ContainsBoundaryCrossing) {
    /* '&' at the last byte of each SIMD chunk width (16/32) */
    for (int w : {15, 16, 17, 31, 32, 33, 63, 64, 65}) {
        std::string s(w, 'x');
        s[s.size() - 1] = '&';
        EXPECT_EQ(leptris_text_contains(s.data(), s.size(), '&'), 1) << "w=" << w;
        std::string t(w, 'x');
        EXPECT_EQ(leptris_text_contains(t.data(), t.size(), '&'), 0) << "w=" << w;
    }
}

TEST(SimdText, FindAcrossLengths) {
    for (int w : {1, 5, 15, 16, 17, 31, 32, 33, 63, 64, 65}) {
        std::string s(w, 'x');
        EXPECT_EQ(leptris_text_find(s.data(), s.size(), '&'), -1) << "w=" << w;
        s[w / 2] = '&';
        EXPECT_EQ(leptris_text_find(s.data(), s.size(), '&'), w / 2) << "w=" << w;
    }
}

TEST(SimdText, Find3AcrossLengths) {
    /* "->" style triple search used by comment/CDATA end detection */
    for (int w : {3, 5, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100}) {
        std::string s(w, 'x');
        EXPECT_EQ(leptris_text_find3(s.data(), s.size(), '-', '-', '>'), -1)
            << "w=" << w;
        if (w >= 3) {
            s[w - 3] = '-';
            s[w - 2] = '-';
            s[w - 1] = '>';
            EXPECT_EQ(leptris_text_find3(s.data(), s.size(), '-', '-', '>'), w - 3)
                << "w=" << w;
        }
    }
}

TEST(SimdText, Find3OverlappingAnchors) {
    /* "a--->b" — anchors at 1 and 2; only index 2 completes the
     * triple ("-->"): s[2..4]. Index 1 is a false anchor. */
    const char* s = "a--->b";
    EXPECT_EQ(leptris_text_find3(s, 6, '-', '-', '>'), 2);
    /* "--x-->" — false anchor at 0, real match at 3. */
    const char* t = "--x-->";
    EXPECT_EQ(leptris_text_find3(t, 6, '-', '-', '>'), 3);
}

TEST(SimdText, Find3TooShort) {
    const char* s = "--";
    EXPECT_EQ(leptris_text_find3(s, 2, '-', '-', '>'), -1);
    EXPECT_EQ(leptris_text_find3(s, 0, '-', '-', '>'), -1);
}

TEST(SimdText, Find3LongBodyEveryPosition) {
    /* Exercise the vector loop (len >= 16), the 14-byte chunk advance,
     * and boundary-straddling triples. A match must be found at every
     * possible start offset in a comment-like body. */
    std::string body(80, 'x');
    for (size_t pos = 0; pos + 3 <= body.size(); pos++) {
        std::string t = body;
        t[pos] = '-';
        t[pos + 1] = '-';
        t[pos + 2] = '>';
        EXPECT_EQ(leptris_text_find3(t.data(), t.size(), '-', '-', '>'),
                  (ptrdiff_t)pos)
            << "pos=" << pos;
    }
    /* No match in a long body. */
    EXPECT_EQ(leptris_text_find3(body.data(), body.size(), '-', '-', '>'), -1);
}

TEST(SimdText, Find3DashRunHeavyBody) {
    /* Adoc-style separator comment: long dash runs starve the old
     * memchr-anchor verify loop. The SIMD path must find the real
     * terminator. */
    std::string t(200, '-');
    t += "-->";
    t.append(50, 'x');
    EXPECT_EQ(leptris_text_find3(t.data(), t.size(), '-', '-', '>'), 200);
}

TEST(SimdText, Find3TerminatorAtVeryEnd) {
    std::string t(40, 'y');
    t += "-->";
    EXPECT_EQ(leptris_text_find3(t.data(), t.size(), '-', '-', '>'), 40);
    /* "--" at the end without '>' is not a match. */
    std::string u(40, 'y');
    u += "--";
    EXPECT_EQ(leptris_text_find3(u.data(), u.size(), '-', '-', '>'), -1);
}

/* ---- leptris_text_scan_run (#1842): one-pass text-run scan ----
 *
 * Parser contract: find the first `term` byte and report whether f1/f2
 * occur strictly BEFORE it. Replaces the 48-byte inline loop plus up to
 * three per-run memchr calls in the direct parser's text path. */

TEST(SimdText, ScanRunFindsTermEveryPosition) {
    std::string base = "abcdefghijklmnopqrstuvwxyz";
    for (size_t len = 1; len <= 80; len++) {
        std::string s;
        for (size_t i = 0; i < len; i++) s += base[i % base.size()];
        for (size_t pos = 0; pos < len; pos++) {
            std::string t = s;
            t[pos] = '<';
            int saw_amp = -1, saw_cr = -1;
            ptrdiff_t off = leptris_text_scan_run(
                t.data(), t.size(), '<', '&', '\r', &saw_amp, &saw_cr);
            EXPECT_EQ(off, (ptrdiff_t)pos)
                << "len=" << len << " pos=" << pos;
            EXPECT_EQ(saw_amp, 0) << "len=" << len << " pos=" << pos;
            EXPECT_EQ(saw_cr, 0) << "len=" << len << " pos=" << pos;
        }
    }
}

TEST(SimdText, ScanRunFlagsCountPrefixOnly) {
    /* '&'/CR before the term set the flags; the same bytes after it
     * must NOT (the parser's entity/CR handling only covers the run
     * it carves). */
    {
        std::string t = "ab&cd<ef&gh";
        int saw_amp = -1, saw_cr = -1;
        ptrdiff_t off = leptris_text_scan_run(
            t.data(), t.size(), '<', '&', '\r', &saw_amp, &saw_cr);
        EXPECT_EQ(off, 5);
        EXPECT_EQ(saw_amp, 1);
        EXPECT_EQ(saw_cr, 0);
    }
    {
        std::string t = "ab<cd&";  /* flag byte after the term is skipped */
        int saw_amp = -1, saw_cr = -1;
        ptrdiff_t off = leptris_text_scan_run(
            t.data(), t.size(), '<', '&', '\r', &saw_amp, &saw_cr);
        EXPECT_EQ(off, 2);
        EXPECT_EQ(saw_amp, 0);
        EXPECT_EQ(saw_cr, 0);
    }
    {
        std::string t = "a\r\rb<c\r";
        int saw_amp = -1, saw_cr = -1;
        ptrdiff_t off = leptris_text_scan_run(
            t.data(), t.size(), '<', '&', '\r', &saw_amp, &saw_cr);
        EXPECT_EQ(off, 4);
        EXPECT_EQ(saw_amp, 0);
        EXPECT_EQ(saw_cr, 1);
    }
    {
        std::string t = "&x\ry<z";
        int saw_amp = -1, saw_cr = -1;
        ptrdiff_t off = leptris_text_scan_run(
            t.data(), t.size(), '<', '&', '\r', &saw_amp, &saw_cr);
        EXPECT_EQ(off, 4);
        EXPECT_EQ(saw_amp, 1);
        EXPECT_EQ(saw_cr, 1);
    }
}

TEST(SimdText, ScanRunNoTermFlagsWholeSpan) {
    std::string base = "The quick brown fox jumps over the lazy dog ";
    for (size_t len = 0; len <= 200; len++) {
        std::string s;
        for (size_t i = 0; i < len; i++) s += base[i % base.size()];
        int saw_amp = -1, saw_cr = -1;
        ptrdiff_t off = leptris_text_scan_run(
            s.data(), s.size(), '<', '&', '\r', &saw_amp, &saw_cr);
        EXPECT_EQ(off, -1) << "len=" << len;
        EXPECT_EQ(saw_amp, 0) << "len=" << len;
        EXPECT_EQ(saw_cr, 0) << "len=" << len;
    }
    /* Flags fire across chunk boundaries when the term is absent. */
    std::string t(15, 'x');
    t += '&';
    t.append(20, 'x');
    t += '\r';
    t.append(40, 'x');
    int saw_amp = -1, saw_cr = -1;
    EXPECT_EQ(leptris_text_scan_run(t.data(), t.size(), '<', '&', '\r',
                                    &saw_amp, &saw_cr),
              -1);
    EXPECT_EQ(saw_amp, 1);
    EXPECT_EQ(saw_cr, 1);
}

TEST(SimdText, ScanRunTermAtVeryEnd) {
    std::string t(40, 'y');
    t += '<';
    int saw_amp = -1, saw_cr = -1;
    EXPECT_EQ(leptris_text_scan_run(t.data(), t.size(), '<', '&', '\r',
                                    &saw_amp, &saw_cr),
              40);
    EXPECT_EQ(saw_amp, 0);
    EXPECT_EQ(saw_cr, 0);
}

TEST(SimdText, ScanRunEmptySpan) {
    int saw_amp = -1, saw_cr = -1;
    EXPECT_EQ(leptris_text_scan_run("", 0, '<', '&', '\r', &saw_amp,
                                    &saw_cr),
              -1);
    EXPECT_EQ(saw_amp, 0);
    EXPECT_EQ(saw_cr, 0);
}

TEST(SimdText, ScanRunLongBodyEveryPosition) {
    /* Vector-loop coverage: term at every offset of a 300-byte body,
     * with flag bytes seeded before and after each position. */
    std::string body(300, 'x');
    for (size_t pos = 0; pos < body.size(); pos++) {
        std::string t = body;
        if (pos > 0) t[pos / 2] = '&';
        if (pos > 2) t[pos / 3] = '\r';
        t[pos] = '<';
        /* Expected flags derive from the final seeded string. */
        int e_amp = 0, e_cr = 0;
        for (size_t i = 0; i < pos; i++) {
            e_amp |= (t[i] == '&');
            e_cr |= (t[i] == '\r');
        }
        int saw_amp = -1, saw_cr = -1;
        ptrdiff_t off = leptris_text_scan_run(
            t.data(), t.size(), '<', '&', '\r', &saw_amp, &saw_cr);
        ASSERT_EQ(off, (ptrdiff_t)pos) << "pos=" << pos;
        EXPECT_EQ(saw_amp, e_amp) << "pos=" << pos;
        EXPECT_EQ(saw_cr, e_cr) << "pos=" << pos;
    }
}

}  // namespace
