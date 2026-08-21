/**
 * @file encoding.c
 * @brief Encoding conversion implementation using iconv
 */

#include "encoding.h"
#include "utf16.h"
#include <iconv.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Encoding name mapping */
static const char* encoding_names[] = {
    [LEPTRIS_ENCODING_UTF8] = "UTF-8",
    [LEPTRIS_ENCODING_UTF16LE] = "UTF-16LE",
    [LEPTRIS_ENCODING_UTF16BE] = "UTF-16BE",
    [LEPTRIS_ENCODING_UTF32LE] = "UTF-32LE",
    [LEPTRIS_ENCODING_UTF32BE] = "UTF-32BE",
    [LEPTRIS_ENCODING_ISO8859_1] = "ISO-8859-1",
    [LEPTRIS_ENCODING_ISO8859_2] = "ISO-8859-2",
    [LEPTRIS_ENCODING_ISO8859_15] = "ISO-8859-15",
    [LEPTRIS_ENCODING_WINDOWS_1252] = "WINDOWS-1252",
    [LEPTRIS_ENCODING_SHIFT_JIS] = "SHIFT_JIS",
    [LEPTRIS_ENCODING_EUC_JP] = "EUC-JP",
    [LEPTRIS_ENCODING_GB18030] = "GB18030",
    [LEPTRIS_ENCODING_EBCDIC_037] = "EBCDIC-CP-US",
    [LEPTRIS_ENCODING_UNKNOWN] = NULL
};

/**
 * Detect encoding from byte order mark (BOM)
 */
static leptris_encoding_t detect_bom(const unsigned char* data, size_t len) {
    if (len >= 3) {
        /* UTF-8 BOM: EF BB BF */
        if (data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
            return LEPTRIS_ENCODING_UTF8;
        }
    }

    if (len >= 4) {
        /* UTF-32LE BOM: FF FE 00 00 */
        if (data[0] == 0xFF && data[1] == 0xFE &&
            data[2] == 0x00 && data[3] == 0x00) {
            return LEPTRIS_ENCODING_UTF32LE;
        }
        /* UTF-32BE BOM: 00 00 FE FF */
        if (data[0] == 0x00 && data[1] == 0x00 &&
            data[2] == 0xFE && data[3] == 0xFF) {
            return LEPTRIS_ENCODING_UTF32BE;
        }
    }

    if (len >= 2) {
        /* UTF-16LE BOM: FF FE */
        if (data[0] == 0xFF && data[1] == 0xFE) {
            return LEPTRIS_ENCODING_UTF16LE;
        }
        /* UTF-16BE BOM: FE FF */
        if (data[0] == 0xFE && data[1] == 0xFF) {
            return LEPTRIS_ENCODING_UTF16BE;
        }
    }

    return LEPTRIS_ENCODING_UNKNOWN;
}

/**
 * Heuristic detection for common encodings
 */
