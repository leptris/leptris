/* lib/src/encoding/encoders.c - Single-byte encoding to UTF-8 converters
 * Copyright (c) 2024, Ribose Inc.
 *
 * Provides conversion from legacy single-byte encodings to UTF-8.
 */

#include "encoders.h"
#include <string.h>

/* ============================================================================
 * ISO-8859-1 (Latin-1) to UTF-8 Converter
 * ============================================================================ */

/**
 * Convert ISO-8859-1 (Latin-1) encoded string to UTF-8.
 * ISO-8859-1 is a single-byte encoding where characters 0-127 match ASCII
 * and characters 128-255 map to Unicode codepoints U+0080 to U+00FF.
 *
 * UTF-8 encoding:
 * - 0x00-0x7F: 1 byte (same as ISO-8859-1)
 * - 0x80-0xFF: 2 bytes: [0xC0+(c>>6), 0x80+(c&0x3F)]
 *
 * @param input ISO-8859-1 encoded string
 * @param input_len Length of input string
 * @param output Output buffer (must be large enough)
 * @param output_size Size of output buffer
 * @return Number of bytes written to output, or 0 on error
 */
size_t latin1_to_utf8(const unsigned char* input, size_t input_len,
                        char* output, size_t output_size) {
    if (!input || !output) return 0;
    if (input_len == 0) return 0;

    size_t out_pos = 0;
    size_t in_pos = 0;

    while (in_pos < input_len && out_pos < output_size) {
        unsigned char c = input[in_pos++];

        if (c < 0x80) {
            /* ASCII range - single byte */
            output[out_pos++] = (char)c;
        } else {
            /* 0x80-0xFF range - two UTF-8 bytes */
            if (out_pos + 2 > output_size) return 0;  /* Buffer too small */

            /* First byte: 110000xx where xx = (c >> 6) + 2 */
            /* c >> 6 gives 2 for 0x80-0xBF, 3 for 0xC0-0xFF */
            output[out_pos++] = (char)(0xC0 + (c >> 6));
            /* Second byte: 10xxxxxx where xxxxxx = c & 0x3F */
            output[out_pos++] = (char)(0x80 + (c & 0x3F));
        }
    }

    return out_pos;
}

/**
 * Calculate the output buffer size needed for ISO-8859-1 to UTF-8 conversion.
 * Worst case: every input byte becomes 2 output bytes.
 */
size_t latin1_to_utf8_size(const unsigned char* input, size_t input_len) {
    if (!input) return 0;

    size_t output_len = 0;
    for (size_t i = 0; i < input_len; i++) {
        if (input[i] < 0x80) {
            output_len += 1;
        } else {
            output_len += 2;
        }
    }
    return output_len;
}

/* ============================================================================
 * ISO-8859-5 (Cyrillic) to UTF-8 Converter
 * ============================================================================ */

/* Unicode codepoint mappings for ISO-8859-5
 * Maps byte value to Unicode codepoint (or 0 for unmapped) */
