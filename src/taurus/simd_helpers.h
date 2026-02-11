/* simd_helpers.h - SIMD acceleration for Taurus parser
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * Platform-agnostic SIMD operations:
 * - x86_64: SSE2 intrinsics (128-bit vectors, 16 bytes at once)
 * - ARM64: NEON intrinsics (128-bit vectors, 16 bytes at once)
 * - Fallback: Scalar operations for other platforms
 *
 * PERFORMANCE TARGET: 3-5× speedup on hot-path operations
 */

#ifndef TAURUS_SIMD_HELPERS_H
#define TAURUS_SIMD_HELPERS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Detect platform and include appropriate SIMD headers */
#if defined(__x86_64__) || defined(_M_X64)
    #define TAURUS_SIMD_SSE2 1
    #include <emmintrin.h>  /* SSE2 intrinsics */
    typedef __m128i simd_vec_t;
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define TAURUS_SIMD_NEON 1
    #include <arm_neon.h>   /* NEON intrinsics */
    typedef uint8x16_t simd_vec_t;
#else
    #define TAURUS_SIMD_NONE 1
#endif

/* SIMD vector size (16 bytes for both SSE2 and NEON) */
#define SIMD_VEC_SIZE 16

/* ==================================================================
 * SIMD WHITESPACE SCANNING
 * =================================================================
 * Scans for whitespace (space, tab, newline, carriage return) using
 * SIMD comparison operations. Processes 16 bytes per iteration.
 */

inline static const char* simd_skip_whitespace(const char* pos, const char* end) {
    const char* p = pos;

#if defined(TAURUS_SIMD_SSE2)
    /* SSE2 path - x86_64 */
    __m128i space = _mm_set1_epi8(' ');
    __m128i tab = _mm_set1_epi8('\t');
    __m128i newline = _mm_set1_epi8('\n');
    __m128i carriage = _mm_set1_epi8('\r');

    /* Process 16 bytes at a time */
    while (p + SIMD_VEC_SIZE <= end) {
        __m128i chunk = _mm_loadu_si128((__m128i*)p);

        /* Compare with all whitespace chars */
        __m128i is_space = _mm_cmpeq_epi8(chunk, space);
        __m128i is_tab = _mm_cmpeq_epi8(chunk, tab);
        __m128i is_newline = _mm_cmpeq_epi8(chunk, newline);
        __m128i is_carriage = _mm_cmpeq_epi8(chunk, carriage);

        /* Combine all whitespace matches */
        __m128i is_ws = _mm_or_si128(
            _mm_or_si128(is_space, is_tab),
            _mm_or_si128(is_newline, is_carriage)
        );

        /* Find first non-whitespace */
        int mask = _mm_movemask_epi8(is_ws);
        if (mask != 0xFFFF) {  /* Found non-whitespace */
            /* Count leading whitespace bytes */
            int leading = __builtin_ctz(~mask & 0xFFFF);
            return p + leading;
        }

        p += SIMD_VEC_SIZE;
    }

#elif defined(TAURUS_SIMD_NEON)
    /* NEON path - ARM64 */
    uint8x16_t space = vdupq_n_u8(' ');
    uint8x16_t tab = vdupq_n_u8('\t');
    uint8x16_t newline = vdupq_n_u8('\n');
    uint8x16_t carriage = vdupq_n_u8('\r');

    /* Process 16 bytes at a time */
    while (p + SIMD_VEC_SIZE <= end) {
        uint8x16_t chunk = vld1q_u8((uint8_t*)p);

        /* Compare with all whitespace chars */
        uint8x16_t is_space = vceqq_u8(chunk, space);
        uint8x16_t is_tab = vceqq_u8(chunk, tab);
        uint8x16_t is_newline = vceqq_u8(chunk, newline);
        uint8x16_t is_carriage = vceqq_u8(chunk, carriage);

        /* Combine all whitespace matches */
        uint8x16_t is_ws = vorrq_u8(
            vorrq_u8(is_space, is_tab),
            vorrq_u8(is_newline, is_carriage)
        );

        /* Check if we have any non-whitespace */
        uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(vmovn_u16(
            vreinterpretq_u16_u8(is_ws))), 0);

        if (mask != 0xFFFFFFFFFFFFFFFFULL) {  /* Found non-whitespace */
            /* Fall back to scalar for this chunk */
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
                p++;
            }
            return p;
        }

        p += SIMD_VEC_SIZE;
    }
