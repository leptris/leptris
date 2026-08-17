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

/* Three counters in one memory pass — the sizing pre-scan shape.
 * Same widen-then-add idiom as count_char above, three vceqq per
 * chunk. One DRAM/L2 traversal instead of three. */
void taurus_text_count3_neon(const char* s, size_t len,
                             char c0, char c1, char c2,
                             size_t* n0, size_t* n1, size_t* n2) {
    const char* p = s;
    const char* end = s + len;
    const uint8x16_t k0 = vdupq_n_u8((uint8_t)c0);
    const uint8x16_t k1 = vdupq_n_u8((uint8_t)c1);
    const uint8x16_t k2 = vdupq_n_u8((uint8_t)c2);
    size_t a = 0, b = 0, c = 0;

    while (end - p >= 16) {
        uint8x16_t v = vld1q_u8((const uint8_t*)p);
        a += (size_t)(vaddvq_u16(vpaddlq_u8(vceqq_u8(v, k0))) / 255);
        b += (size_t)(vaddvq_u16(vpaddlq_u8(vceqq_u8(v, k1))) / 255);
        c += (size_t)(vaddvq_u16(vpaddlq_u8(vceqq_u8(v, k2))) / 255);
        p += 16;
    }
    while (p < end) {
        unsigned char ch = (unsigned char)*p;
        if (ch == (unsigned char)c0) a++;
        if (ch == (unsigned char)c1) b++;
        if (ch == (unsigned char)c2) c++;
        p++;
    }
    *n0 = a;
    *n1 = b;
    *n2 = c;
}

/* Fused copy + three counters in ONE memory pass (TODO 188). The
 * parser's owns-copy path used to stream the document twice — a
 * count3 pre-scan for arena sizing, then the memcpy into the
 * buffer copy. This kernel loads each chunk once, stores it to
 * dst, and counts all three chars from the same registers.
 * Measured on M1: 17-33% faster than memcpy + count3_neon across
 * 40-884 KB inputs (26 -> 16 GB/s single-pass effective). */
void taurus_copy_count3_neon(char* dst, const char* src, size_t len,
                             char c0, char c1, char c2,
                             size_t* n0, size_t* n1, size_t* n2) {
    const char* p = src;
    const char* end = src + len;
    char* q = dst;
    const uint8x16_t k0 = vdupq_n_u8((uint8_t)c0);
    const uint8x16_t k1 = vdupq_n_u8((uint8_t)c1);
    const uint8x16_t k2 = vdupq_n_u8((uint8_t)c2);
    size_t a = 0, b = 0, c = 0;

    while (end - p >= 32) {
        uint8x16_t v0 = vld1q_u8((const uint8_t*)p);
        uint8x16_t v1 = vld1q_u8((const uint8_t*)p + 16);
        vst1q_u8((uint8_t*)q, v0);
        vst1q_u8((uint8_t*)q + 16, v1);
        a += (size_t)(vaddvq_u16(vpaddlq_u8(vceqq_u8(v0, k0))) / 255);
        a += (size_t)(vaddvq_u16(vpaddlq_u8(vceqq_u8(v1, k0))) / 255);
        b += (size_t)(vaddvq_u16(vpaddlq_u8(vceqq_u8(v0, k1))) / 255);
        b += (size_t)(vaddvq_u16(vpaddlq_u8(vceqq_u8(v1, k1))) / 255);
        c += (size_t)(vaddvq_u16(vpaddlq_u8(vceqq_u8(v0, k2))) / 255);
        c += (size_t)(vaddvq_u16(vpaddlq_u8(vceqq_u8(v1, k2))) / 255);
        p += 32;
        q += 32;
    }
    while (p < end) {
        unsigned char ch = (unsigned char)*p;
        *q = (char)ch;
        if (ch == (unsigned char)c0) a++;
        if (ch == (unsigned char)c1) b++;
        if (ch == (unsigned char)c2) c++;
        p++;
        q++;
    }
    *n0 = a;
    *n1 = b;
    *n2 = c;
}

