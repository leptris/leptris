/* common/simd_text_neon.c — NEON implementations (TODO 175).
 *
 * On aarch64 NEON is architectural baseline — no runtime detection
 * needed and no special compile flags. Compiled unconditionally on
 * arm64 targets.
 *
 * Safety: processes full 16-byte chunks only while 16+ bytes remain,
 * then a scalar tail. Never reads past s+len.
 */
#include "simd_text.h"
#include "cpu.h"

#if defined(TAURUS_ARCH_ARM) && defined(__aarch64__)

#include <arm_neon.h>

int taurus_text_contains_neon(const char* s, size_t len, char c) {
    const char* p = s;
    const char* end = s + len;
    const uint8x16_t needle = vdupq_n_u8((uint8_t)c);

    while (end - p >= 16) {
        uint8x16_t v = vld1q_u8((const uint8_t*)p);
        uint8x16_t eq = vceqq_u8(v, needle);
        if (vmaxvq_u8(eq) != 0) return 1;
        p += 16;
    }
    while (p < end) {
        if (*p == c) return 1;
        p++;
    }
    return 0;
}

/* Three-byte sequence search. cand[i] must equal eq0[i] & eq1[i+1] &
 * eq2[i+2], so eq1/eq2 shift LEFT by 1/2 bytes: L_k(x)[i] = x[i+k],
 * expressed as vextq_u8(x, zero, k) — result[i] = x[i+k] within the
 * chunk, zero-filled past the end. Chunks advance 14 bytes (16 - 2)
 * so boundary-straddling triples are re-checked next iteration. */
ptrdiff_t taurus_text_find3_neon(const char* s, size_t len,
                                  char c0, char c1, char c2) {
    if (len < 3) return -1;
    const uint8x16_t n0 = vdupq_n_u8((uint8_t)c0);
    const uint8x16_t n1 = vdupq_n_u8((uint8_t)c1);
    const uint8x16_t n2 = vdupq_n_u8((uint8_t)c2);
    const uint8x16_t zero = vdupq_n_u8(0);

    size_t i = 0;
    for (; len - i >= 16; i += 14) {
        uint8x16_t v = vld1q_u8((const uint8_t*)(s + i));
        uint8x16_t eq0 = vceqq_u8(v, n0);
        uint8x16_t eq1 = vceqq_u8(v, n1);
        uint8x16_t eq2 = vceqq_u8(v, n2);
        uint8x16_t s1 = vextq_u8(eq1, zero, 1);  /* result[i] = eq1[i+1] */
        uint8x16_t s2 = vextq_u8(eq2, zero, 2);  /* result[i] = eq2[i+2] */
        uint8x16_t cand = vandq_u8(vandq_u8(eq0, s1), s2);
        if (vmaxvq_u8(cand) != 0) {
            /* Locate the byte; only in-chunk starts (first 14). */
            uint8_t bytes[16];
            vst1q_u8(bytes, cand);
            for (int k = 0; k < 14; k++) {
                if (bytes[k]) return (ptrdiff_t)(i + (size_t)k);
            }
        }
    }
    /* Scalar tail (also re-covers the 2 overlap bytes). */
    for (size_t j = i; j + 2 < len; j++) {
        if (s[j] == c0 && s[j + 1] == c1 && s[j + 2] == c2) {
            return (ptrdiff_t)j;
        }
    }
    return -1;
}

/* Byte-population count. vceqq gives 0xFF per match, but ADDV.16b
 * accumulates in a BYTE register — the raw sum overflows mod 256
 * (density ≥2 per chunk truncates to garbage). Widen pairwise first
 * (vpaddlq_u8 → u16 lanes ≤510), then vaddvq_u16 (u16 accumulator,
 * max 4080) — sum/255 is the match count. Scalar tail. */
size_t taurus_text_count_char_neon(const char* s, size_t len, char c) {
    const char* p = s;
    const char* end = s + len;
    const uint8x16_t needle = vdupq_n_u8((uint8_t)c);
    size_t n = 0;

    while (end - p >= 16) {
        uint8x16_t v = vld1q_u8((const uint8_t*)p);
        uint8x16_t eq = vceqq_u8(v, needle);
        n += (size_t)(vaddvq_u16(vpaddlq_u8(eq)) / 255);
        p += 16;
    }
    while (p < end) {
        if (*p == c) n++;
        p++;
    }
    return n;
}

#endif /* TAURUS_ARCH_ARM && __aarch64__ */
