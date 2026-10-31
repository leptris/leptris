// #1842 A5: the SIMD UTF-8 validator must agree with the scalar DFA
// on every input — adversarial boundary matrix plus a randomized
// cross-check over lengths straddling the 16-byte block edge.
#include "../../src/leptris/common/simd_text.h"

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace {
}

TEST(Utf8Simd, SingleByteValues) {
    for (int b = 0; b <= 255; b++) {
        char buf[1] = {(char)b};
        int scalar = leptris_utf8_valid_scalar(buf, 1);
        int simd = leptris_utf8_valid(buf, 1);
        EXPECT_EQ(simd, scalar) << "byte 0x" << std::hex << b << std::dec;
        bool expect = (b < 0x80); /* a lone start byte is truncated */
        EXPECT_EQ(scalar, expect ? 1 : 0)
            << "byte 0x" << std::hex << b << std::dec;
    }
}

TEST(Utf8Simd, SequenceMatrixAcrossBlockEdges) {
    // (case, expected) — corrupt rows must be rejected too.
    const std::vector<std::pair<std::string, int>> cases = {
        // valid
        {{"\xC2\xA0"}, 1}, {{"\xDF\xBF"}, 1}, {{"\xE0\xA0\x80"}, 1},
        {{"\xE1\x80\x80"}, 1}, {{"\xED\x9F\xBF"}, 1}, {{"\xEE\x80\x80"}, 1},
        {{"\xEF\xBF\xBD"}, 1}, {{"\xF0\x90\x80\x80"}, 1},
        {{"\xF1\xBF\xBF\xBF"}, 1}, {{"\xF3\xA0\x80\x80"}, 1},
        {{"\xF4\x8F\xBF\xBF"}, 1},
        // overlong / surrogates / out-of-range
        {{"\xC0\x80"}, 0}, {{"\xC1\xBF"}, 0}, {{"\xE0\x80\x80"}, 0},
        {{"\xE0\x9F\xBF"}, 0}, {{"\xED\xA0\x80"}, 0}, {{"\xED\xBF\xBF"}, 0},
        {{"\xF0\x80\x80\x80"}, 0}, {{"\xF0\x8F\xBF\xBF"}, 0},
        {{"\xF4\x90\x80\x80"}, 0}, {{"\xF5\x80\x80\x80"}, 0},
        // truncated (each prefix)
        {{"\xC2"}, 0}, {{"\xE0\xA0"}, 0}, {{"\xF0\x90\x80"}, 0},
        {{"\xF4\x8F"}, 0},
        // stray continuation
        {{"\x80"}, 0}, {{"a\x80" "b"}, 0}, {{"\xBF"}, 0},
    };
    for (std::vector<std::pair<std::string, int> >::const_iterator
             it = cases.begin();
         it != cases.end(); ++it) {
        const std::string& s = it->first;
        const int expect = it->second;
        for (int pad = 0; pad < 20; pad++) {
            std::string buf((size_t)pad, 'a');
            buf += s;
            int scalar =
                leptris_utf8_valid_scalar(buf.data(), buf.size());
            int simd = leptris_utf8_valid(buf.data(), buf.size());
            EXPECT_EQ(simd, scalar)
                << "case len=" << s.size() << " pad=" << pad;
            EXPECT_EQ(scalar, expect)
                << "verdict drifted: len=" << s.size() << " pad=" << pad;
        }
    }
}

TEST(Utf8Simd, BlockEdgeSizes) {
    for (size_t n = 0; n <= 40; n++) {
        std::string buf(n, 'x');
        buf += "\xC3\xA9";
        ASSERT_EQ(leptris_utf8_valid_scalar(buf.data(), buf.size()), 1);
        ASSERT_EQ(leptris_utf8_valid(buf.data(), buf.size()), 1) << "n=" << n;
    }
    for (size_t n = 0; n <= 40; n++) {
        std::string buf(n, 'x');
        buf += "\xF0\x90\x80";
        ASSERT_EQ(leptris_utf8_valid_scalar(buf.data(), buf.size()), 0);
        ASSERT_EQ(leptris_utf8_valid(buf.data(), buf.size()), 0) << "n=" << n;
    }
}

TEST(Utf8Simd, RandomizedCrossCheck) {
    unsigned seed = 0x1842;
    auto rnd = [&seed]() {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 16) & 0xFF;
    };
    for (int trial = 0; trial < 4000; trial++) {
        size_t len = (size_t)(rnd() % 97);
        std::string buf;
        for (size_t i = 0; i < len; i++) {
            unsigned r = rnd() % 100;
            char c;
            if (r < 55) c = (char)('a' + (r % 26));
            else if (r < 75) c = (char)(0xC0 + (r % 0x40));
            else c = (char)(0x80 + (r % 0x80));
            buf.push_back(c);
        }
        /* The plain corpus rarely produces the boundary shapes that
         * broke the cores (x86-TSAN 2026-10-31): a start byte in the
         * final block lane with a non-continuation after it, and
         * sequences straddling 16/32-byte edges. Append an ASCII
         * tail after a valid-or-not multibyte run so every trial
         * also probes the tail-anchor at a shifted offset. */
        if (trial % 3 == 0) {
            static const char* mids[] = {
                "\xC3\xA9", "\xE4\xB8\xAD", "\xF0\x9F\x98\x80",
                "\xC3", "\xE0\xA0", "\xF0\x90\x80", "\xED\xA0\x80",
                "\xF4\x8F\xBF\xBF", "\xC2\xBF", "\xEF\xBF\xBD"
            };
            buf += mids[trial % 10];
            buf += "zz";
        }
        int scalar = leptris_utf8_valid_scalar(buf.data(), buf.size());
        int simd = leptris_utf8_valid(buf.data(), buf.size());
        EXPECT_EQ(simd, scalar)
            << "trial " << trial << " len " << buf.size();
    }
}

TEST(Utf8Simd, DenseValidDocument) {
    std::string buf;
    buf.reserve(64 * 1024);
    const char* unit = "héllo wörld — 日本語テキスト 🎉 emoji §naïve";
    for (int i = 0; i < 1024; i++) buf += unit;
    EXPECT_EQ(leptris_utf8_valid_scalar(buf.data(), buf.size()), 1);
    EXPECT_EQ(leptris_utf8_valid(buf.data(), buf.size()), 1);
    buf[buf.size() / 2] = (char)0xF5;
    EXPECT_EQ(leptris_utf8_valid(buf.data(), buf.size()), 0);
}