static leptris_encoding_t detect_heuristic(const unsigned char* data, size_t len) {
    /* Check for EBCDIC encoding first (characteristic byte patterns)
     * EBCDIC has predictable patterns like:
     * - 0x4C = '<' (less than sign)
     * - 0x40 = ' ' (space)
     * - 0xC1-0xC9 = 'A'-'I'
     * - 0xD1-0xD9 = 'J'-'R'
     * - 0xE2-0xE9 = 'S'-'Z'
     * - 0xF0-0xF9 = '0'-'9'
     */
    if (len >= 10) {
        int ebcdic_indicators = 0;

        /* Check for EBCDIC '<' (0x4C) near start of file */
        if (data[0] == 0x4C || data[1] == 0x4C || data[2] == 0x4C) {
            ebcdic_indicators++;
        }

        /* Check for EBCDIC space (0x40) */
        for (size_t i = 0; i < 10 && i < len; i++) {
            if (data[i] == 0x40) {
                ebcdic_indicators++;
                break;
            }
        }

        /* Check for EBCDIC uppercase letters (C1-C9, D1-D9, E2-E9) */
        for (size_t i = 0; i < 20 && i < len; i++) {
            unsigned char c = data[i];
            if ((c >= 0xC1 && c <= 0xC9) || /* A-I */
                (c >= 0xD1 && c <= 0xD9) || /* J-R */
                (c >= 0xE2 && c <= 0xE9)) { /* S-Z */
                ebcdic_indicators++;
                break;
            }
        }

        /* If we have multiple EBCDIC indicators, it's likely EBCDIC */
        if (ebcdic_indicators >= 2) {
            return LEPTRIS_ENCODING_EBCDIC_037;
        }
    }

    size_t null_count = 0;
    size_t high_bit_count = 0;

    /* Sample first 1024 bytes or entire string if shorter */
    size_t sample_len = (len > 1024) ? 1024 : len;

    for (size_t i = 0; i < sample_len; i++) {
        if (data[i] == 0) {
            null_count++;
        } else if (data[i] >= 0x80) {
            high_bit_count++;
        }
    }

    /* If we have null bytes, likely UTF-16 or UTF-32 */
    if (null_count > sample_len / 10) {
        /* Check byte patterns for UTF-16 */
        if (len >= 2 && data[0] != 0 && data[1] == 0) {
            return LEPTRIS_ENCODING_UTF16LE;
        }
        if (len >= 2 && data[0] == 0 && data[1] != 0) {
            return LEPTRIS_ENCODING_UTF16BE;
        }
        /* Otherwise might be UTF-32 */
        return LEPTRIS_ENCODING_UTF32LE;
    }

    /* If mostly ASCII, check for valid UTF-8 sequences */
    if (high_bit_count > 0) {
        /* Simple UTF-8 validation */
        bool valid_utf8 = true;
        for (size_t i = 0; i < sample_len; i++) {
            if ((data[i] & 0x80) == 0) {
                continue;  /* ASCII */
            }

            /* Multi-byte sequence */
            int bytes_expected = 0;
            if ((data[i] & 0xE0) == 0xC0) bytes_expected = 1;
            else if ((data[i] & 0xF0) == 0xE0) bytes_expected = 2;
            else if ((data[i] & 0xF8) == 0xF0) bytes_expected = 3;
            else {
                valid_utf8 = false;
                break;
            }

            /* Check continuation bytes */
            for (int j = 1; j <= bytes_expected && i + j < sample_len; j++) {
                if ((data[i + j] & 0xC0) != 0x80) {
                    valid_utf8 = false;
                    break;
                }
            }

            if (!valid_utf8) break;
            i += bytes_expected;
        }

        if (valid_utf8) {
            return LEPTRIS_ENCODING_UTF8;
        }
    }

    /* Default to ISO-8859-1 for 8-bit data */
    if (high_bit_count > 0) {
        return LEPTRIS_ENCODING_ISO8859_1;
    }

    /* Pure ASCII is valid UTF-8 */
    return LEPTRIS_ENCODING_UTF8;
}

/**
 * Detect encoding from byte order mark or heuristics
 */
leptris_encoding_t leptris_encoding_detect(const char* data, size_t len) {
    if (!data || len == 0) {
        return LEPTRIS_ENCODING_UNKNOWN;
    }

    const unsigned char* udata = (const unsigned char*)data;

    /* First try BOM detection */
    leptris_encoding_t encoding = detect_bom(udata, len);
    if (encoding != LEPTRIS_ENCODING_UNKNOWN) {
        return encoding;
    }

    /* Fall back to heuristic detection */
    return detect_heuristic(udata, len);
}

/**
 * Get encoding name string
 */
const char* leptris_encoding_name(leptris_encoding_t encoding) {
    if (encoding < 0 || encoding >= LEPTRIS_ENCODING_UNKNOWN) {
        return NULL;
    }
    return encoding_names[encoding];
}

/**
 * Convert text from one encoding to another
 */
char* leptris_encoding_convert(const char* from_encoding,
                               const char* to_encoding,
                               const char* input,
                               size_t input_len,
                               size_t* output_len) {
    if (!from_encoding || !to_encoding || !input || !output_len) {
        return NULL;
    }

    /* Open iconv conversion descriptor */
    iconv_t cd = iconv_open(to_encoding, from_encoding);
    if (cd == (iconv_t)-1) {
        return NULL;
    }

    /* Allocate output buffer (4x input size to handle expansion) */
    size_t outbuf_size = input_len * 4 + 1;
    char* outbuf = malloc(outbuf_size);
    if (!outbuf) {
        iconv_close(cd);
        return NULL;
    }

    /* Perform conversion */
    char* inptr = (char*)input;
    char* outptr = outbuf;
    size_t inleft = input_len;
    size_t outleft = outbuf_size - 1;

    size_t result = iconv(cd, &inptr, &inleft, &outptr, &outleft);

    if (result == (size_t)-1) {
        /* Conversion error */
        free(outbuf);
        iconv_close(cd);
        return NULL;
    }

    /* Null-terminate output */
    *outptr = '\0';
    *output_len = outbuf_size - 1 - outleft;

    iconv_close(cd);
    return outbuf;
}

