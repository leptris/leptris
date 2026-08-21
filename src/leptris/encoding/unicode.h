/* unicode.h - Unicode Conversion Functions
 * Copyright (c) 2025, Ribose Inc.
 *
 * Provides UTF-8 to UTF-16/UTF-32 conversion functions.
 */

#ifndef UNICODE_H
#define UNICODE_H

#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * UTF-8 to UTF-16 Conversion (as_wide equivalent)
 * ============================================================================ */

/**
 * Convert UTF-8 string to UTF-16 string
 *
 * @param utf8_str Input UTF-8 string (null-terminated)
 * @param out_utf16 Output pointer for UTF-16 string (allocated, must free with leptris_utf16_free)
 * @return Number of UTF-16 code units, or 0 on error
 *
 * Note: For code points beyond BMP (U+10000 to U+10FFFF), uses surrogate pairs
 * Invalid UTF-8 sequences are replaced with U+FFFD (replacement character)
 *
 * Memory: Caller must call leptris_utf16_free() on the returned string
 */
size_t leptris_utf8_to_utf16(const char* utf8_str, uint16_t** out_utf16);

/**
 * Free UTF-16 string allocated by leptris_utf8_to_utf16
 *
 * @param utf16_str UTF-16 string to free (can be NULL)
 */
void leptris_utf16_free(uint16_t* utf16_str);

/* ============================================================================
 * UTF-16 to UTF-8 Conversion (as_utf8 equivalent)
 * ============================================================================ */

/**
 * Convert UTF-16 string to UTF-8 string
 *
 * @param utf16_str Input UTF-16 string
 * @param utf16_len Number of UTF-16 code units
 * @param out_utf8 Output pointer for UTF-8 string (allocated, must free with leptris_utf8_free)
 * @return Number of UTF-8 bytes, or 0 on error
 *
 * Note: Handles surrogate pairs for code points beyond BMP
 * Isolated surrogates are replaced with U+FFFD (replacement character)
 *
 * Memory: Caller must call leptris_utf8_free() on the returned string
 */
size_t leptris_utf16_to_utf8(const uint16_t* utf16_str, size_t utf16_len, char** out_utf8);

/**
 * Free UTF-8 string allocated by leptris_utf16_to_utf8
 *
 * @param utf8_str UTF-8 string to free (can be NULL)
 */
void leptris_utf8_free(char* utf8_str);

#endif /* UNICODE_H */
