/* common/simd_text_avx2.c — AVX2 implementations (TODO 175).
 *
 * Compiled with -mavx2 (see src/CMakeLists.txt per-file flags). Only
 * entered when leptris_cpu_detect() reports AVX2 at runtime, so the
 * whole TU's instructions are guaranteed available.
 *
 * Safety: processes full 32-byte chunks only while 32+ bytes remain,
 * then a 16-byte chunk if 16+ remain, then a scalar tail. Never reads
 * past s+len.
 */
#include "simd_text.h"
#include "cpu.h"
#include "../common/port.h"

#if defined(LEPTRIS_ARCH_X86) && defined(__AVX2__)
#include <immintrin.h>

int leptris_text_contains_avx2(const char* s, size_t len, char c) {
    const char* p = s;
    const char* end = s + len;
    const __m256i needle = _mm256_set1_epi8((char)c);

    while (end - p >= 32) {
        __m256i v = _mm256_loadu_si256((const __m256i*)p);
        __m256i eq = _mm256_cmpeq_epi8(v, needle);
        if (_mm256_movemask_epi8(eq) != 0) return 1;
        p += 32;
    }
    if (end - p >= 16) {
        __m128i v = _mm_loadu_si128((const __m128i*)p);
        __m128i eq = _mm_cmpeq_epi8(v, _mm256_castsi256_si128(needle));
        if (_mm_movemask_epi8(eq) != 0) return 1;
        p += 16;
    }
    while (p < end) {
        if (*p == c) return 1;
        p++;
    }
    return 0;
}

/* Three-byte sequence search via movemask bit-alignment:
 * a triple at position i needs eq0[i] & eq1[i+1] & eq2[i+2], which in
 * movemask-space is m0 & (m1 >> 1) & (m2 >> 2). Chunks advance 30
 * bytes (32 - 2) so triples straddling a chunk boundary are re-checked
 * in the next iteration; first-match semantics make the redundancy
 * harmless. Positions with i+2 >= len are masked off. */
ptrdiff_t leptris_text_find3_avx2(const char* s, size_t len,
                                  char c0, char c1, char c2) {
    if (len < 3) return -1;
    const __m256i n0 = _mm256_set1_epi8(c0);
    const __m256i n1 = _mm256_set1_epi8(c1);
    const __m256i n2 = _mm256_set1_epi8(c2);

    size_t i = 0;
    /* Full chunks: at least 32 bytes remain, and a triple starting
     * here ends within len (guard via (len - i) > 2 checked by mask). */
    for (; len - i >= 32; i += 30) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(s + i));
        uint32_t m0 = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, n0));
        uint32_t m1 = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, n1));
        uint32_t m2 = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, n2));
        /* In-chunk positions only: a triple starting at bit 30/31 would
         * read beyond this chunk — left for the next (overlapping)
         * iteration. */
        uint32_t match = m0 & (m1 >> 1) & (m2 >> 2) & 0x3FFFFFFFu;
        if (match) return (ptrdiff_t)(i + LEPTRIS_CTZ(match));
    }
    /* Scalar tail (also re-covers the 2 overlap bytes). */
    for (size_t j = i; j + 2 < len; j++) {
        if (s[j] == c0 && s[j + 1] == c1 && s[j + 2] == c2) {
            return (ptrdiff_t)j;
        }
    }
    return -1;
}

/* Byte-population count: cmpeq → movemask → popcount per 32-byte
 * chunk, 16-byte SSE step, scalar tail. Never reads past s+len. */
size_t leptris_text_count_char_avx2(const char* s, size_t len, char c) {
    const char* p = s;
    const char* end = s + len;
    const __m256i needle = _mm256_set1_epi8((char)c);
    size_t n = 0;

    while (end - p >= 32) {
        __m256i v = _mm256_loadu_si256((const __m256i*)p);
        __m256i eq = _mm256_cmpeq_epi8(v, needle);
        n += (size_t)__builtin_popcount((unsigned)_mm256_movemask_epi8(eq));
        p += 32;
    }
    if (end - p >= 16) {
        __m128i v = _mm_loadu_si128((const __m128i*)p);
        __m128i eq = _mm_cmpeq_epi8(v, _mm256_castsi256_si128(needle));
        n += (size_t)__builtin_popcount((unsigned)_mm_movemask_epi8(eq));
        p += 16;
    }
    while (p < end) {
        if (*p == c) n++;
        p++;
    }
    return n;
}

#endif /* LEPTRIS_ARCH_X86 && __AVX2__ */

