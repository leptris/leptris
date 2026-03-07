/**
 * @file escape.c
 * @brief Entity escaping with pre-computed lookup tables
 *
 * Implements branch-free entity escaping using lookup tables.
 * Each byte value maps to either:
 * - NULL (no escape needed)
 * - Entity string (&lt;, &gt;, &amp;, etc.)
 *
 * Performance techniques from Woodstox:
 * 1. Pre-computed lookup table (branch-free)
 * 2. SIMD-optimized escape scanning (ARM NEON / x86 SSE2)
 * 3. Block copy for safe character runs
 */

#include "writer_internal.h"
#include <string.h>
#include <stdint.h>

/* ============================================================================
 * Escape Lookup Tables
 * ============================================================================ */

/**
 * Text content escape table
 *
 * Index by character value (0-255).
 * Only escapes <, >, and &
 */
const EscapeEntry escape_table_text[256] = {
    /* 0x00-0x08: Control chars -> &#xNN; (for completeness) */
    [0x00] = {"&#x0;", 5},  [0x01] = {"&#x1;", 5},  [0x02] = {"&#x2;", 5},
    [0x03] = {"&#x3;", 5},  [0x04] = {"&#x4;", 5},  [0x05] = {"&#x5;", 5},
    [0x06] = {"&#x6;", 5},  [0x07] = {"&#x7;", 5},  [0x08] = {"&#x8;", 5},

    /* 0x09: Tab - no escape */
    [0x09] = {NULL, 0},

    /* 0x0A: Newline - no escape */
    [0x0A] = {NULL, 0},

    /* 0x0B-0x0C: Control chars */
    [0x0B] = {"&#xB;", 5}, [0x0C] = {"&#xC;", 5},

    /* 0x0D: CR - may escape based on config */
    [0x0D] = {"&#xD;", 5},

    /* 0x0E-0x1F: Control chars */
    [0x0E] = {"&#xE;", 5}, [0x0F] = {"&#xF;", 5}, [0x10] = {"&#x10;", 6},
    [0x11] = {"&#x11;", 6}, [0x12] = {"&#x12;", 6}, [0x13] = {"&#x13;", 6},
    [0x14] = {"&#x14;", 6}, [0x15] = {"&#x15;", 6}, [0x16] = {"&#x16;", 6},
    [0x17] = {"&#x17;", 6}, [0x18] = {"&#x18;", 6}, [0x19] = {"&#x19;", 6},
    [0x1A] = {"&#x1A;", 6}, [0x1B] = {"&#x1B;", 6}, [0x1C] = {"&#x1C;", 6},
    [0x1D] = {"&#x1D;", 6}, [0x1E] = {"&#x1E;", 6}, [0x1F] = {"&#x1F;", 6},

    /* Special XML characters */
    ['<']  = {"&lt;", 4},
    ['>']  = {"&gt;", 4},
    ['&']  = {"&amp;", 5},

    /* Rest are NULL (no escape) - C99 designated initializer fills gaps with 0 */
};

/**
 * Attribute value escape table
 *
 * Also escapes " and '
 */