#endif

    /* Scalar fallback for remaining bytes (< 16) or unsupported platforms */
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }

    return p;
}

/* ==================================================================
 * SIMD NAME CHARACTER CLASSIFICATION
 * =================================================================
 * Validates XML name characters: a-z, A-Z, 0-9, _, -, ., :
 * Uses SIMD range checks for massive speedup.
 */

inline static int simd_is_name_char(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '-' || c == '.' || c == ':';
}

/* Scan for end of name using SIMD */
inline static const char* simd_scan_name(const char* start, const char* end) {
    const char* p = start;

#if defined(TAURUS_SIMD_SSE2)
    /* SSE2 path - Check ranges in parallel */
    __m128i lower_a = _mm_set1_epi8('a' - 1);
    __m128i upper_z = _mm_set1_epi8('z' + 1);
    __m128i lower_A = _mm_set1_epi8('A' - 1);
    __m128i upper_Z = _mm_set1_epi8('Z' + 1);
    __m128i lower_0 = _mm_set1_epi8('0' - 1);
    __m128i upper_9 = _mm_set1_epi8('9' + 1);
    __m128i underscore = _mm_set1_epi8('_');
    __m128i dash = _mm_set1_epi8('-');
    __m128i dot = _mm_set1_epi8('.');
    __m128i colon = _mm_set1_epi8(':');

    while (p + SIMD_VEC_SIZE <= end) {
        __m128i chunk = _mm_loadu_si128((__m128i*)p);

        /* Check ranges: a-z, A-Z, 0-9 */
        __m128i is_lower = _mm_and_si128(
            _mm_cmpgt_epi8(chunk, lower_a),
            _mm_cmplt_epi8(chunk, upper_z)
        );
        __m128i is_upper = _mm_and_si128(
            _mm_cmpgt_epi8(chunk, lower_A),
            _mm_cmplt_epi8(chunk, upper_Z)
        );
        __m128i is_digit = _mm_and_si128(
            _mm_cmpgt_epi8(chunk, lower_0),
            _mm_cmplt_epi8(chunk, upper_9)
        );

        /* Check special chars: _, -, ., : */
        __m128i is_underscore = _mm_cmpeq_epi8(chunk, underscore);
        __m128i is_dash = _mm_cmpeq_epi8(chunk, dash);
        __m128i is_dot = _mm_cmpeq_epi8(chunk, dot);
        __m128i is_colon = _mm_cmpeq_epi8(chunk, colon);

        /* Combine all valid character checks */
        __m128i is_valid = _mm_or_si128(
            _mm_or_si128(
                _mm_or_si128(is_lower, is_upper),
                _mm_or_si128(is_digit, is_underscore)
            ),
            _mm_or_si128(
                _mm_or_si128(is_dash, is_dot),
                is_colon
            )
        );

        /* Find first invalid character */
        int mask = _mm_movemask_epi8(is_valid);
        if (mask != 0xFFFF) {  /* Found invalid char */
            int valid_count = __builtin_ctz(~mask & 0xFFFF);
            return p + valid_count;
        }

        p += SIMD_VEC_SIZE;
    }

#elif defined(TAURUS_SIMD_NEON)
    /* NEON path - ARM64 */
    uint8x16_t lower_a = vdupq_n_u8('a' - 1);
    uint8x16_t upper_z = vdupq_n_u8('z');
    uint8x16_t lower_A = vdupq_n_u8('A' - 1);
    uint8x16_t upper_Z = vdupq_n_u8('Z');
    uint8x16_t lower_0 = vdupq_n_u8('0' - 1);
    uint8x16_t upper_9 = vdupq_n_u8('9');
    uint8x16_t underscore = vdupq_n_u8('_');
    uint8x16_t dash = vdupq_n_u8('-');
    uint8x16_t dot = vdupq_n_u8('.');
    uint8x16_t colon = vdupq_n_u8(':');

    while (p + SIMD_VEC_SIZE <= end) {
        uint8x16_t chunk = vld1q_u8((uint8_t*)p);

        /* Check ranges */
        uint8x16_t is_lower = vandq_u8(
            vcgtq_u8(chunk, lower_a),
            vcleq_u8(chunk, upper_z)
        );
        uint8x16_t is_upper = vandq_u8(
            vcgtq_u8(chunk, lower_A),
            vcleq_u8(chunk, upper_Z)
        );
        uint8x16_t is_digit = vandq_u8(
            vcgtq_u8(chunk, lower_0),
            vcleq_u8(chunk, upper_9)
        );

        /* Check special chars */
        uint8x16_t is_underscore = vceqq_u8(chunk, underscore);
        uint8x16_t is_dash = vceqq_u8(chunk, dash);
        uint8x16_t is_dot = vceqq_u8(chunk, dot);
        uint8x16_t is_colon = vceqq_u8(chunk, colon);

        /* Combine all checks */
        uint8x16_t is_valid = vorrq_u8(
            vorrq_u8(
                vorrq_u8(is_lower, is_upper),
                vorrq_u8(is_digit, is_underscore)
            ),
            vorrq_u8(
                vorrq_u8(is_dash, is_dot),
                is_colon
            )
        );

        /* Check if all bytes are valid name chars */
        uint64_t low = vgetq_lane_u64(vreinterpretq_u64_u8(is_valid), 0);
        uint64_t high = vgetq_lane_u64(vreinterpretq_u64_u8(is_valid), 1);

        if (~(low & high)) {  /* Found invalid char */
            /* Fall back to scalar for exact position */
            break;
        }

        p += SIMD_VEC_SIZE;
    }
#endif

    /* Scalar finish for remaining bytes or on non-SIMD platforms */
    while (p < end && simd_is_name_char(*p)) {
        p++;
    }

    return p;
}

