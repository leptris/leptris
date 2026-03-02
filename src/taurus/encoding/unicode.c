/* unicode.c - Unicode Conversion Functions
 * Copyright (c) 2025, Ribose Inc.
 *
 * Provides UTF-8 to UTF-16/UTF-32 conversion functions.
 * Based on pugixml unicode.cpp but implemented in pure C.
 */

#include "unicode.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

#define REPLACEMENT_CHARACTER 0xFFFD

/* ============================================================================
 * UTF-8 Encoding/Decoding Helpers
 * ============================================================================ */

/**
 * Get UTF-8 sequence length from lead byte
 * Returns 0 for invalid lead bytes
 */
static size_t utf8_sequence_length(unsigned char lead_byte) {
    if ((lead_byte & 0x80) == 0x00) return 1;      /* 0xxxxxxx */
    if ((lead_byte & 0xE0) == 0xC0) return 2;      /* 110xxxxx */
    if ((lead_byte & 0xF0) == 0xE0) return 3;      /* 1110xxxx */
    if ((lead_byte & 0xF8) == 0xF0) return 4;      /* 11110xxx */
    return 0;  /* Invalid */
}

/**
 * Decode UTF-8 sequence to code point
 * Returns byte count consumed, or 0 for invalid sequence
 */
static size_t utf8_decode(const char* str, size_t len, uint32_t* code_point) {
    if (!str || len == 0) return 0;

    unsigned char lead = str[0];
    size_t seq_len = utf8_sequence_length(lead);

    if (seq_len == 0 || seq_len > len) return 0;  /* Invalid or truncated */

    *code_point = 0;

    switch (seq_len) {
        case 1:
            *code_point = lead & 0x7F;
            break;

        case 2:
            if ((str[1] & 0xC0) != 0x80) return 0;  /* Invalid continuation */
            *code_point = ((lead & 0x1F) << 6) | (str[1] & 0x3F);
            /* Check for overlong encoding */
            if (*code_point < 0x80) return 0;
            break;

        case 3:
            if ((str[1] & 0xC0) != 0x80) return 0;
            if ((str[2] & 0xC0) != 0x80) return 0;
            *code_point = ((lead & 0x0F) << 12) | ((str[1] & 0x3F) << 6) | (str[2] & 0x3F);
            /* Check for overlong encoding */
            if (*code_point < 0x800) return 0;
            /* Check for surrogate range */
            if (*code_point >= 0xD800 && *code_point <= 0xDFFF) return 0;
            break;

        case 4:
            if ((str[1] & 0xC0) != 0x80) return 0;
            if ((str[2] & 0xC0) != 0x80) return 0;
            if ((str[3] & 0xC0) != 0x80) return 0;
            *code_point = ((lead & 0x07) << 18) | ((str[1] & 0x3F) << 12) |
                         ((str[2] & 0x3F) << 6) | (str[3] & 0x3F);
            /* Check for overlong encoding */
            if (*code_point < 0x10000) return 0;
            /* Check for beyond Unicode range */
            if (*code_point > 0x10FFFF) return 0;
            break;

        default:
            return 0;
    }

    return seq_len;
}

/**
 * Encode code point to UTF-8
 * Returns number of bytes written (1-4)
 */
