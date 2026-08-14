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

#endif /* TAURUS_ARCH_X86 && __AVX2__ */