/**
 * Convert text to UTF-8
 */
char* leptris_encoding_to_utf8(leptris_encoding_t from_encoding,
                               const char* input,
                               size_t input_len,
                               size_t* output_len) {
    if (!input || !output_len) {
        return NULL;
    }

    /* Get encoding name */
    const char* from_name = leptris_encoding_name(from_encoding);
    if (!from_name) {
        return NULL;
    }

    /* If already UTF-8, just copy */
    if (from_encoding == LEPTRIS_ENCODING_UTF8) {
        char* result = malloc(input_len + 1);
        if (!result) {
            return NULL;
        }
        memcpy(result, input, input_len);
        result[input_len] = '\0';
        *output_len = input_len;
        return result;
    }

    /* Convert to UTF-8 */
    return leptris_encoding_convert(from_name, "UTF-8", input, input_len, output_len);
}

/**
 * Parse encoding from XML declaration
 */
char* leptris_encoding_parse_declaration(const char* xml, size_t len) {
    if (!xml || len < 20) {  /* Minimum: <?xml encoding=""?> */
        return NULL;
    }

    /* Check for <?xml prefix */
    if (strncmp(xml, "<?xml", 5) != 0) {
        return NULL;
    }

    /* Find encoding=" */
    const char* encoding_start = strstr(xml, "encoding");
    if (!encoding_start || encoding_start - xml > (long)len) {
        return NULL;
    }

    /* Skip "encoding" and whitespace */
    encoding_start += 8;
    while (encoding_start - xml < (long)len && (*encoding_start == ' ' || *encoding_start == '\t')) {
        encoding_start++;
    }

    /* Expect '=' */
    if (encoding_start - xml >= (long)len || *encoding_start != '=') {
        return NULL;
    }
    encoding_start++;

    /* Skip whitespace after '=' */
    while (encoding_start - xml < (long)len && (*encoding_start == ' ' || *encoding_start == '\t')) {
        encoding_start++;
    }

    /* Expect quote (single or double) */
    if (encoding_start - xml >= (long)len) {
        return NULL;
    }
    char quote = *encoding_start;
    if (quote != '"' && quote != '\'') {
        return NULL;
    }
    encoding_start++;

    /* Find closing quote */
    const char* encoding_end = encoding_start;
    while (encoding_end - xml < (long)len && *encoding_end != quote) {
        encoding_end++;
    }

    if (encoding_end - xml >= (long)len) {
        return NULL;
    }

    /* Extract encoding name */
    size_t encoding_len = encoding_end - encoding_start;
    if (encoding_len == 0 || encoding_len > 64) {
        return NULL;
    }

    char* encoding_name = malloc(encoding_len + 1);
    if (!encoding_name) {
        return NULL;
    }

    memcpy(encoding_name, encoding_start, encoding_len);
    encoding_name[encoding_len] = '\0';

    return encoding_name;
}

/**
 * Auto-detect and convert XML to UTF-8
 */