const EscapeEntry escape_table_attr[256] = {
    /* Control chars (same as text table) */
    [0x00] = {"&#x0;", 5},  [0x01] = {"&#x1;", 5},  [0x02] = {"&#x2;", 5},
    [0x03] = {"&#x3;", 5},  [0x04] = {"&#x4;", 5},  [0x05] = {"&#x5;", 5},
    [0x06] = {"&#x6;", 5},  [0x07] = {"&#x7;", 5},  [0x08] = {"&#x8;", 5},
    [0x09] = {NULL, 0},
    [0x0A] = {NULL, 0},
    [0x0B] = {"&#xB;", 5}, [0x0C] = {"&#xC;", 5},
    [0x0D] = {"&#xD;", 5},
    [0x0E] = {"&#xE;", 5}, [0x0F] = {"&#xF;", 5}, [0x10] = {"&#x10;", 6},
    [0x11] = {"&#x11;", 6}, [0x12] = {"&#x12;", 6}, [0x13] = {"&#x13;", 6},
    [0x14] = {"&#x14;", 6}, [0x15] = {"&#x15;", 6}, [0x16] = {"&#x16;", 6},
    [0x17] = {"&#x17;", 6}, [0x18] = {"&#x18;", 6}, [0x19] = {"&#x19;", 6},
    [0x1A] = {"&#x1A;", 6}, [0x1B] = {"&#x1B;", 6}, [0x1C] = {"&#x1C;", 6},
    [0x1D] = {"&#x1D;", 6}, [0x1E] = {"&#x1E;", 6}, [0x1F] = {"&#x1F;", 6},

    /* Special XML characters */
    ['<']  = {"&lt;", 4},
    ['>']  = {"&gt;", 4},
    ['&']  = {"&amp;", 5},

    /* Attribute-specific: quote characters */
    ['"']  = {"&quot;", 6},
    ['\''] = {"&apos;", 6},
};

/* ============================================================================
 * SIMD Escape Detection (Optional)
 * ============================================================================ */

#if defined(__ARM_NEON)
#include <arm_neon.h>

/**
 * ARM NEON optimized escape position finder
 *
 * Scans 16 bytes at a time for escape characters.
 */
static size_t find_escape_pos_neon(const char* text, size_t len, int is_attr) {
    const uint8_t* p = (const uint8_t*)text;
    size_t i = 0;

    /* Create vectors for comparison */
    uint8x16_t lt = vdupq_n_u8('<');
    uint8x16_t gt = vdupq_n_u8('>');
    uint8x16_t amp = vdupq_n_u8('&');

    while (i + 16 <= len) {
        uint8x16_t chunk = vld1q_u8(p + i);

        /* Compare with escape characters */
        uint8x16_t eq_lt = vceqq_u8(chunk, lt);
        uint8x16_t eq_gt = vceqq_u8(chunk, gt);
        uint8x16_t eq_amp = vceqq_u8(chunk, amp);

        /* Combine results */
        uint8x16_t combined = vorrq_u8(vorrq_u8(eq_lt, eq_gt), eq_amp);

        /* Add attribute quotes if needed */
        if (is_attr) {
            uint8x16_t quot = vdupq_n_u8('"');
            uint8x16_t apos = vdupq_n_u8('\'');
            uint8x16_t eq_quot = vceqq_u8(chunk, quot);
            uint8x16_t eq_apos = vceqq_u8(chunk, apos);
            combined = vorrq_u8(combined, vorrq_u8(eq_quot, eq_apos));
        }

        /* Check if any matches */
        uint64x2_t result = vreinterpretq_u64_u8(combined);
        if (vgetq_lane_u64(result, 0) != 0 || vgetq_lane_u64(result, 1) != 0) {
            /* Found escape character, find exact position */
            for (size_t j = 0; j < 16; j++) {
                const EscapeEntry* table = is_attr ? escape_table_attr : escape_table_text;
                if (table[p[i + j]].entity != NULL) {
                    return i + j;
                }
            }
        }

        i += 16;
    }

    /* Check remaining bytes */
    const EscapeEntry* table = is_attr ? escape_table_attr : escape_table_text;
    for (; i < len; i++) {
        if (table[(uint8_t)p[i]].entity != NULL) {
            return i;
        }
    }

    return len;
}

#endif /* __ARM_NEON */

#if defined(__SSE2__)
#include <emmintrin.h>

/**
 * x86 SSE2 optimized escape position finder
 *
 * Scans 16 bytes at a time for escape characters.
 */
