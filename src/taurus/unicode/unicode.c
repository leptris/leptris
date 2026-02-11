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
bool taurus_unicode_validate_utf8(const char* str, size_t len) {
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
int taurus_unicode_strlen(const char* str, size_t len) {
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
char* taurus_unicode_normalize(const char* str, size_t len,
                                taurus_unicode_normalization_t form,
                                size_t* out_len) {
    if (!str || !out_len) {
        return NULL;
    }
    
    utf8proc_option_t options = UTF8PROC_STABLE;
    
    switch (form) {
        case TAURUS_UNICODE_NFC:
            options |= UTF8PROC_COMPOSE;
            break;
        case TAURUS_UNICODE_NFD:
            options |= UTF8PROC_DECOMPOSE;
            break;
        case TAURUS_UNICODE_NFKC:
            options |= UTF8PROC_COMPOSE | UTF8PROC_COMPAT;
            break;
        case TAURUS_UNICODE_NFKD:
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
char* taurus_unicode_to_upper(const char* str, size_t len, size_t* out_len) {
    if (!str || !out_len) {
        return NULL;
    }
    
    utf8proc_uint8_t* result = NULL;
    utf8proc_ssize_t result_len = utf8proc_map(
        (const utf8proc_uint8_t*)str,
        (utf8proc_ssize_t)len,
        &result,
        UTF8PROC_STABLE | UTF8PROC_CASEFOLD | UTF8PROC_COMPOSE
    );
    
    if (result_len < 0) {
        return NULL;
    }
    
    *out_len = (size_t)result_len;
    return (char*)result;
}

/**
 * Convert UTF-8 string to lowercase
 */
char* taurus_unicode_to_lower(const char* str, size_t len, size_t* out_len) {
    if (!str || !out_len) {
        return NULL;
    }
    
    utf8proc_uint8_t* result = NULL;
    utf8proc_ssize_t result_len = utf8proc_map(
        (const utf8proc_uint8_t*)str,
        (utf8proc_ssize_t)len,
        &result,
        UTF8PROC_STABLE | UTF8PROC_CASEFOLD | UTF8PROC_COMPOSE
    );
    
    if (result_len < 0) {
        return NULL;
    }
    
    *out_len = (size_t)result_len;
    return (char*)result;
}

/**
 * Case-insensitive comparison of two UTF-8 strings
 */
int taurus_unicode_casecmp(const char* str1, size_t len1,
                           const char* str2, size_t len2) {
    if (!str1 || !str2) {
        return (str1 == str2) ? 0 : (str1 ? 1 : -1);
    }
    
    /* Normalize both strings to lowercase for comparison */
    size_t norm1_len, norm2_len;
    char* norm1 = taurus_unicode_to_lower(str1, len1, &norm1_len);
    char* norm2 = taurus_unicode_to_lower(str2, len2, &norm2_len);
    
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
bool taurus_unicode_is_whitespace(int codepoint) {
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
int taurus_unicode_next_codepoint(const char** str, size_t* len) {
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