static const uint16_t iso88595_to_unicode[256] = {
    /* 0x00-0x7F: ASCII (direct mapping) */
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
    0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,

    /* 0x80-0x9F: C1 control characters (unchanged in ISO-8859-5) */
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008A, 0x008B, 0x008C, 0x008D, 0x008E, 0x008F,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009A, 0x009B, 0x009C, 0x009D, 0x009E, 0x009F,

    /* 0xA0-0xFF: Cyrillic characters */
    0x00A0,  /* 0xA0: NO-BREAK SPACE */
    0x0401,  /* 0xA1: CYRILLIC CAPITAL LETTER IO */
    0x0402,  /* 0xA2: CYRILLIC CAPITAL LETTER DJE */
    0x0403,  /* 0xA3: CYRILLIC CAPITAL LETTER GJE */
    0x0404,  /* 0xA4: CYRILLIC CAPITAL LETTER IE */
    0x0405,  /* 0xA5: CYRILLIC CAPITAL LETTER DZE */
    0x0406,  /* 0xA6: CYRILLIC CAPITAL LETTER BYELORUSIAN-UKRAINIAN I */
    0x0407,  /* 0xA7: CYRILLIC CAPITAL LETTER YI */
    0x0408,  /* 0xA8: CYRILLIC CAPITAL LETTER JE */
    0x0409,  /* 0xA9: CYRILLIC CAPITAL LETTER LJE */
    0x040A,  /* 0xAA: CYRILLIC CAPITAL LETTER NJE */
    0x040B,  /* 0xAB: CYRILLIC CAPITAL LETTER TSHE */
    0x040C,  /* 0xAC: CYRILLIC CAPITAL LETTER KJE */
    0x040D,  /* 0xAD: CYRILLIC CAPITAL LETTER I WITH GRAVE */
    0x040E,  /* 0xAE: CYRILLIC CAPITAL LETTER SHORT U */
    0x040F,  /* 0xAF: CYRILLIC CAPITAL LETTER DZHE */
    0x0410,  /* 0xB0: CYRILLIC CAPITAL LETTER A */
    0x0411,  /* 0xB1: CYRILLIC CAPITAL LETTER BE */
    0x0412,  /* 0xB2: CYRILLIC CAPITAL LETTER VE */
    0x0413,  /* 0xB3: CYRILLIC CAPITAL LETTER GHE */
    0x0414,  /* 0xB4: CYRILLIC CAPITAL LETTER DE */
    0x0415,  /* 0xB5: CYRILLIC CAPITAL LETTER IE */
    0x0416,  /* 0xB6: CYRILLIC CAPITAL LETTER ZHE */
    0x0417,  /* 0xB7: CYRILLIC CAPITAL LETTER ZE */
    0x0418,  /* 0xB8: CYRILLIC CAPITAL LETTER I */
    0x0419,  /* 0xB9: CYRILLIC CAPITAL LETTER SHORT I */
    0x041A,  /* 0xBA: CYRILLIC CAPITAL LETTER KA */
    0x041B,  /* 0xBB: CYRILLIC CAPITAL LETTER EL */
    0x041C,  /* 0xBC: CYRILLIC CAPITAL LETTER EM */
    0x041D,  /* 0xBD: CYRILLIC CAPITAL LETTER EN */
    0x041E,  /* 0xBE: CYRILLIC CAPITAL LETTER O */
    0x041F,  /* 0xBF: CYRILLIC CAPITAL LETTER PE */
    0x0420,  /* 0xC0: CYRILLIC CAPITAL LETTER ER */
    0x0421,  /* 0xC1: CYRILLIC CAPITAL LETTER ES */
    0x0422,  /* 0xC2: CYRILLIC CAPITAL LETTER TE */
    0x0423,  /* 0xC3: CYRILLIC CAPITAL LETTER U */
    0x0424,  /* 0xC4: CYRILLIC CAPITAL LETTER EF */
    0x0425,  /* 0xC5: CYRILLIC CAPITAL LETTER HA */
    0x0426,  /* 0xC6: CYRILLIC CAPITAL LETTER TSE */
    0x0427,  /* 0xC7: CYRILLIC CAPITAL LETTER CHE */
    0x0428,  /* 0xC8: CYRILLIC CAPITAL LETTER SHA */
    0x0429,  /* 0xC9: CYRILLIC CAPITAL LETTER SHCHA */
    0x042A,  /* 0xCA: CYRILLIC CAPITAL LETTER HARD SIGN */
    0x042B,  /* 0xCB: CYRILLIC CAPITAL LETTER YERU */
    0x042C,  /* 0xCC: CYRILLIC CAPITAL LETTER SOFT SIGN */
    0x042D,  /* 0xCD: CYRILLIC CAPITAL LETTER E */
    0x042E,  /* 0xCE: CYRILLIC CAPITAL LETTER YU */
    0x042F,  /* 0xCF: CYRILLIC CAPITAL LETTER YA */
    0x0430,  /* 0xD0: CYRILLIC SMALL LETTER A */
    0x0431,  /* 0xD1: CYRILLIC SMALL LETTER BE */
    0x0432,  /* 0xD2: CYRILLIC SMALL LETTER VE */
    0x0433,  /* 0xD3: CYRILLIC SMALL LETTER GHE */
    0x0434,  /* 0xD4: CYRILLIC SMALL LETTER DE */
    0x0435,  /* 0xD5: CYRILLIC SMALL LETTER IE */
    0x0436,  /* 0xD6: CYRILLIC SMALL LETTER ZHE */
    0x0437,  /* 0xD7: CYRILLIC SMALL LETTER ZE */
    0x0438,  /* 0xD8: CYRILLIC SMALL LETTER I */
    0x0439,  /* 0xD9: CYRILLIC SMALL LETTER SHORT I */
    0x043A,  /* 0xDA: CYRILLIC SMALL LETTER KA */
    0x043B,  /* 0xDB: CYRILLIC SMALL LETTER EL */
    0x043C,  /* 0xDC: CYRILLIC SMALL LETTER EM */
    0x043D,  /* 0xDD: CYRILLIC SMALL LETTER EN */
    0x043E,  /* 0xDE: CYRILLIC SMALL LETTER O */
    0x043F,  /* 0xDF: CYRILLIC SMALL LETTER PE */
    0x0440,  /* 0xE0: CYRILLIC SMALL LETTER ER */
    0x0441,  /* 0xE1: CYRILLIC SMALL LETTER ES */
    0x0442,  /* 0xE2: CYRILLIC SMALL LETTER TE */
    0x0443,  /* 0xE3: CYRILLIC SMALL LETTER U */
    0x0444,  /* 0xE4: CYRILLIC SMALL LETTER EF */
    0x0445,  /* 0xE5: CYRILLIC SMALL LETTER HA */
    0x0446,  /* 0xE6: CYRILLIC SMALL LETTER TSE */
    0x0447,  /* 0xE7: CYRILLIC SMALL LETTER CHE */
    0x0448,  /* 0xE8: CYRILLIC SMALL LETTER SHA */
    0x0449,  /* 0xE9: CYRILLIC SMALL LETTER SHCHA */
    0x044A,  /* 0xEA: CYRILLIC SMALL LETTER HARD SIGN */
    0x044B,  /* 0xEB: CYRILLIC SMALL LETTER YERU */
    0x044C,  /* 0xEC: CYRILLIC SMALL LETTER SOFT SIGN */
    0x044D,  /* 0xED: CYRILLIC SMALL LETTER E */
    0x044E,  /* 0xEE: CYRILLIC SMALL LETTER YU */
    0x044F,  /* 0xEF: CYRILLIC SMALL LETTER YA */
    0x2116,  /* 0xF0: NUMERO SIGN */
    0x0451,  /* 0xF1: CYRILLIC SMALL LETTER IO */
    0x0452,  /* 0xF2: CYRILLIC SMALL LETTER DJE */
    0x0453,  /* 0xF3: CYRILLIC SMALL LETTER GJE */
    0x0454,  /* 0xF4: CYRILLIC SMALL LETTER IE */
    0x0455,  /* 0xF5: CYRILLIC SMALL LETTER DZE */
    0x0456,  /* 0xF6: CYRILLIC SMALL LETTER BYELORUSIAN-UKRAINIAN I */
    0x0457,  /* 0xF7: CYRILLIC SMALL LETTER YI */
    0x0458,  /* 0xF8: CYRILLIC SMALL LETTER JE */
    0x0459,  /* 0xF9: CYRILLIC SMALL LETTER LJE */
    0x045A,  /* 0xFA: CYRILLIC SMALL LETTER NJE */
    0x045B,  /* 0xFB: CYRILLIC SMALL LETTER TSHE */
    0x045C,  /* 0xFC: CYRILLIC SMALL LETTER KJE */
    0x045D,  /* 0xFD: CYRILLIC SMALL LETTER I WITH GRAVE */
    0x045E,  /* 0xFE: CYRILLIC SMALL LETTER SHORT U */
    0x045F,  /* 0xFF: CYRILLIC SMALL LETTER DZHE */
};

