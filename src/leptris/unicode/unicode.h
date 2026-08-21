/**
 * @file unicode.h
 * @brief Unicode support via utf8proc
 *
 * Provides UTF-8 validation, normalization, and case conversion functions
 * using the utf8proc library.
 */

#ifndef LEPTRIS_UNICODE_H
#define LEPTRIS_UNICODE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Normalization forms
 */
typedef enum {
    LEPTRIS_UNICODE_NFC,   /**< Canonical Decomposition, followed by Canonical Composition */
    LEPTRIS_UNICODE_NFD,   /**< Canonical Decomposition */
    LEPTRIS_UNICODE_NFKC,  /**< Compatibility Decomposition, followed by Canonical Composition */
    LEPTRIS_UNICODE_NFKD   /**< Compatibility Decomposition */
} leptris_unicode_normalization_t;

/**
 * Validate UTF-8 string
 *
 * @param str String to validate
 * @param len Length of string in bytes
 * @return true if valid UTF-8, false otherwise
 */
bool leptris_unicode_validate_utf8(const char* str, size_t len);

/**
 * Get the number of Unicode codepoints in a UTF-8 string
 *
 * @param str UTF-8 encoded string
 * @param len Length of string in bytes
 * @return Number of codepoints, or -1 on error
 */
int leptris_unicode_strlen(const char* str, size_t len);

/**
 * Normalize a UTF-8 string
 *
 * @param str String to normalize
 * @param len Length of string in bytes
 * @param form Normalization form
 * @param out_len Output parameter for result length
 * @return Normalized string (must be freed by caller) or NULL on error
 */
char* leptris_unicode_normalize(const char* str, size_t len,
                                leptris_unicode_normalization_t form,
                                size_t* out_len);

/**
 * Convert UTF-8 string to uppercase
 *
 * @param str String to convert
 * @param len Length of string in bytes
 * @param out_len Output parameter for result length
 * @return Uppercase string (must be freed by caller) or NULL on error
 */
char* leptris_unicode_to_upper(const char* str, size_t len, size_t* out_len);

/**
 * Convert UTF-8 string to lowercase
 *
 * @param str String to convert
 * @param len Length of string in bytes
 * @param out_len Output parameter for result length
 * @return Lowercase string (must be freed by caller) or NULL on error
 */
char* leptris_unicode_to_lower(const char* str, size_t len, size_t* out_len);

/**
 * Case-insensitive comparison of two UTF-8 strings
 *
 * @param str1 First string
 * @param len1 Length of first string
 * @param str2 Second string
 * @param len2 Length of second string
 * @return 0 if equal, <0 if str1 < str2, >0 if str1 > str2
 */
int leptris_unicode_casecmp(const char* str1, size_t len1,
                           const char* str2, size_t len2);

/**
 * Check if a codepoint is whitespace
 *
 * @param codepoint Unicode codepoint
 * @return true if whitespace, false otherwise
 */
bool leptris_unicode_is_whitespace(int codepoint);

/**
 * Get the next UTF-8 codepoint from a string
 *
 * @param str Pointer to UTF-8 string (will be advanced)
 * @param len Pointer to remaining length (will be decremented)
 * @return Unicode codepoint, or -1 on error
 */
int leptris_unicode_next_codepoint(const char** str, size_t* len);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_UNICODE_H */