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
#include "port.h"  /* LEPTRIS_CTZ (MSVC has no __builtin_ctz, issue #487) */

#if defined(LEPTRIS_ARCH_ARM) && defined(__aarch64__)

#include <arm_neon.h>

int leptris_text_contains_neon(const char* s, size_t len, char c) {
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
ptrdiff_t leptris_text_find3_neon(const char* s, size_t len,
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
size_t leptris_text_count_char_neon(const char* s, size_t len, char c) {
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
 * S10: per-char u16 accumulators with a horizontal reduce only every
 * 1024 chunks (lane ceiling 2048, far under u16). The old form paid
 * vaddvq horizontal reductions per vector per char — counting, not
 * the copy, capped the kernel at ~16 GB/s while memcpy runs 5x+ that.
 * Counts saturate no lane; flush cadence keeps every u16 lane < 2048. */
void leptris_text_count3_neon(const char* s, size_t len,
                             char c0, char c1, char c2,
                             size_t* n0, size_t* n1, size_t* n2) {
    const char* p = s;
    const char* end = s + len;
    const uint8x16_t k0 = vdupq_n_u8((uint8_t)c0);
    const uint8x16_t k1 = vdupq_n_u8((uint8_t)c1);
    const uint8x16_t k2 = vdupq_n_u8((uint8_t)c2);
    size_t a = 0, b = 0, c = 0;
    uint16x8_t a0 = vdupq_n_u16(0), a1 = vdupq_n_u16(0), a2 = vdupq_n_u16(0);
    size_t since_flush = 0;

    while (end - p >= 16) {
        uint8x16_t v = vld1q_u8((const uint8_t*)p);
        a0 = vpadalq_u8(a0, vshrq_n_u8(vceqq_u8(v, k0), 7));
        a1 = vpadalq_u8(a1, vshrq_n_u8(vceqq_u8(v, k1), 7));
        a2 = vpadalq_u8(a2, vshrq_n_u8(vceqq_u8(v, k2), 7));
        p += 16;
        if (++since_flush == 1024) {
            a += (size_t)vaddvq_u16(a0);
            b += (size_t)vaddvq_u16(a1);
            c += (size_t)vaddvq_u16(a2);
            a0 = a1 = a2 = vdupq_n_u16(0);
            since_flush = 0;
        }
    }
    a += (size_t)vaddvq_u16(a0);
    b += (size_t)vaddvq_u16(a1);
    c += (size_t)vaddvq_u16(a2);
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

/* count3 + high-byte detection in ONE pass (#1842): the parser's
 * sizing pre-scan doubles as the UTF-8 fast gate — pure-ASCII input
 * (no byte >= 0x80) is valid UTF-8 by construction, so the separate
 * SWAR validation pass disappears for the common case. */
void leptris_text_count3_high_neon(const char* s, size_t len,
                                   char c0, char c1, char c2,
                                   size_t* n0, size_t* n1, size_t* n2,
                                   int* saw_high) {
    const char* p = s;
    const char* end = s + len;
    const uint8x16_t k0 = vdupq_n_u8((uint8_t)c0);
    const uint8x16_t k1 = vdupq_n_u8((uint8_t)c1);
    const uint8x16_t k2 = vdupq_n_u8((uint8_t)c2);
    size_t a = 0, b = 0, c = 0;
    uint16x8_t a0 = vdupq_n_u16(0), a1 = vdupq_n_u16(0), a2 = vdupq_n_u16(0);
    uint8x16_t hi = vdupq_n_u8(0);
    size_t since_flush = 0;

    while (end - p >= 16) {
        uint8x16_t v = vld1q_u8((const uint8_t*)p);
        a0 = vpadalq_u8(a0, vshrq_n_u8(vceqq_u8(v, k0), 7));
        a1 = vpadalq_u8(a1, vshrq_n_u8(vceqq_u8(v, k1), 7));
        a2 = vpadalq_u8(a2, vshrq_n_u8(vceqq_u8(v, k2), 7));
        hi = vorrq_u8(hi, vshrq_n_u8(v, 7));
        p += 16;
        if (++since_flush == 1024) {
            a += (size_t)vaddvq_u16(a0);
            b += (size_t)vaddvq_u16(a1);
            c += (size_t)vaddvq_u16(a2);
            a0 = a1 = a2 = vdupq_n_u16(0);
            since_flush = 0;
        }
    }
    a += (size_t)vaddvq_u16(a0);
    b += (size_t)vaddvq_u16(a1);
    c += (size_t)vaddvq_u16(a2);
    int h = vmaxvq_u8(hi) != 0;
    while (p < end) {
        unsigned char ch = (unsigned char)*p;
        if (ch == (unsigned char)c0) a++;
        if (ch == (unsigned char)c1) b++;
        if (ch == (unsigned char)c2) c++;
        if (ch & 0x80) h = 1;
        p++;
    }
    *n0 = a;
    *n1 = b;
    *n2 = c;
    *saw_high = h;
}

/* Fused copy + three counters in ONE memory pass (TODO 188). The
 * parser's owns-copy path used to stream the document twice — a
 * count3 pre-scan for arena sizing, then the memcpy into the
 * buffer copy. This kernel loads each chunk once, stores it to
 * dst, and counts all three chars from the same registers.
 * S10: same accumulator/flush form as count3_neon — the copy is
 * cheap; the reductions were the cost. */
void leptris_copy_count3_neon(char* dst, const char* src, size_t len,
                             char c0, char c1, char c2,
                             size_t* n0, size_t* n1, size_t* n2) {
    const char* p = src;
    const char* end = src + len;
    char* q = dst;
    const uint8x16_t k0 = vdupq_n_u8((uint8_t)c0);
    const uint8x16_t k1 = vdupq_n_u8((uint8_t)c1);
    const uint8x16_t k2 = vdupq_n_u8((uint8_t)c2);
    size_t a = 0, b = 0, c = 0;
    uint16x8_t a0 = vdupq_n_u16(0), a1 = vdupq_n_u16(0), a2 = vdupq_n_u16(0);
    size_t since_flush = 0;

    while (end - p >= 32) {
        uint8x16_t v0 = vld1q_u8((const uint8_t*)p);
        uint8x16_t v1 = vld1q_u8((const uint8_t*)p + 16);
        vst1q_u8((uint8_t*)q, v0);
        vst1q_u8((uint8_t*)q + 16, v1);
        a0 = vpadalq_u8(a0, vpaddq_u8(vshrq_n_u8(vceqq_u8(v0, k0), 7),
                                       vshrq_n_u8(vceqq_u8(v1, k0), 7)));
        a1 = vpadalq_u8(a1, vpaddq_u8(vshrq_n_u8(vceqq_u8(v0, k1), 7),
                                       vshrq_n_u8(vceqq_u8(v1, k1), 7)));
        a2 = vpadalq_u8(a2, vpaddq_u8(vshrq_n_u8(vceqq_u8(v0, k2), 7),
                                       vshrq_n_u8(vceqq_u8(v1, k2), 7)));
        p += 32;
        q += 32;
        if (++since_flush == 1024) {
            a += (size_t)vaddvq_u16(a0);
            b += (size_t)vaddvq_u16(a1);
            c += (size_t)vaddvq_u16(a2);
            a0 = a1 = a2 = vdupq_n_u16(0);
            since_flush = 0;
        }
    }
    a += (size_t)vaddvq_u16(a0);
    b += (size_t)vaddvq_u16(a1);
    c += (size_t)vaddvq_u16(a2);
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

#endif /* LEPTRIS_ARCH_ARM && __aarch64__ */

/* Structural span scan (TODO 193 Phase 1). Marker detection is 8
 * vector compares per 16-byte chunk ('<','>','/','\'','"','=','&'
 * plus a c<=' ' range test for whitespace); the per-hit class comes
 * from the shared 256-byte table — hits are ~1 per 8-15 bytes in
 * real XML, so the scalar lookups are noise. The 16-bit mask is
 * built with the bit-weight + horizontal-add trick: no NEON
 * movemask exists, and weights sum to the exact bitmask because
 * each bit appears once (no carries). */
size_t leptris_text_scan_events_neon(const char* buf, size_t len,
                                    LeptrisScanEvent* out, size_t max) {
    leptris_dp_class_table_init();
    const uint8x16_t k_lt   = vdupq_n_u8('<');
    const uint8x16_t k_gt   = vdupq_n_u8('>');
    const uint8x16_t k_sl   = vdupq_n_u8('/');
    const uint8x16_t k_eq   = vdupq_n_u8('=');
    const uint8x16_t k_qs   = vdupq_n_u8('\'');
    const uint8x16_t k_qd   = vdupq_n_u8('"');
    const uint8x16_t k_amp  = vdupq_n_u8('&');
    const uint8x16_t k_sp   = vdupq_n_u8(' ');
    /* vshlq_u16 takes an int16x8_t shift vector per the ARM ACLE;
     * lanes 0..7 are fine as signed. Apple clang accepts the
     * unsigned form silently, GCC rejects it (issue #487). */
    static const int16_t sh8[8] = {0,1,2,3,4,5,6,7};
    const int16x8_t sh8v = vld1q_s16(sh8);

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
                unsigned idx = (unsigned)LEPTRIS_CTZ(mask);
                out[n].offset = (uint32_t)(base + idx);
                out[n].cls = leptris_dp_class_table[p[idx]];
                n++;
                mask ^= bit;
            }
        }
        p += 16;
        base += 16;
    }
    /* Tail: scalar, same table. */
    while (p < end && n < max) {
        unsigned char cls = leptris_dp_class_table[*p];
        if (cls) {
            out[n].offset = (uint32_t)((size_t)(p - (const unsigned char*)buf));
            out[n].cls = cls;
            n++;
        }
        p++;
    }
    return n;
}

/* #1842: one-pass text-run scan — first `term` byte plus f1/f2
 * seen-flags over the prefix. Full chunks OR-accumulate the f1/f2
 * compare masks; a term hit resolves its lane scalar (once per run,
 * at most 15 bytes of prefix re-check) and returns without folding
 * the post-hit lanes. */
ptrdiff_t leptris_text_scan_run_neon(const char* s, size_t len, char term,
                                     char f1, char f2,
                                     int* saw_f1, int* saw_f2) {
    const char* p = s;
    const char* end = s + len;
    const uint8x16_t kt = vdupq_n_u8((uint8_t)term);
    const uint8x16_t k1 = vdupq_n_u8((uint8_t)f1);
    const uint8x16_t k2 = vdupq_n_u8((uint8_t)f2);
    uint8x16_t acc1 = vdupq_n_u8(0);
    uint8x16_t acc2 = vdupq_n_u8(0);

    while (end - p >= 16) {
        uint8x16_t v = vld1q_u8((const uint8_t*)p);
        uint8x16_t eqt = vceqq_u8(v, kt);
        if (vmaxvq_u8(eqt) != 0) {
            int i = 0;
            while (p[i] != term) i++;
            int a = vmaxvq_u8(acc1) != 0;
            int b = vmaxvq_u8(acc2) != 0;
            for (int j = 0; j < i; j++) {
                a |= (p[j] == f1);
                b |= (p[j] == f2);
            }
            *saw_f1 = a;
            *saw_f2 = b;
            return (ptrdiff_t)(p + i - s);
        }
        acc1 = vorrq_u8(acc1, vceqq_u8(v, k1));
        acc2 = vorrq_u8(acc2, vceqq_u8(v, k2));
        p += 16;
    }
    {
        int a = vmaxvq_u8(acc1) != 0;
        int b = vmaxvq_u8(acc2) != 0;
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
 * Same derivation as the SSE core in simd_text_avx2.c; NEON bytes are
 * unsigned, so ranges are direct vcge/vcle pairs. */
int leptris_utf8_valid_neon(const char* src, size_t len) {
    const unsigned char* p = (const unsigned char*)src;
    if (len < 16) return leptris_utf8_valid_scalar(src, len);

    const uint8x16_t zero = vdupq_n_u8(0);
    const uint8x16_t ff = vdupq_n_u8(0xFF);
    const uint8x16_t b80 = vdupq_n_u8(0x80);
    const uint8x16_t bBF = vdupq_n_u8(0xBF);
    uint8x16_t prev = zero;

#define IN_RANGE(v, lo, hi)                                             \
    vandq_u8(vcgeq_u8((v), vdupq_n_u8(lo)), vcleq_u8((v), vdupq_n_u8(hi)))
    size_t i = 0;
    while (i + 16 <= len) {
        uint8x16_t cur = vld1q_u8(p + i);
        /* lane j = byte(j-1): vext(prev, cur, k) keeps the last k
         * lanes of prev at the head -> k = 15/14/13. */
        uint8x16_t b1 = vextq_u8(prev, cur, 15);
        uint8x16_t b2 = vextq_u8(prev, cur, 14);
        uint8x16_t b3 = vextq_u8(prev, cur, 13);

        uint8x16_t st2c = IN_RANGE(cur, 0xC2, 0xDF);
        uint8x16_t st3c = IN_RANGE(cur, 0xE0, 0xEF);
        uint8x16_t st4c = IN_RANGE(cur, 0xF0, 0xF4);
        uint8x16_t is_ascii = vcltq_u8(cur, b80);
        uint8x16_t legal_start = vorrq_u8(st2c, vorrq_u8(st3c, st4c));

        uint8x16_t st2b1 = IN_RANGE(b1, 0xC2, 0xDF);
        uint8x16_t st3b1 = IN_RANGE(b1, 0xE0, 0xEF);
        uint8x16_t st4b1 = IN_RANGE(b1, 0xF0, 0xF4);
        uint8x16_t st3b2 = IN_RANGE(b2, 0xE0, 0xEF);
        uint8x16_t st4b2 = IN_RANGE(b2, 0xF0, 0xF4);
        uint8x16_t st4b3 = IN_RANGE(b3, 0xF0, 0xF4);

        uint8x16_t must_cont =
            vorrq_u8(vorrq_u8(st2b1, vorrq_u8(st3b1, st4b1)),
                     vorrq_u8(vorrq_u8(st3b2, st4b2), st4b3));

        const uint8x16_t bA0 = vdupq_n_u8(0xA0);
        const uint8x16_t b90 = vdupq_n_u8(0x90);
        const uint8x16_t b9F = vdupq_n_u8(0x9F);
        const uint8x16_t b8F = vdupq_n_u8(0x8F);
        uint8x16_t e0 = vceqq_u8(b1, vdupq_n_u8(0xE0));
        uint8x16_t ed = vceqq_u8(b1, vdupq_n_u8(0xED));
        uint8x16_t f0 = vceqq_u8(b1, vdupq_n_u8(0xF0));
        uint8x16_t f4 = vceqq_u8(b1, vdupq_n_u8(0xF4));
        uint8x16_t lo = vbslq_u8(e0, bA0, b80);
        lo = vbslq_u8(f0, b90, lo);
        uint8x16_t hi = vbslq_u8(ed, b9F, bBF);
        hi = vbslq_u8(f4, b8F, hi);

        uint8x16_t cont_ok = vandq_u8(vcgeq_u8(cur, lo), vcleq_u8(cur, hi));

        uint8x16_t block_error = vorrq_u8(
            vandq_u8(must_cont, veorq_u8(cont_ok, ff)),
            vandq_u8(veorq_u8(must_cont, ff),
                     vandq_u8(veorq_u8(is_ascii, ff),
                              veorq_u8(legal_start, ff))));
        /* any lane set? */
        /* any lane set? */
        if (vmaxvq_u8(block_error) != 0) return 0;
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
#undef IN_RANGE