/* Helper: Write Unicode codepoint as UTF-8 */
static int write_utf8_char(uint32_t codepoint, char* output, size_t output_size, size_t* out_pos) {
    if (codepoint <= 0x7F) {
        if (*out_pos + 1 > output_size) return 0;
        output[(*out_pos)++] = (char)codepoint;
    } else if (codepoint <= 0x7FF) {
        if (*out_pos + 2 > output_size) return 0;
        output[(*out_pos)++] = (char)(0xC0 | (codepoint >> 6));
        output[(*out_pos)++] = (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        if (*out_pos + 3 > output_size) return 0;
        output[(*out_pos)++] = (char)(0xE0 | (codepoint >> 12));
        output[(*out_pos)++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        output[(*out_pos)++] = (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0x10FFFF) {
        if (*out_pos + 4 > output_size) return 0;
        output[(*out_pos)++] = (char)(0xF0 | (codepoint >> 18));
        output[(*out_pos)++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        output[(*out_pos)++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        output[(*out_pos)++] = (char)(0x80 | (codepoint & 0x3F));
    } else {
        return 0;  /* Invalid codepoint */
    }
    return 1;
}

/**
 * Convert ISO-8859-5 (Cyrillic) encoded string to UTF-8.
 */
size_t iso88595_to_utf8(const unsigned char* input, size_t input_len,
                         char* output, size_t output_size) {
    if (!input || !output) return 0;
    if (input_len == 0) return 0;

    size_t out_pos = 0;

    for (size_t in_pos = 0; in_pos < input_len; in_pos++) {
        uint16_t codepoint = iso88595_to_unicode[input[in_pos]];
        if (!write_utf8_char(codepoint, output, output_size, &out_pos)) {
            return 0;  /* Conversion failed */
        }
    }

    return out_pos;
}

/**
 * Calculate the output buffer size needed for ISO-8859-5 to UTF-8 conversion.
 */
size_t iso88595_to_utf8_size(const unsigned char* input, size_t input_len) {
    if (!input) return 0;

    size_t output_len = 0;
    for (size_t i = 0; i < input_len; i++) {
        uint16_t codepoint = iso88595_to_unicode[input[i]];
        if (codepoint <= 0x7F) output_len += 1;
        else if (codepoint <= 0x7FF) output_len += 2;
        else if (codepoint <= 0xFFFF) output_len += 3;
        else output_len += 4;
    }
    return output_len;
}

/* ============================================================================
 * EBCDIC-037 to UTF-8 Converter
 * ============================================================================ */

/* EBCDIC-037 to Unicode mapping table
 * Standard EBCDIC code page 037 (US/Canada) - IBM037
 * Reference: IBM Character Data Representation Architecture */
static const uint16_t ebcdic037_to_unicode[256] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
    0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0020, 0x00A0, 0x00E2, 0x00E4, 0x00E0, 0x00E1, 0x00E3, 0x00E5,
    0x00E7, 0x00F1, 0x00A2, 0x002E, 0x003C, 0x0028, 0x002B, 0x007C,
    0x0026, 0x00E9, 0x00EA, 0x00EB, 0x00E8, 0x00ED, 0x00EE, 0x00EF,
    0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00B6, 0x00C6, 0x00F4, 0x00F6,
    0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3,
    0x00A5, 0x007E, 0x00B7, 0x00AF, 0x0060, 0x0027, 0x003D, 0x005E,
    0x00FF, 0x00F3, 0x00FA, 0x00A1, 0x00B5, 0x00B7, 0x00AA, 0x00AB,
    0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x003F, 0x003F,
    0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
    0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
    0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
    0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
    0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
    0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
    0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
    0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
    0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
    0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003F, 0x003F, 0x003F, 0x003F, 0x003F, 0x003F,
    0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048,
    0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F, 0x003F
};

/**
 * Convert EBCDIC-037 encoded string to UTF-8.
 */
size_t ebcdic037_to_utf8(const unsigned char* input, size_t input_len,
                         char* output, size_t output_size) {
    if (!input || !output) return 0;
    if (input_len == 0) return 0;

    size_t out_pos = 0;

    for (size_t in_pos = 0; in_pos < input_len; in_pos++) {
        uint16_t codepoint = ebcdic037_to_unicode[input[in_pos]];
        if (!write_utf8_char(codepoint, output, output_size, &out_pos)) {
            return 0;  /* Conversion failed */
        }
    }

    return out_pos;
}

/**
 * Calculate the output buffer size needed for EBCDIC-037 to UTF-8 conversion.
 */
size_t ebcdic037_to_utf8_size(const unsigned char* input, size_t input_len) {
    if (!input) return 0;

    size_t output_len = 0;
    for (size_t i = 0; i < input_len; i++) {
        uint16_t codepoint = ebcdic037_to_unicode[input[i]];
        if (codepoint <= 0x7F) output_len += 1;
        else if (codepoint <= 0x7FF) output_len += 2;
        else if (codepoint <= 0xFFFF) output_len += 3;
        else output_len += 4;
    }
    return output_len;
}