/* ==================================================================
 * SIMD STRING COMPARISON
 * =================================================================
 * Fast memcmp using SIMD for common short string comparisons.
 * Optimized for typical XML name/namespace lookups (4-20 chars).
 */

inline static int simd_memcmp(const char* s1, const char* s2, size_t n) {
    /* For very short strings, scalar is faster (branch prediction) */
    if (n < 8) {
        return memcmp(s1, s2, n);
    }

#if defined(TAURUS_SIMD_SSE2)
    /* SSE2 path */
    const char *p1 = s1, *p2 = s2;
    size_t remaining = n;

    while (remaining >= SIMD_VEC_SIZE) {
        __m128i v1 = _mm_loadu_si128((__m128i*)p1);
        __m128i v2 = _mm_loadu_si128((__m128i*)p2);
        __m128i cmp = _mm_cmpeq_epi8(v1, v2);

        int mask = _mm_movemask_epi8(cmp);
        if (mask != 0xFFFF) {  /* Found difference */
            /* Find first differing byte */
            int diff_pos = __builtin_ctz(~mask & 0xFFFF);
            return (unsigned char)p1[diff_pos] - (unsigned char)p2[diff_pos];
        }

        p1 += SIMD_VEC_SIZE;
        p2 += SIMD_VEC_SIZE;
        remaining -= SIMD_VEC_SIZE;
    }

    /* Handle remaining bytes */
    return memcmp(p1, p2, remaining);

#elif defined(TAURUS_SIMD_NEON)
    /* NEON path */
    const char *p1 = s1, *p2 = s2;
    size_t remaining = n;

    while (remaining >= SIMD_VEC_SIZE) {
        uint8x16_t v1 = vld1q_u8((uint8_t*)p1);
        uint8x16_t v2 = vld1q_u8((uint8_t*)p2);
        uint8x16_t cmp = vceqq_u8(v1, v2);

        /* Check if all bytes match */
        uint64_t low = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 0);
        uint64_t high = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 1);

        if ((low & high) != 0xFFFFFFFFFFFFFFFFULL) {  /* Found difference */
            return memcmp(p1, p2, SIMD_VEC_SIZE);
        }

        p1 += SIMD_VEC_SIZE;
        p2 += SIMD_VEC_SIZE;
        remaining -= SIMD_VEC_SIZE;
    }

    return memcmp(p1, p2, remaining);