#endif /* TAURUS_ARCH_ARM && __aarch64__ */

/* Structural span scan (TODO 193 Phase 1). Marker detection is 8
 * vector compares per 16-byte chunk ('<','>','/','\'','"','=','&'
 * plus a c<=' ' range test for whitespace); the per-hit class comes
 * from the shared 256-byte table — hits are ~1 per 8-15 bytes in
 * real XML, so the scalar lookups are noise. The 16-bit mask is
 * built with the bit-weight + horizontal-add trick: no NEON
 * movemask exists, and weights sum to the exact bitmask because
 * each bit appears once (no carries). */
size_t taurus_text_scan_events_neon(const char* buf, size_t len,
                                    TaurusScanEvent* out, size_t max) {
    extern void taurus_dp_class_table_init(void);
    taurus_dp_class_table_init();
    const uint8x16_t k_lt   = vdupq_n_u8('<');
    const uint8x16_t k_gt   = vdupq_n_u8('>');
    const uint8x16_t k_sl   = vdupq_n_u8('/');
    const uint8x16_t k_eq   = vdupq_n_u8('=');
    const uint8x16_t k_qs   = vdupq_n_u8('\'');
    const uint8x16_t k_qd   = vdupq_n_u8('"');
    const uint8x16_t k_amp  = vdupq_n_u8('&');
    const uint8x16_t k_sp   = vdupq_n_u8(' ');
    static const uint16_t sh8[8] = {0,1,2,3,4,5,6,7};
    const uint16x8_t sh8v = vld1q_u16(sh8);

    size_t n = 0;
    const unsigned char* p = (const unsigned char*)buf;
    const unsigned char* end = p + len;
    size_t base = 0;

    while (end - p >= 16 && n < max) {
        uint8x16_t v = vld1q_u8(p);
        uint8x16_t m = vorrq_u8(vceqq_u8(v, k_lt), vceqq_u8(v, k_gt));
        m = vorrq_u8(m, vceqq_u8(v, k_sl));
        m = vorrq_u8(m, vceqq_u8(v, k_eq));
        m = vorrq_u8(m, vceqq_u8(v, k_qs));
        m = vorrq_u8(m, vceqq_u8(v, k_qd));
        m = vorrq_u8(m, vceqq_u8(v, k_amp));
        m = vorrq_u8(m, vcltq_u8(v, k_sp));   /* c < ' ' = ctrl/ws */
        m = vorrq_u8(m, vceqq_u8(v, k_sp));
        if (vmaxvq_u8(m) != 0) {
            /* NEON has no movemask: widen both halves, shift each
             * lane by its own index (vector shift), reduce. */
            uint8x16_t bits = vshrq_n_u8(m, 7);
            uint16x8_t lo16 = vshlq_u16(vmovl_u8(vget_low_u8(bits)), sh8v);
            uint16x8_t hi16 = vshlq_u16(vmovl_u8(vget_high_u8(bits)), sh8v);
            unsigned mask =
                (unsigned)vaddvq_u16(lo16) | ((unsigned)vaddvq_u16(hi16) << 8);
            while (mask) {
                if (n >= max) return max;
                unsigned bit = mask & (unsigned)(-(int)mask);
                unsigned idx = __builtin_ctz(mask);
                out[n].offset = (uint32_t)(base + idx);
                out[n].cls = taurus_dp_class_table[p[idx]];
                n++;
                mask ^= bit;
            }
        }
        p += 16;
        base += 16;
    }
    /* Tail: scalar, same table. */
    while (p < end && n < max) {
        unsigned char cls = taurus_dp_class_table[*p];
        if (cls) {
            out[n].offset = (uint32_t)((size_t)(p - (const unsigned char*)buf));
            out[n].cls = cls;
            n++;
        }
        p++;
    }
    return n;
}