/* #1842: one-pass text-run scan — AVX2 mirror of the NEON kernel:
 * full 32-byte chunks OR-accumulate the f1/f2 movemasks; the term
 * mask hits and the first lane is ctz-resolved with a scalar prefix
 * fold. */
ptrdiff_t leptris_text_scan_run_avx2(const char* s, size_t len, char term,
                                     char f1, char f2,
                                     int* saw_f1, int* saw_f2) {
    const char* p = s;
    const char* end = s + len;
    const __m256i kt = _mm256_set1_epi8(term);
    const __m256i k1 = _mm256_set1_epi8(f1);
    const __m256i k2 = _mm256_set1_epi8(f2);
    uint32_t acc1 = 0, acc2 = 0;

    while (end - p >= 32) {
        __m256i v = _mm256_loadu_si256((const __m256i*)p);
        uint32_t mt =
            (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, kt));
        if (mt) {
            unsigned idx = (unsigned)LEPTRIS_CTZ(mt);
            int a = (acc1 != 0);
            int b = (acc2 != 0);
            for (unsigned j = 0; j < idx; j++) {
                a |= (p[j] == f1);
                b |= (p[j] == f2);
            }
            *saw_f1 = a;
            *saw_f2 = b;
            return (ptrdiff_t)(p + idx - s);
        }
        acc1 |= (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, k1));
        acc2 |= (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, k2));
        p += 32;
    }
    {
        int a = (acc1 != 0);
        int b = (acc2 != 0);
        while (p < end) {
            char ch = *p;
            if (ch == term) break;
            a |= (ch == f1);
            b |= (ch == f2);
            p++;
        }
        *saw_f1 = a;
        *saw_f2 = b;
        return (p < end) ? (ptrdiff_t)(p - s) : -1;
    }
}

/* ---- UTF-8 validation (leptris#1842 A5) -------------------------------
 *
 * Per-16-byte block, per-byte required ranges derived from the three
 * preceding bytes (carried across blocks via palignr):
 *   must_cont(j) = start2(j-1) | start3(j-1) | start4(j-1)
 *                | start3(j-2) | start4(j-2) | start4(j-3)
 * where startN(v) = v in [C2..DF | E0..EF | F0..F4] by length class.
 * A must-cont byte must land in its opener's continuation range
 * (E0 -> A0..BF, ED -> 80..9F, F0 -> 90..BF, F4 -> 80..8F, else
 * 80..BF); a non-cont byte must be ASCII or a legal start. The tail
 * (<16 bytes) runs the scalar DFA anchored at a character boundary
 * (walk back at most 3 bytes). Verdicts identical to
 * leptris_utf8_valid_scalar — pinned by the encoding suite.
 */