static size_t find_escape_pos_sse2(const char* text, size_t len, int is_attr) {
    const uint8_t* p = (const uint8_t*)text;
    size_t i = 0;

    /* Create vectors for comparison */
    __m128i lt = _mm_set1_epi8('<');
    __m128i gt = _mm_set1_epi8('>');
    __m128i amp = _mm_set1_epi8('&');

    while (i + 16 <= len) {
        __m128i chunk = _mm_loadu_si128((const __m128i*)(p + i));

        /* Compare with escape characters */
        __m128i eq_lt = _mm_cmpeq_epi8(chunk, lt);
        __m128i eq_gt = _mm_cmpeq_epi8(chunk, gt);
        __m128i eq_amp = _mm_cmpeq_epi8(chunk, amp);

        /* Combine results */
        __m128i combined = _mm_or_si128(_mm_or_si128(eq_lt, eq_gt), eq_amp);

        /* Add attribute quotes if needed */
        if (is_attr) {
            __m128i quot = _mm_set1_epi8('"');
            __m128i apos = _mm_set1_epi8('\'');
            __m128i eq_quot = _mm_cmpeq_epi8(chunk, quot);
            __m128i eq_apos = _mm_cmpeq_epi8(chunk, apos);
            combined = _mm_or_si128(combined, _mm_or_si128(eq_quot, eq_apos));
        }

        /* Check if any matches */
        int mask = _mm_movemask_epi8(combined);
        if (mask != 0) {
            /* Found escape character at first set bit */
            return i + __builtin_ctz(mask);
        }

        i += 16;
    }

    /* Check remaining bytes */
    const EscapeEntry* table = is_attr ? escape_table_attr : escape_table_text;
    for (; i < len; i++) {
        if (table[p[i]].entity != NULL) {
            return i;
        }
    }

    return len;
}

#endif /* __SSE2__ */

/* ============================================================================
 * Public Functions
 * ============================================================================ */

/**
 * Find position of first character needing escape
 *
 * Uses SIMD when available, falls back to scalar otherwise.
 */
size_t find_escape_pos(const char* text, size_t len, int is_attr) {
#if defined(__ARM_NEON)
    return find_escape_pos_neon(text, len, is_attr);
#elif defined(__SSE2__)
    return find_escape_pos_sse2(text, len, is_attr);
#else
    /* Scalar fallback */
    const EscapeEntry* table = is_attr ? escape_table_attr : escape_table_text;
    const uint8_t* p = (const uint8_t*)text;

    for (size_t i = 0; i < len; i++) {
        if (table[p[i]].entity != NULL) {
            return i;
        }
    }

    return len;
#endif
}

size_t calc_escaped_length(const char* text, size_t len, int is_attr) {
    if (!text || len == 0) return 0;

    const EscapeEntry* table = is_attr ? escape_table_attr : escape_table_text;
    const uint8_t* p = (const uint8_t*)text;
    size_t result = 0;

    for (size_t i = 0; i < len; i++) {
        const EscapeEntry* entry = &table[p[i]];
        if (entry->entity) {
            result += entry->len;
        } else {
            result += 1;
        }
    }

    return result;
}

void buffer_write_escaped(OutputBuffer* buf, const char* text, size_t len, int is_attr) {
    if (!buf || !text || len == 0 || buf->error) return;

    const EscapeEntry* table = is_attr ? escape_table_attr : escape_table_text;
    const uint8_t* p = (const uint8_t*)text;
    size_t i = 0;

    while (i < len) {
        /* Find start of safe run */
        size_t run_start = i;

        /* Scan for characters that need escaping */
        while (i < len && table[p[i]].entity == NULL) {
            i++;
        }

        /* Copy safe run in one block */
        if (i > run_start) {
            buffer_write_raw(buf, text + run_start, i - run_start);
            if (buf->error) return;
        }

        /* Handle escape character */
        if (i < len) {
            const EscapeEntry* entry = &table[p[i]];
            if (entry->entity) {
                buffer_write_raw(buf, entry->entity, entry->len);
            } else {
                /* Should not happen, but handle gracefully */
                buffer_write_char(buf, (char)p[i]);
            }
            i++;
        }
    }
}

void escape_tables_init(void) {
    /* Tables are statically initialized above.
     * This function is provided for future dynamic initialization if needed.
     */
}
