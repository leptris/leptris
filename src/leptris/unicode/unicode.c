/**
 * @file unicode.c
 * @brief Unicode support implementation using utf8proc
 */

#include "unicode.h"
#include <utf8proc.h>
#include <stdlib.h>
#include <string.h>

/**
 * Validate UTF-8 string
 */
bool leptris_unicode_validate_utf8(const char* str, size_t len) {
    if (!str) {
        return false;
    }

    const uint8_t* p = (const uint8_t*)str;
    const uint8_t* end = p + len;

    while (p < end) {
        utf8proc_int32_t codepoint;
        utf8proc_ssize_t bytes = utf8proc_iterate(p, end - p, &codepoint);

        if (bytes < 0) {
            return false;  /* Invalid UTF-8 sequence */
        }

        p += bytes;
    }

    return true;
}

/**
 * Get the number of Unicode codepoints in a UTF-8 string
 */
int leptris_unicode_strlen(const char* str, size_t len) {
    if (!str) {
        return -1;
    }

    const uint8_t* p = (const uint8_t*)str;
    const uint8_t* end = p + len;
    int count = 0;

    while (p < end) {
        utf8proc_int32_t codepoint;
        utf8proc_ssize_t bytes = utf8proc_iterate(p, end - p, &codepoint);

        if (bytes < 0) {
            return -1;  /* Invalid UTF-8 */
        }

        p += bytes;
        count++;
    }

    return count;
}

/**
 * Normalize a UTF-8 string
 */
char* leptris_unicode_normalize(const char* str, size_t len,
                                leptris_unicode_normalization_t form,
                                size_t* out_len) {
    if (!str || !out_len) {
        return NULL;
    }

    utf8proc_option_t options = UTF8PROC_STABLE;

    switch (form) {
        case LEPTRIS_UNICODE_NFC:
            options |= UTF8PROC_COMPOSE;
            break;
        case LEPTRIS_UNICODE_NFD:
            options |= UTF8PROC_DECOMPOSE;
            break;
        case LEPTRIS_UNICODE_NFKC:
            options |= UTF8PROC_COMPOSE | UTF8PROC_COMPAT;
            break;
        case LEPTRIS_UNICODE_NFKD:
            options |= UTF8PROC_DECOMPOSE | UTF8PROC_COMPAT;
            break;
        default:
            return NULL;
    }

    utf8proc_uint8_t* result = NULL;
    utf8proc_ssize_t result_len = utf8proc_map(
        (const utf8proc_uint8_t*)str,
        (utf8proc_ssize_t)len,
        &result,
        options
    );

    if (result_len < 0) {
        return NULL;
    }

    *out_len = (size_t)result_len;
    return (char*)result;
}

/**
 * Convert UTF-8 string to uppercase
 */
/* Unicode CASE MAPPING (F&O upper-case/lower-case): per-codepoint
 * utf8proc_toupper/tolower — NOT case folding. The previous
 * UTF8PROC_CASEFOLD map folded to the canonical (lowercase-shaped)
 * fold form, which is neither operation and left fn:upper-case
 * wrong for every non-ASCII input (fn-upper-case-20: U+01CB must
 * map to U+01CA). Case mapping is codepoint-wise in utf8proc's
 * property API; encode the mapped sequence back to UTF-8. The
 * mapped form of a BMP codepoint is <= 3 bytes, so a 4-byte worst
 * case per input byte is a safe bound (surrogates never appear in
 * valid UTF-8). */
char* leptris_unicode_to_upper(const char* str, size_t len, size_t* out_len) {
    if (!str || !out_len) {
        return NULL;
    }

    char* out = (char*)malloc(len * 4 + 1);
    if (!out) return NULL;
    size_t w = 0;
    for (size_t i = 0; i < len; ) {
        utf8proc_int32_t cp;
        utf8proc_ssize_t used = utf8proc_iterate(
            (const utf8proc_uint8_t*)str + i,
            (utf8proc_ssize_t)(len - i), &cp);
        if (used < 1) { free(out); return NULL; }
        cp = utf8proc_toupper(cp);
        utf8proc_ssize_t written = utf8proc_encode_char(
            cp, (utf8proc_uint8_t*)out + w);
        if (written < 1) { free(out); return NULL; }
        w += (size_t)written;
        i += (size_t)used;
    }
    out[w] = 0;
    *out_len = w;
    return out;
}

