/* utf16.c - UTF-16 to UTF-8 conversion
 * Copyright (c) 2024, Ribose Inc.
 *
 * UTF-16 encoding support for Taurus XML Parser
 */

#include "utf16.h"
#include <string.h>
#include <stddef.h>

/* Check for BOM (Byte Order Mark) */
utf16_bom_t utf16_detect_bom(const unsigned char* data, size_t len) {
    if (!data || len < 2) {
        return UTF16_BOM_NONE;
    }

    /* UTF-16 LE BOM: FF FE */
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        /* Make sure it's not UTF-32 LE (FF FE 00 00) */
        if (len < 4 || data[2] != 0x00 || data[3] != 0x00) {
            return UTF16_BOM_LE;
        }
    }

    /* UTF-16 BE BOM: FE FF */
    if (len >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        /* Make sure it's not UTF-32 BE (00 00 FE FF) */
        if (len < 4 || data[2] != 0x00 || data[3] != 0x00) {
            return UTF16_BOM_BE;
        }
    }

    return UTF16_BOM_NONE;
}

/* Detect UTF-16 encoding without BOM by checking for null bytes */
utf16_encoding_t utf16_detect_encoding(const unsigned char* data, size_t len) {
    if (!data || len < 4) {
        return UTF16_UNKNOWN;
    }

    /* Safety check: if the buffer contains a null terminator before len,
     * use the actual string length to avoid reading past valid data.
     * This handles cases where caller provides incorrect length. */
    size_t actual_len = strnlen((const char*)data, len);
    if (actual_len < len) {
        len = actual_len;
    }
    if (len < 4) {
        return UTF16_UNKNOWN;
    }

    /* Check for UTF-16 LE pattern: ASCII followed by null byte
     * Example: "H" (0x48) followed by 0x00 */
    size_t le_score = 0;
    size_t be_score = 0;

    /* Sample first 100 bytes or entire string if shorter */
    size_t sample_len = (len > 100) ? 100 : len;

    for (size_t i = 0; i + 1 < sample_len; i += 2) {
        if (data[i] >= 0x20 && data[i] < 0x7F && data[i + 1] == 0x00) {
            le_score++;
        }
    }

    for (size_t i = 1; i + 1 < sample_len; i += 2) {
        if (data[i] >= 0x20 && data[i] < 0x7F && data[i - 1] == 0x00) {
            be_score++;
        }
    }

    if (le_score > be_score && le_score > 5) {
        return UTF16_LE;
    } else if (be_score > le_score && be_score > 5) {
        return UTF16_BE;
    }

    return UTF16_UNKNOWN;
}

/* Convert UTF-16 code unit to UTF-8 */
static size_t utf16_to_utf8_codepoint(uint32_t codepoint, char* utf8) {
    if (codepoint < 0x80) {
        /* 1 byte: 0xxxxxxx */
        utf8[0] = (char)codepoint;
        return 1;
    } else if (codepoint < 0x800) {
        /* 2 bytes: 110xxxxx 10xxxxxx */
        utf8[0] = (char)(0xC0 | (codepoint >> 6));
        utf8[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint < 0x10000) {
        /* 3 bytes: 1110xxxx 10xxxxxx 10xxxxxx */
        utf8[0] = (char)(0xE0 | (codepoint >> 12));
        utf8[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else if (codepoint < 0x110000) {
        /* 4 bytes: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
        utf8[0] = (char)(0xF0 | (codepoint >> 18));
        utf8[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        utf8[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;  /* Invalid code point */
}

/* Read UTF-16 code unit from buffer (handles endianness) */
static uint16_t utf16_read_codeunit(const unsigned char* data, utf16_encoding_t encoding) {
    if (encoding == UTF16_LE) {
        /* Little endian: low byte first */
        return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    } else {
        /* Big endian: high byte first */
        return ((uint16_t)data[0] << 8) | (uint16_t)data[1];
    }
}

/* Convert UTF-16 LE/BE string to UTF-8
 *
 * Returns: Number of UTF-8 bytes written, or 0 on error
 */
size_t utf16_to_utf8(const unsigned char* utf16, size_t utf16_len,
                      char* utf8, size_t utf8_size,
                      utf16_encoding_t encoding) {
    if (!utf16 || !utf8 || utf8_size == 0) {
        return 0;
    }

    size_t utf16_pos = 0;
    size_t utf8_pos = 0;
    int has_bom = (utf16_detect_bom(utf16, utf16_len) != UTF16_BOM_NONE);

    /* Skip BOM if present */
    if (has_bom && utf16_len >= 2) {
        utf16_pos += 2;
    }

    while (utf16_pos + 1 < utf16_len) {
        uint16_t codeunit = utf16_read_codeunit(utf16 + utf16_pos, encoding);
        utf16_pos += 2;

        uint32_t codepoint;

        /* Check for surrogate pair */
        if (codeunit >= 0xD800 && codeunit <= 0xDBFF) {
            /* High surrogate - need low surrogate */
            if (utf16_pos + 1 >= utf16_len) {
                break;  /* Incomplete surrogate pair */
            }

            uint16_t low_surrogate = utf16_read_codeunit(utf16 + utf16_pos, encoding);
            utf16_pos += 2;

            if (low_surrogate < 0xDC00 || low_surrogate > 0xDFFF) {
                break;  /* Invalid low surrogate */
            }

            /* Calculate full code point from surrogate pair */
            codepoint = 0x10000 + ((codeunit - 0xD800) << 10) + (low_surrogate - 0xDC00);
        } else if (codeunit >= 0xDC00 && codeunit <= 0xDFFF) {
            /* Low surrogate without high surrogate - invalid */
            break;
        } else {
            /* Regular BMP character */
            codepoint = codeunit;
        }

        /* Convert code point to UTF-8 */
        char utf8_buf[4];
        size_t utf8_len = utf16_to_utf8_codepoint(codepoint, utf8_buf);

        if (utf8_len == 0) {
            break;  /* Invalid code point */
        }

        /* Check if we have enough space in output buffer */
        if (utf8_pos + utf8_len > utf8_size) {
            break;  /* Output buffer too small */
        }

        /* Copy UTF-8 bytes to output */
        for (size_t i = 0; i < utf8_len; i++) {
            utf8[utf8_pos++] = utf8_buf[i];
        }
    }

    return utf8_pos;
}

/* Calculate UTF-8 buffer size needed for UTF-16 to UTF-8 conversion
 * Returns: Maximum number of UTF-8 bytes needed, or 0 on error
 */
size_t utf16_to_utf8_size(const unsigned char* utf16, size_t utf16_len,
                           utf16_encoding_t encoding) {
    if (!utf16 || utf16_len == 0) {
        return 0;
    }

    /* Worst case: each UTF-16 code unit becomes 3 UTF-8 bytes
     * Surrogate pairs (2 code units) become 4 UTF-8 bytes
     * So we estimate: number of code units * 3 */
    size_t num_codeunits = utf16_len / 2;

    /* Account for BOM */
    int has_bom = (utf16_detect_bom(utf16, utf16_len) != UTF16_BOM_NONE);
    if (has_bom && num_codeunits > 0) {
        num_codeunits--;
    }

    /* Add 1 for null terminator */
    return (num_codeunits * 3) + 1;
}
