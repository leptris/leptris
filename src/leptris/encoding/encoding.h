/**
 * @file encoding.h
 * @brief Character encoding conversion via iconv
 *
 * Provides encoding detection and conversion functions using iconv.
 */

#ifndef LEPTRIS_ENCODING_H
#define LEPTRIS_ENCODING_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Common character encodings
 */
typedef enum {
    LEPTRIS_ENCODING_UTF8,
    LEPTRIS_ENCODING_UTF16LE,
    LEPTRIS_ENCODING_UTF16BE,
    LEPTRIS_ENCODING_UTF32LE,
    LEPTRIS_ENCODING_UTF32BE,
    LEPTRIS_ENCODING_ISO8859_1,
    LEPTRIS_ENCODING_ISO8859_2,
    LEPTRIS_ENCODING_ISO8859_15,
    LEPTRIS_ENCODING_WINDOWS_1252,
    LEPTRIS_ENCODING_SHIFT_JIS,
    LEPTRIS_ENCODING_EUC_JP,
    LEPTRIS_ENCODING_GB18030,
    LEPTRIS_ENCODING_EBCDIC_037,
    LEPTRIS_ENCODING_UNKNOWN
} leptris_encoding_t;

/**
 * Detect encoding from byte order mark (BOM) or heuristics
 *
 * @param data Input data
 * @param len Length of data in bytes
 * @return Detected encoding
 */
leptris_encoding_t leptris_encoding_detect(const char* data, size_t len);

/**
 * Get encoding name string
 *
 * @param encoding Encoding enum value
 * @return Encoding name (e.g., "UTF-8", "ISO-8859-1")
 */
const char* leptris_encoding_name(leptris_encoding_t encoding);

/**
 * Convert text from one encoding to another
 *
 * @param from_encoding Source encoding name (e.g., "ISO-8859-1")
 * @param to_encoding Target encoding name (e.g., "UTF-8")
 * @param input Input data
 * @param input_len Length of input in bytes
 * @param output_len Output parameter for result length
 * @return Converted string (must be freed by caller) or NULL on error
 */
char* leptris_encoding_convert(const char* from_encoding,
                               const char* to_encoding,
                               const char* input,
                               size_t input_len,
                               size_t* output_len);

/**
 * Convert text to UTF-8
 *
 * @param from_encoding Source encoding enum
 * @param input Input data
 * @param input_len Length of input in bytes
 * @param output_len Output parameter for result length
 * @return UTF-8 string (must be freed by caller) or NULL on error
 */
char* leptris_encoding_to_utf8(leptris_encoding_t from_encoding,
                               const char* input,
                               size_t input_len,
                               size_t* output_len);

/**
 * Parse encoding from XML declaration
 *
 * Extracts encoding="..." from <?xml version="1.0" encoding="..." ?>
 *
 * @param xml XML string starting with <?xml
 * @param len Length of XML string
 * @return Encoding name (must be freed by caller) or NULL if not found
 */
char* leptris_encoding_parse_declaration(const char* xml, size_t len);

/**
 * Auto-detect and convert XML to UTF-8
 *
 * This function checks the XML declaration for encoding attribute,
 * falls back to BOM/heuristic detection, and converts to UTF-8 if needed.
 *
 * @param xml Input XML data
 * @param len Length of input in bytes
 * @param output_len Output parameter for result length
 * @param detected_encoding Output parameter for detected encoding (optional)
 * @return UTF-8 string (must be freed by caller, may be original if already UTF-8)
 *         or NULL on error
 */
char* leptris_encoding_auto_convert(const char* xml,
                                    size_t len,
                                    size_t* output_len,
                                    char** detected_encoding);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_ENCODING_H */