#else
    /* Scalar fallback */
    return memcmp(s1, s2, n);
#endif
}

/* ==================================================================
 * SIMD PATTERN MATCHING
 * =================================================================
 * Fast pattern matching for "xmlns" prefix detection.
 * Uses SIMD to check multiple bytes simultaneously.
 */

inline static int simd_starts_with_xmlns(const char* s) {
    /* Quick scalar check for common non-xmlns cases */
    if (s[0] != 'x') return 0;

#if defined(TAURUS_SIMD_SSE2) || defined(TAURUS_SIMD_NEON)
    /* Load first 8 bytes (includes "xmlns" and potential ":") */
    /* We know s[0] == 'x', so just check remaining "mlns" */
    if (s[1] == 'm' && s[2] == 'l' && s[3] == 'n' && s[4] == 's') {
        /* Check for xmlns or xmlns: */
        return (s[5] == '\0' || s[5] == ':');
    }
    return 0;
#else
    /* Scalar path */
    return (s[0] == 'x' && s[1] == 'm' && s[2] == 'l' &&
            s[3] == 'n' && s[4] == 's' &&
            (s[5] == '\0' || s[5] == ':'));
#endif
}

/* ==================================================================
 * SIMD STRING EQUALITY
 * =================================================================
 * Fast string equality check for namespace prefix matching.
 */

inline static int simd_streq(const char* s1, const char* s2, size_t len) {
    /* For very short strings, direct comparison is faster */
    if (len <= 4) {
        switch (len) {
            case 0: return 1;
            case 1: return s1[0] == s2[0];
            case 2: return s1[0] == s2[0] && s1[1] == s2[1];
            case 3: return s1[0] == s2[0] && s1[1] == s2[1] && s1[2] == s2[2];
            case 4: return *((uint32_t*)s1) == *((uint32_t*)s2);
        }
    }

    return simd_memcmp(s1, s2, len) == 0;
}

/* ==================================================================
 * SIMD FIRST CHARACTER SCANNING
 * =================================================================
 * Find first occurrence of character using SIMD.
 * Faster than strchr() for short strings.
 */

inline static const char* simd_find_char(const char* s, const char* end, char target) {
    const char* p = s;

#if defined(TAURUS_SIMD_SSE2)
    __m128i target_vec = _mm_set1_epi8(target);

    while (p + SIMD_VEC_SIZE <= end) {
        __m128i chunk = _mm_loadu_si128((__m128i*)p);
        __m128i cmp = _mm_cmpeq_epi8(chunk, target_vec);

        int mask = _mm_movemask_epi8(cmp);
        if (mask != 0) {  /* Found target */
            int pos = __builtin_ctz(mask);
            return p + pos;
        }

        p += SIMD_VEC_SIZE;
    }

#elif defined(TAURUS_SIMD_NEON)
    uint8x16_t target_vec = vdupq_n_u8((uint8_t)target);

    while (p + SIMD_VEC_SIZE <= end) {
        uint8x16_t chunk = vld1q_u8((uint8_t*)p);
        uint8x16_t cmp = vceqq_u8(chunk, target_vec);

        /* Check if we found the target */
        uint64_t low = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 0);
        uint64_t high = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 1);

        if (low || high) {  /* Found target somewhere */
            /* Fall back to scalar for exact position */
            while (p < end && *p != target) {
                p++;
            }
            return (p < end) ? p : NULL;
        }

        p += SIMD_VEC_SIZE;
    }
#endif

    /* Scalar finish */
    while (p < end && *p != target) {
        p++;
    }

    return (p < end) ? p : NULL;
}

#endif /* TAURUS_SIMD_HELPERS_H */