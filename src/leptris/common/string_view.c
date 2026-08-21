#include "../memory/pool.h"
#include "string_view.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ============================================================================
 * Pointer Validation - Detects obviously corrupted pointers
 * ============================================================================ */

/* ============================================================================
 * SIMD Support Detection and Includes
 * ============================================================================ */

/* Detect platform and include appropriate SIMD headers */
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    #define LEPTRIS_HAS_NEON 1
    #include <arm_neon.h>
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #define LEPTRIS_HAS_SSE2 1
    #include <emmintrin.h>
#else
    #define LEPTRIS_HAS_SIMD 0
#endif

/* ============================================================================
 * SIMD-Optimized String Comparison
 * ============================================================================ */

/**
 * SIMD-accelerated memory comparison (16-byte at a time)
 *
 * Uses NEON (ARM) or SSE2 (x86) for 8-way parallel comparison.
 * Falls back to memcmp for remaining bytes.
 *
 * @param a First buffer
 * @param b Second buffer
 * @param len Length to compare (must be > 0)
 * @return 1 if equal, 0 if different
 */
static inline int leptris_simd_memcmp(const char* a, const char* b, size_t len) {
    /* Process 16 bytes at a time using SIMD */
    size_t i = 0;

#if defined(LEPTRIS_HAS_NEON)
    /* ARM NEON implementation - 16 bytes per iteration */
    for (; i + 16 <= len; i += 16) {
        /* Load 16 bytes from each buffer */
        uint8x16_t va = vld1q_u8((const uint8_t*)(a + i));
        uint8x16_t vb = vld1q_u8((const uint8_t*)(b + i));

        /* Compare all 16 bytes */
        uint8x16_t cmp = vceqq_u8(va, vb);

        /* Check if all bytes are equal (all bits set) */
        if (vminvq_u8(cmp) == 0) {
            return 0;  /* Not equal */
        }
    }
#elif defined(LEPTRIS_HAS_SSE2)
    /* x86 SSE2 implementation - 16 bytes per iteration */
    for (; i + 16 <= len; i += 16) {
        /* Load 16 bytes from each buffer */
        __m128i va = _mm_loadu_si128((const __m128i*)(a + i));
        __m128i vb = _mm_loadu_si128((const __m128i*)(b + i));

        /* Compare all 16 bytes */
        __m128i cmp = _mm_cmpeq_epi8(va, vb);

        /* Check if all bytes are equal (all bits set) */
        if (_mm_movemask_epi8(cmp) != 0xFFFF) {
            return 0;  /* Not equal */
        }
    }
#endif

    /* Handle remaining bytes (0-15 bytes) */
    if (i < len) {
        return memcmp(a + i, b + i, len - i) == 0;
    }

    return 1;  /* All bytes equal */
}

/* Creation from pointer + length */
/* Creation from NULL-terminated C string */
/* Create empty StringView */
/* Query: Check if empty */
int leptris_sv_is_empty(const LeptrisStringView* sv) {
    return !sv || !sv->data || sv->length == 0;
}

/* Query: Get length */
size_t leptris_sv_length(const LeptrisStringView* sv) {
    return sv ? sv->length : 0;
}

/* Comparison: Compare two StringViews (SIMD-accelerated) */
int leptris_sv_equals(const LeptrisStringView* a, const LeptrisStringView* b) {
    if (!a || !b) return 0;
    if (a->length != b->length) return 0;
    if (a->length == 0) return 1;

    /* Use SIMD-accelerated comparison for longer strings */
    if (a->length >= 16) {
        return leptris_simd_memcmp(a->data, b->data, a->length);
    }

    /* For short strings, use memcmp (faster due to no SIMD overhead) */
    return memcmp(a->data, b->data, a->length) == 0;
}

/* Comparison: Compare StringView with C string (SIMD-accelerated) */
int leptris_sv_equals_cstr(const LeptrisStringView* sv, const char* str) {
    if (!sv || !str) return 0;
    size_t str_len = strlen(str);
    if (sv->length != str_len) return 0;
    if (sv->length == 0) return 1;

    /* Use SIMD-accelerated comparison for longer strings */
    if (sv->length >= 16) {
        return leptris_simd_memcmp(sv->data, str, str_len);
    }

    /* For short strings, use memcmp */
    return memcmp(sv->data, str, str_len) == 0;
}

/* Hash: FNV-1a hash algorithm (fast, good distribution) */
size_t leptris_sv_hash(const LeptrisStringView* sv) {
    if (!sv || !sv->data) return 0;

    const size_t FNV_offset_basis = 14695981039346656037ULL;
    const size_t FNV_prime = 1099511628211ULL;

    size_t hash = FNV_offset_basis;
    for (size_t i = 0; i < sv->length; i++) {
        hash ^= (unsigned char)sv->data[i];
        hash *= FNV_prime;
    }
    return hash;
}

/* Convert to NULL-terminated C string (allocates!) */
char* leptris_sv_to_cstr(const LeptrisStringView* sv) {
    if (!sv || sv->length == 0 || !sv->data) {
        return strdup("");
    }

    char* str = malloc(sv->length + 1);
    if (!str) return NULL;

    memcpy(str, sv->data, sv->length);
    str[sv->length] = '\0';
    return str;
}

/* Convert to NULL-terminated C string using pool (fast!) */
char* leptris_sv_to_cstr_pooled(const LeptrisStringView* sv, LeptrisMemoryPool* pool) {
    if (!sv || sv->length == 0) {
        /* For empty strings, allocate from pool if available */
        if (pool) {
            char* str = (char*)leptris_pool_alloc(pool, 1);
            if (str) str[0] = '\0';
            return str;
        }
        return strdup("");
    }

    if (!pool) {
        /* No pool - fall back to malloc */
        return leptris_sv_to_cstr(sv);
    }

    /* Use string interning if pool has hash table (deduplication!) */
    if (pool->string_cache) {
        return leptris_pool_intern_string(pool, sv);
    }

    /* No hash table - allocate directly from pool (no deduplication) */
    char* str = (char*)leptris_pool_alloc(pool, sv->length + 1);
    if (!str) return NULL;

    memcpy(str, sv->data, sv->length);
    str[sv->length] = '\0';
    return str;
}

/* Copy to buffer with NULL-termination */
size_t leptris_sv_to_buffer(const LeptrisStringView* sv, char* buf, size_t buf_size) {
    if (!sv || !buf || buf_size == 0) return 0;
    if (sv->length == 0) {
        buf[0] = '\0';
        return 0;
    }

    size_t copy_len = sv->length < buf_size - 1 ? sv->length : buf_size - 1;
    memcpy(buf, sv->data, copy_len);
    buf[copy_len] = '\0';
    return copy_len;
}