static size_t utf8_encode(uint32_t code_point, char* buf) {
    if (code_point <= 0x7F) {
        buf[0] = (char)code_point;
        return 1;
    } else if (code_point <= 0x7FF) {
        buf[0] = (char)(0xC0 | ((code_point >> 6) & 0x1F));
        buf[1] = (char)(0x80 | (code_point & 0x3F));
        return 2;
    } else if (code_point <= 0xFFFF) {
        /* Check for surrogate range */
        if (code_point >= 0xD800 && code_point <= 0xDFFF) {
            /* Replace with replacement character */
            buf[0] = (char)0xEF;
            buf[1] = (char)0xBF;
            buf[2] = (char)0xBD;
            return 3;
        }
        buf[0] = (char)(0xE0 | ((code_point >> 12) & 0x0F));
        buf[1] = (char)(0x80 | ((code_point >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (code_point & 0x3F));
        return 3;
    } else if (code_point <= 0x10FFFF) {
        buf[0] = (char)(0xF0 | ((code_point >> 18) & 0x07));
        buf[1] = (char)(0x80 | ((code_point >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((code_point >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (code_point & 0x3F));
        return 4;
    } else {
        /* Beyond Unicode range - use replacement character */
        buf[0] = (char)0xEF;
        buf[1] = (char)0xBF;
        buf[2] = (char)0xBD;
        return 3;
    }
}

/* ============================================================================
 * UTF-8 to UTF-16 Conversion (as_wide equivalent)
 * ============================================================================ */

/**
 * Convert UTF-8 string to UTF-16 string
 * Note: UTF-16 uses 2 bytes per code unit ( wchar_t on Windows)
 * For code points beyond BMP, uses surrogate pairs
 *
 * Input: UTF-8 string
 * Output: UTF-16 string (allocated, caller must free)
 * Returns: Number of UTF-16 code units, or 0 on error
 */
size_t taurus_utf8_to_utf16(const char* utf8_str, uint16_t** out_utf16) {
    if (!utf8_str || !out_utf16) return 0;

    size_t utf8_len = strlen(utf8_str);
    if (utf8_len == 0) {
        *out_utf16 = (uint16_t*)calloc(1, sizeof(uint16_t));
        return (*out_utf16) ? 1 : 0;
    }

    /* Calculate required UTF-16 buffer size */
    size_t utf16_count = 0;
    size_t i = 0;

    while (i < utf8_len) {
        uint32_t code_point;
        size_t consumed = utf8_decode(utf8_str + i, utf8_len - i, &code_point);

        if (consumed == 0) {
            /* Invalid UTF-8, skip byte - but count replacement character */
            utf16_count++;  /* Count the replacement character we'll add */
            i++;
            continue;
        }

        /* Count UTF-16 code units needed */
        if (code_point <= 0xFFFF) {
            utf16_count++;
        } else {
            /* Surrogate pair needed */
            utf16_count += 2;
        }

        i += consumed;
    }

    /* Allocate UTF-16 buffer */
    *out_utf16 = (uint16_t*)calloc(utf16_count + 1, sizeof(uint16_t));
    if (!*out_utf16) return 0;

    /* Convert to UTF-16 */
    size_t out_idx = 0;
    i = 0;

    while (i < utf8_len) {
        uint32_t code_point;
        size_t consumed = utf8_decode(utf8_str + i, utf8_len - i, &code_point);

        if (consumed == 0) {
            /* Invalid UTF-8, add replacement character */
            (*out_utf16)[out_idx++] = REPLACEMENT_CHARACTER;
            i++;
            continue;
        }

        if (code_point <= 0xFFFF) {
            (*out_utf16)[out_idx++] = (uint16_t)code_point;
        } else {
            /* Encode as surrogate pair */
            code_point -= 0x10000;
            (*out_utf16)[out_idx++] = (uint16_t)((code_point >> 10) + 0xD800);
            (*out_utf16)[out_idx++] = (uint16_t)((code_point & 0x3FF) + 0xDC00);
        }

        i += consumed;
    }

    (*out_utf16)[out_idx] = 0;  /* Null terminate */
    return out_idx;
}

/**
 * Free UTF-16 string allocated by taurus_utf8_to_utf16
 */
void taurus_utf16_free(uint16_t* utf16_str) {
    free(utf16_str);
}

/* ============================================================================
 * UTF-16 to UTF-8 Conversion (as_utf8 equivalent)
 * ============================================================================ */

/**
 * Convert UTF-16 string to UTF-8 string
 *
 * Input: UTF-16 string with specified length
 * Output: UTF-8 string (allocated, caller must free)
 * Returns: 0 on error, length of UTF-8 string on success
 */
size_t taurus_utf16_to_utf8(const uint16_t* utf16_str, size_t utf16_len, char** out_utf8) {
    if (!utf16_str || !out_utf8) return 0;

    if (utf16_len == 0) {
        *out_utf8 = strdup("");
        return (*out_utf8) ? 0 : 0;
    }

    /* Calculate required UTF-8 buffer size */
    size_t utf8_count = 0;
    size_t i = 0;

    while (i < utf16_len) {
        uint32_t code_point;

        if (utf16_str[i] >= 0xD800 && utf16_str[i] <= 0xDBFF) {
            /* High surrogate */
            if (i + 1 < utf16_len && utf16_str[i + 1] >= 0xDC00 && utf16_str[i + 1] <= 0xDFFF) {
                /* Valid surrogate pair */
                code_point = 0x10000 + ((utf16_str[i] - 0xD800) << 10) + (utf16_str[i + 1] - 0xDC00);
                i += 2;
            } else {
                /* Isolated high surrogate - use replacement character */
                code_point = REPLACEMENT_CHARACTER;
                i++;
            }
        } else if (utf16_str[i] >= 0xDC00 && utf16_str[i] <= 0xDFFF) {
            /* Isolated low surrogate - use replacement character */
            code_point = REPLACEMENT_CHARACTER;
            i++;
        } else {
            /* Regular BMP character */
            code_point = utf16_str[i];
            i++;
        }

        /* Count UTF-8 bytes needed */
        if (code_point <= 0x7F) utf8_count++;
        else if (code_point <= 0x7FF) utf8_count += 2;
        else if (code_point <= 0xFFFF) utf8_count += 3;
        else utf8_count += 4;
    }

    /* Allocate UTF-8 buffer */
    *out_utf8 = (char*)malloc(utf8_count + 1);
    if (!*out_utf8) return 0;

    /* Convert to UTF-8 */
    size_t out_idx = 0;
    i = 0;

    while (i < utf16_len) {
        uint32_t code_point;

        if (utf16_str[i] >= 0xD800 && utf16_str[i] <= 0xDBFF) {
            /* High surrogate */
            if (i + 1 < utf16_len && utf16_str[i + 1] >= 0xDC00 && utf16_str[i + 1] <= 0xDFFF) {
                /* Valid surrogate pair */
                code_point = 0x10000 + ((utf16_str[i] - 0xD800) << 10) + (utf16_str[i + 1] - 0xDC00);
                i += 2;
            } else {
                /* Isolated high surrogate */
                code_point = REPLACEMENT_CHARACTER;
                i++;
            }
        } else if (utf16_str[i] >= 0xDC00 && utf16_str[i] <= 0xDFFF) {
            /* Isolated low surrogate */
            code_point = REPLACEMENT_CHARACTER;
            i++;
        } else {
            /* Regular BMP character */
            code_point = utf16_str[i];
            i++;
        }

        out_idx += utf8_encode(code_point, *out_utf8 + out_idx);
    }

    (*out_utf8)[out_idx] = '\0';  /* Null terminate */
    return out_idx;
}

/**
 * Free UTF-8 string allocated by taurus_utf16_to_utf8
 */
void taurus_utf8_free(char* utf8_str) {
    free(utf8_str);
}