/**
 * Convert UTF-8 string to lowercase
 */
char* leptris_unicode_to_lower(const char* str, size_t len, size_t* out_len) {
    if (!str || !out_len) {
        return NULL;
    }

    char* out = (char*)malloc(len * 4 + 1);
    if (!out) return NULL;
    size_t w = 0;
    for (size_t i = 0; i < len; ) {
        utf8proc_int32_t cp;
        utf8proc_ssize_t used = utf8proc_iterate(
            (const utf8proc_uint8_t*)str + i,
            (utf8proc_ssize_t)(len - i), &cp);
        if (used < 1) { free(out); return NULL; }
        cp = utf8proc_tolower(cp);
        utf8proc_ssize_t written = utf8proc_encode_char(
            cp, (utf8proc_uint8_t*)out + w);
        if (written < 1) { free(out); return NULL; }
        w += (size_t)written;
        i += (size_t)used;
    }
    out[w] = 0;
    *out_len = w;
    return out;
}

/**
 * Case-insensitive comparison of two UTF-8 strings
 */
int leptris_unicode_casecmp(const char* str1, size_t len1,
                           const char* str2, size_t len2) {
    if (!str1 || !str2) {
        return (str1 == str2) ? 0 : (str1 ? 1 : -1);
    }

    /* Fold both strings for comparison — case folding, NOT
     * lower-case mapping: the fold form is the canonical
     * case-insensitive identity (the old to_lower did this
     * implicitly; true case mapping is not a substitute). */
    size_t norm1_len, norm2_len;
    utf8proc_uint8_t *f1 = NULL, *f2 = NULL;
    utf8proc_ssize_t n1 = utf8proc_map(
        (const utf8proc_uint8_t*)str1, (utf8proc_ssize_t)len1, &f1,
        UTF8PROC_STABLE | UTF8PROC_CASEFOLD | UTF8PROC_COMPOSE);
    utf8proc_ssize_t n2 = utf8proc_map(
        (const utf8proc_uint8_t*)str2, (utf8proc_ssize_t)len2, &f2,
        UTF8PROC_STABLE | UTF8PROC_CASEFOLD | UTF8PROC_COMPOSE);
    if (n1 < 0 || n2 < 0) {
        free(f1); free(f2);
        return (str1 == str2) ? 0 : 1;
    }
    char* norm1 = (char*)f1;
    char* norm2 = (char*)f2;
    (void)0;

    if (!norm1 || !norm2) {
        free(norm1);
        free(norm2);
        return -1;
    }

    /* Compare normalized strings */
    size_t min_len = (norm1_len < norm2_len) ? norm1_len : norm2_len;
    int result = memcmp(norm1, norm2, min_len);

    if (result == 0) {
        /* If prefixes match, compare lengths */
        if (norm1_len < norm2_len) {
            result = -1;
        } else if (norm1_len > norm2_len) {
            result = 1;
        }
    }

    free(norm1);
    free(norm2);

    return result;
}

/**
 * Check if a codepoint is whitespace
 */
bool leptris_unicode_is_whitespace(int codepoint) {
    const utf8proc_property_t* prop = utf8proc_get_property(codepoint);
    if (!prop) {
        return false;
    }

    /* Check for various whitespace categories */
    utf8proc_category_t cat = prop->category;
    return (cat == UTF8PROC_CATEGORY_ZS ||  /* Space separator */
            cat == UTF8PROC_CATEGORY_ZL ||  /* Line separator */
            cat == UTF8PROC_CATEGORY_ZP ||  /* Paragraph separator */
            codepoint == 0x09 ||            /* TAB */
            codepoint == 0x0A ||            /* LF */
            codepoint == 0x0B ||            /* VT */
            codepoint == 0x0C ||            /* FF */
            codepoint == 0x0D);             /* CR */
}

/**
 * Get the next UTF-8 codepoint from a string
 */
int leptris_unicode_next_codepoint(const char** str, size_t* len) {
    if (!str || !*str || !len || *len == 0) {
        return -1;
    }

    const uint8_t* p = (const uint8_t*)*str;
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t bytes = utf8proc_iterate(p, *len, &codepoint);

    if (bytes < 0) {
        return -1;  /* Invalid UTF-8 */
    }

    /* Advance pointer and decrement length */
    *str += bytes;
    *len -= bytes;

    return (int)codepoint;
}