char* leptris_encoding_auto_convert(const char* xml,
                                    size_t len,
                                    size_t* output_len,
                                    char** detected_encoding) {
    if (!xml || len == 0 || !output_len) {
        return NULL;
    }

    /* First try to parse encoding from XML declaration */
    char* declared_encoding = leptris_encoding_parse_declaration(xml, len);

    /* If no declared encoding, use BOM/heuristic detection */
    leptris_encoding_t detected = LEPTRIS_ENCODING_UNKNOWN;
    if (!declared_encoding) {
        detected = leptris_encoding_detect(xml, len);
    }

    /* Determine which encoding to use */
    const char* encoding_to_use = declared_encoding;
    if (!encoding_to_use && detected != LEPTRIS_ENCODING_UNKNOWN) {
        encoding_to_use = leptris_encoding_name(detected);
    }

    /* Store detected encoding if requested */
    if (detected_encoding) {
        if (declared_encoding) {
            *detected_encoding = strdup(declared_encoding);
        } else if (detected != LEPTRIS_ENCODING_UNKNOWN) {
            const char* name = leptris_encoding_name(detected);
            *detected_encoding = name ? strdup(name) : NULL;
        } else {
            *detected_encoding = NULL;
        }
    }

    /* If UTF-8 (or unknown, assume UTF-8), just copy */
    if (!encoding_to_use ||
        strcmp(encoding_to_use, "UTF-8") == 0 ||
        strcmp(encoding_to_use, "utf-8") == 0 ||
        detected == LEPTRIS_ENCODING_UTF8) {

        char* result = malloc(len + 1);
        if (!result) {
            if (declared_encoding) free(declared_encoding);
            return NULL;
        }
        memcpy(result, xml, len);
        result[len] = '\0';
        *output_len = len;

        if (declared_encoding) free(declared_encoding);
        return result;
    }

    /* Native UTF-16 to UTF-8 conversion (without iconv) */
    if (detected == LEPTRIS_ENCODING_UTF16LE || detected == LEPTRIS_ENCODING_UTF16BE) {
        utf16_encoding_t encoding = (detected == LEPTRIS_ENCODING_UTF16LE) ? UTF16_LE : UTF16_BE;

        /* Calculate required UTF-8 buffer size */
        size_t utf8_size = utf16_to_utf8_size((const unsigned char*)xml, len, encoding);
        char* utf8_buffer = (char*)malloc(utf8_size);
        if (!utf8_buffer) {
            if (declared_encoding) free(declared_encoding);
            return NULL;
        }

        /* Convert UTF-16 to UTF-8 */
        size_t utf8_len = utf16_to_utf8((const unsigned char*)xml, len, utf8_buffer, utf8_size, encoding);
        utf8_buffer[utf8_len] = '\0';

        *output_len = utf8_len;

        if (declared_encoding) free(declared_encoding);
        return utf8_buffer;
    }

    /* Need to convert from detected encoding to UTF-8 */
    char* converted = leptris_encoding_convert(encoding_to_use, "UTF-8",
                                               xml, len, output_len);

    /* If conversion with declared encoding failed, try heuristic detection */
    if (!converted && declared_encoding) {
        /* Free the failed declared encoding and try heuristic detection */
        free(declared_encoding);
        declared_encoding = NULL;

        /* Try heuristic detection */
        detected = leptris_encoding_detect(xml, len);
        if (detected != LEPTRIS_ENCODING_UNKNOWN) {
            encoding_to_use = leptris_encoding_name(detected);
            if (encoding_to_use) {
                /* Update detected encoding output */
                if (detected_encoding) {
                    if (*detected_encoding) free(*detected_encoding);
                    *detected_encoding = strdup(encoding_to_use);
                }

                /* Try conversion again with detected encoding */
                converted = leptris_encoding_convert(encoding_to_use, "UTF-8",
                                                   xml, len, output_len);
            }
        }
    }

    if (!converted) {
        if (declared_encoding) free(declared_encoding);
        return NULL;
    }

    /* Update encoding declaration to UTF-8 if conversion was performed
     * and the content was actually converted (i.e., it wasn't already UTF-8) */
    char* enc_ptr = strstr(converted, "encoding=");
    if (enc_ptr && enc_ptr - converted < (long)*output_len) {
        /* Check if the encoding is not already UTF-8 */
        char quote = enc_ptr[9];  /* Skip 'encoding=' */
        if (quote == '"' || quote == '\'') {
            char* value_start = enc_ptr + 10;
            char* value_end = strchr(value_start, quote);
            if (value_end) {
                /* Check if the encoding value is not already UTF-8 */
                int is_utf8 = (value_end - value_start == 5 &&
                               (strncmp(value_start, "UTF-8", 5) == 0 ||
                                strncmp(value_start, "utf-8", 5) == 0));
                if (!is_utf8) {
                    /* Replace encoding value with UTF-8
                     * prefix_len includes everything up to opening quote (not including it)
                     * suffix_len includes closing quote + rest of file */
                    size_t prefix_len = value_start - converted;
                    size_t suffix_len = *output_len - (value_end - converted);
                    size_t new_len = prefix_len + 5 + suffix_len;

                    char* new_xml = (char*)malloc(new_len + 1);
                    if (new_xml) {
                        memcpy(new_xml, converted, prefix_len);
                        memcpy(new_xml + prefix_len, "UTF-8", 5);
                        memcpy(new_xml + prefix_len + 5, value_end, suffix_len + 1);
                        new_xml[new_len] = '\0';
                        free(converted);
                        converted = new_xml;
                        *output_len = new_len;
                    }
                }
            }
        }
    }

    if (declared_encoding) free(declared_encoding);
    return converted;
}