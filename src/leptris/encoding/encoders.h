/* lib/src/encoding/encoders.h - Single-byte encoding to UTF-8 converters
 * Copyright (c) 2024, Ribose Inc.
 */

#ifndef TAURUS_ENCODING_ENCODERS_H
#define TAURUS_ENCODING_ENCODERS_H

#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * ISO-8859-1 (Latin-1) to UTF-8 Converter
 * ============================================================================ */

/**
 * Convert ISO-8859-1 (Latin-1) encoded string to UTF-8.
 * @param input ISO-8859-1 encoded string
 * @param input_len Length of input string
 * @param output Output buffer (must be large enough)
 * @param output_size Size of output buffer
 * @return Number of bytes written to output, or 0 on error
 */
size_t latin1_to_utf8(const unsigned char* input, size_t input_len,
                        char* output, size_t output_size);

/**
 * Calculate the output buffer size needed for ISO-8859-1 to UTF-8 conversion.
 */
size_t latin1_to_utf8_size(const unsigned char* input, size_t input_len);

/* ============================================================================
 * ISO-8859-5 (Cyrillic) to UTF-8 Converter
 * ============================================================================ */

/**
 * Convert ISO-8859-5 (Cyrillic) encoded string to UTF-8.
 */
size_t iso88595_to_utf8(const unsigned char* input, size_t input_len,
                         char* output, size_t output_size);

/**
 * Calculate the output buffer size needed for ISO-8859-5 to UTF-8 conversion.
 */
size_t iso88595_to_utf8_size(const unsigned char* input, size_t input_len);

#endif /* TAURUS_ENCODING_ENCODERS_H */

/* ============================================================================
 * EBCDIC-037 to UTF-8 Converter
 * ============================================================================ */

/**
 * Convert EBCDIC-037 encoded string to UTF-8.
 */
size_t ebcdic037_to_utf8(const unsigned char* input, size_t input_len,
                         char* output, size_t output_size);

/**
 * Calculate the output buffer size needed for EBCDIC-037 to UTF-8 conversion.
 */
size_t ebcdic037_to_utf8_size(const unsigned char* input, size_t input_len);