int leptris_utf8_valid_avx2(const char* src, size_t len) {
    const unsigned char* p = (const unsigned char*)src;
    if (len < 16) return leptris_utf8_valid_scalar(src, len);

    const __m128i zero = _mm_setzero_si128();
    const __m128i b80 = _mm_set1_epi8((char)0x80);
    const __m128i bBF = _mm_set1_epi8((char)0xBF);
    __m128i prev = zero; /* the previous block's 16 bytes */

    size_t i = 0;
    while (i + 16 <= len) {
        __m128i cur = _mm_loadu_si128((const __m128i*)(p + i));
        /* result[j] = byte(j-1): concat(a:b) shifted right leaves the
         * PREVIOUS block's last lanes at the head — imm 15/14/13. */
        __m128i b1 = _mm_alignr_epi8(cur, prev, 15);
        __m128i b2 = _mm_alignr_epi8(cur, prev, 14);
        __m128i b3 = _mm_alignr_epi8(cur, prev, 13);

        /* start classes (unsigned ranges via saturating compare) */
#define IN_RANGE(v, lo, hi)                                            \
        (_mm_cmpeq_epi8(_mm_subs_epu8((v), _mm_set1_epi8(lo)), zero) & \
         _mm_cmpeq_epi8(_mm_subs_epu8(_mm_set1_epi8(hi), (v)), zero))
        __m128i st2c = IN_RANGE(cur, (char)0xC2, (char)0xDF);
        __m128i st3c = IN_RANGE(cur, (char)0xE0, (char)0xEF);
        __m128i st4c = IN_RANGE(cur, (char)0xF0, (char)0xF4);
        __m128i is_ascii =
            _mm_cmpeq_epi8(_mm_and_si128(cur, b80), zero);
        __m128i legal_start = _mm_or_si128(
            st2c, _mm_or_si128(st3c, st4c));

        __m128i st2b1 = IN_RANGE(b1, (char)0xC2, (char)0xDF);
        __m128i st3b1 = IN_RANGE(b1, (char)0xE0, (char)0xEF);
        __m128i st4b1 = IN_RANGE(b1, (char)0xF0, (char)0xF4);
        __m128i st3b2 = IN_RANGE(b2, (char)0xE0, (char)0xEF);
        __m128i st4b2 = IN_RANGE(b2, (char)0xF0, (char)0xF4);
        __m128i st4b3 = IN_RANGE(b3, (char)0xF0, (char)0xF4);
#undef IN_RANGE

        __m128i must_cont = _mm_or_si128(
            _mm_or_si128(st2b1, _mm_or_si128(st3b1, st4b1)),
            _mm_or_si128(_mm_or_si128(st3b2, st4b2), st4b3));

        /* continuation range refinement from the opener (b1) */
        const __m128i bA0 = _mm_set1_epi8((char)0xA0);
        const __m128i b90 = _mm_set1_epi8((char)0x90);
        const __m128i b9F = _mm_set1_epi8((char)0x9F);
        const __m128i b8F = _mm_set1_epi8((char)0x8F);
        const __m128i e0 = _mm_cmpeq_epi8(b1, _mm_set1_epi8((char)0xE0));
        const __m128i ed = _mm_cmpeq_epi8(b1, _mm_set1_epi8((char)0xED));
        const __m128i f0 = _mm_cmpeq_epi8(b1, _mm_set1_epi8((char)0xF0));
        const __m128i f4 = _mm_cmpeq_epi8(b1, _mm_set1_epi8((char)0xF4));
        __m128i lo = _mm_blendv_epi8(b80, bA0, e0);
        lo = _mm_blendv_epi8(lo, b90, f0);
        __m128i hi = _mm_blendv_epi8(bBF, b9F, ed);
        hi = _mm_blendv_epi8(hi, b8F, f4);

        /* cont-in-range */
        __m128i cge = _mm_subs_epu8(cur, lo); /* max(0, cur-lo) */
        __m128i cle = _mm_subs_epu8(hi, cur); /* max(0, hi-cur) */
        __m128i cont_ok = _mm_and_si128(
            _mm_cmpeq_epi8(cge, zero), _mm_cmpeq_epi8(cle, zero));

        const __m128i ff = _mm_set1_epi8((char)0xFF);
        __m128i not_must = _mm_xor_si128(must_cont, ff);
        __m128i not_ascii = _mm_xor_si128(is_ascii, ff);
        __m128i not_start = _mm_xor_si128(legal_start, ff);
        /* error = must_cont & !cont_ok  |  !must_cont & !ascii &
         * !legal_start (0x80..C1 and F5..FF fall out of the latter). */
        __m128i block_error = _mm_or_si128(
            _mm_and_si128(must_cont,
                          _mm_xor_si128(cont_ok, ff)),
            _mm_and_si128(not_must,
                          _mm_and_si128(not_ascii, not_start)));
        if (_mm_movemask_epi8(
                _mm_cmpeq_epi8(_mm_and_si128(block_error, b80), b80)) != 0)
            return 0;
        prev = cur;
        i += 16;
    }
    if (i >= len) {
        /* The input ENDS at the block edge: the last sequence must
         * be complete (the scalar DFA's "len - i <= n" rule). Walk
         * back over at most 3 continuations; s2 lands ON the last
         * continuation (or the final non-continuation byte). */
        size_t s2 = i;
        size_t conts = 0;
        while (s2 > 0 && (p[s2 - 1] & 0xC0) == 0x80 && conts < 3) {
            s2--;
            conts++;
        }
        if (conts > 0) {
            if (s2 == 0) return 0; /* continuations, no opener anywhere */
            unsigned op = p[s2 - 1]; /* the opener BEFORE the run */
            unsigned owed = (op >= 0xF0) ? 3
                          : (op >= 0xE0) ? 2
                          : (op >= 0xC2) ? 1
                                         : 0;
            if (owed > conts) return 0;
            return 1;
        }
        /* the last byte is not a continuation: it must be ASCII */
        unsigned op = p[i - 1];
        unsigned owed = (op >= 0xF0) ? 3
                      : (op >= 0xE0) ? 2
                      : (op >= 0xC2) ? 1
                                     : 0;
        return owed == 0 ? 1 : 0;
    }
    /* Tail: scalar DFA anchored at a character boundary (at most 3
     * bytes back from the SIMD end; a sequence is at most 4) — the
     * anchored run also covers a sequence spanning the block edge. */
    size_t start = i;
    while (start > 0 && (p[start] & 0xC0) == 0x80 && i - start < 3)
        start--;
    return leptris_utf8_valid_scalar((const char*)(p + start), len - start);
}