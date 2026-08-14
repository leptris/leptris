/* common/simd_text_avx2.c — AVX2 implementations (TODO 175).
 *
 * Compiled with -mavx2 (see src/CMakeLists.txt per-file flags). Only
 * entered when taurus_cpu_detect() reports AVX2 at runtime, so the
 * whole TU's instructions are guaranteed available.
 *
 * Safety: processes full 32-byte chunks only while 32+ bytes remain,
 * then a 16-byte chunk if 16+ remain, then a scalar tail. Never reads
 * past s+len.
 */
#include "simd_text.h"
#include "cpu.h"
#include "../common/port.h"

#if defined(TAURUS_ARCH_X86) && defined(__AVX2__)
#include <immintrin.h>

int taurus_text_contains_avx2(const char* s, size_t len, char c) {
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
ptrdiff_t taurus_text_find3_avx2(const char* s, size_t len,
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
        if (match) return (ptrdiff_t)(i + TAURUS_CTZ(match));
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
size_t taurus_text_count_char_avx2(const char* s, size_t len, char c) {
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

#endif /* TAURUS_ARCH_X86 && __AVX2__ */
