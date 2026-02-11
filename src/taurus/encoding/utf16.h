/* utf16.h - UTF-16 encoding support
 * Copyright (c) 2024, Ribose Inc.
 *
 * UTF-16 to UTF-8 conversion for Taurus XML Parser
 */

#ifndef TAURUS_UTF16_H
#define TAURUS_UTF16_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UTF-16 Byte Order Mark types */
typedef enum {
    UTF16_BOM_NONE = 0,  /* No BOM detected */
    UTF16_BOM_LE,        /* Little Endian BOM (FF FE) */
    UTF16_BOM_BE         /* Big Endian BOM (FE FF) */
} utf16_bom_t;

/* UTF-16 encoding types */
typedef enum {
    UTF16_UNKNOWN = 0,
    UTF16_LE,           /* Little Endian */
    UTF16_BE            /* Big Endian */
} utf16_encoding_t;

/**
 * Detect UTF-16 BOM (Byte Order Mark) in data
 *
 * @param data Data buffer to check
 * @param len Length of data buffer
 * @return BOM type detected
 */
utf16_bom_t utf16_detect_bom(const unsigned char* data, size_t len);

/**
 * Detect UTF-16 encoding without BOM (by analyzing byte patterns)
 *
 * @param data Data buffer to analyze
 * @param len Length of data buffer
 * @return Detected encoding type
 */
utf16_encoding_t utf16_detect_encoding(const unsigned char* data, size_t len);

/**
 * Calculate required UTF-8 buffer size for UTF-16 to UTF-8 conversion
 *
 * @param utf16 UTF-16 data buffer
 * @param utf16_len Length of UTF-16 data in bytes
 * @param encoding UTF-16 encoding (LE or BE)
 * @return Maximum number of UTF-8 bytes needed (including null terminator)
 */
size_t utf16_to_utf8_size(const unsigned char* utf16, size_t utf16_len,
                           utf16_encoding_t encoding);

/**
 * Convert UTF-16 LE/BE string to UTF-8
 *
 * @param utf16 UTF-16 data buffer
 * @param utf16_len Length of UTF-16 data in bytes
 * @param utf8 Output buffer for UTF-8 data
 * @param utf8_size Size of output buffer
 * @param encoding UTF-16 encoding (LE or BE)
 * @return Number of UTF-8 bytes written (excluding null terminator), or 0 on error
 */
size_t utf16_to_utf8(const unsigned char* utf16, size_t utf16_len,
                      char* utf8, size_t utf8_size,
                      utf16_encoding_t encoding);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_UTF16_H */
