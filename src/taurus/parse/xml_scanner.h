/* xml_scanner.h - Shared XML Scanning Primitives
 * Copyright (c) 2026, Ribose Inc.
 *
 * UNIFIED SCANNER API FOR ALL PARSER MODES
 *
 * This module provides shared XML scanning primitives used by:
 * - ptr_parser.c (copy mode, 72-byte ptr_element)
 * - compact_parser.c (inplace mode, 16-byte compact_element_v2)
 *
 * Design principles:
 * 1. Zero-overhead when inlined (no function call overhead)
 * 2. SIMD-optimized for hot paths (whitespace, name scanning)
 * 3. Consistent naming: xml_* prefix for all public functions
 * 4. Clear boundary semantics: [start, end) half-open intervals
 *
 * Naming convention:
 * - xml_is_*()     : Character classification (returns int 0/1)
 * - xml_scan_*()   : Scanning functions (returns pointer)
 * - xml_check_*()  : Validation functions (returns int 0/1)
 *
 * Performance targets:
 * - Whitespace: 16 bytes/cycle with SIMD
 * - Name scan: 8 bytes/cycle with SIMD
 * - Scalar fallback: 1 byte/cycle
 */

#ifndef TAURUS_XML_SCANNER_H
#define TAURUS_XML_SCANNER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Include SIMD helpers for optimized implementations */
#include "../simd_helpers.h"

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/* SIMD threshold: use SIMD for strings >= this length */
#ifndef XML_SCANNER_SIMD_THRESHOLD
#define XML_SCANNER_SIMD_THRESHOLD 16
#endif

/* ============================================================================
 * CHARACTER CLASSIFICATION - BASIC MACROS
 * ============================================================================ */

/**
 * Check if character is XML whitespace (space, tab, newline, carriage return)
 * XML 1.0 spec: S ::= (#x20 | #x9 | #xD | #xA)+
 */
#define xml_is_space(c) \
    ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')

/**
 * Check if character can start an XML name
 * XML 1.0 spec: NameStartChar ::= ":" | [A-Z] | "_" | [a-z]
 * Note: Full XML 1.0 also allows Unicode chars, but we keep ASCII for performance
 */
#define xml_is_name_start(c) \
    (((c) >= 'a' && (c) <= 'z') || \
     ((c) >= 'A' && (c) <= 'Z') || \
     (c) == '_' || (c) == ':')

/**
 * Check if character can be in an XML name (after first char)
 * XML 1.0 spec: NameChar ::= NameStartChar | "-" | "." | [0-9]
 */
#define xml_is_name_char(c) \
    (xml_is_name_start(c) || \
     ((c) >= '0' && (c) <= '9') || \
     (c) == '-' || (c) == '.')

/**
 * Check if character is a decimal digit
 */
#define xml_is_digit(c) \
    ((c) >= '0' && (c) <= '9')

/**
 * Check if character is a hexadecimal digit
 */
#define xml_is_hexdigit(c) \
    (((c) >= '0' && (c) <= '9') || \
     ((c) >= 'a' && (c) <= 'f') || \
     ((c) >= 'A' && (c) <= 'F'))

/**
 * Check if byte is a UTF-8 continuation byte (10xxxxxx)
 */
#define xml_is_utf8_continuation(c) \
    (((unsigned char)(c) & 0xC0) == 0x80)

/* ============================================================================
 * BRANCH PREDICTION HINTS
 * ============================================================================ */

#if defined(__GNUC__) || defined(__clang__)
    #define XML_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define XML_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define XML_LIKELY(x)   (x)
    #define XML_UNLIKELY(x) (x)
#endif

/* ============================================================================
 * WHITESPACE SCANNING
 * ============================================================================ */

/**
 * Scan past whitespace characters
 *
 * @param start Start of input (inclusive)
 * @param end   End of input (exclusive)
 * @return Pointer to first non-whitespace character, or end if all whitespace
 *
 * Uses SIMD when available for 16+ byte sequences.
 */
inline static const char* xml_scan_whitespace(const char* start, const char* end) {
    if (XML_UNLIKELY(!start || !end || start >= end)) {
        return start ? start : end;
    }

    /* Use SIMD-optimized version from simd_helpers.h */
    return simd_skip_whitespace(start, end);
}

/**
 * Check if a range contains only whitespace characters
 *
 * @param start Start of input (inclusive)
 * @param end   End of input (exclusive)
 * @return 1 if all whitespace (or empty), 0 if any non-whitespace
 *
 * Uses SIMD when available for 16+ byte sequences.
 */
inline static int xml_is_whitespace_only(const char* start, const char* end) {
    if (XML_UNLIKELY(!start || !end || start >= end)) {
        return 1;  /* Empty range is whitespace-only */
    }

    /* Use SIMD-optimized version from simd_helpers.h */
    return simd_is_whitespace_only(start, end);
}

/* ============================================================================
 * NAME SCANNING
 * ============================================================================ */

/**
 * Scan an XML name and return pointer to first non-name character
 *
 * @param start Start of input (must point to valid name start char)
 * @param end   End of input (exclusive)
 * @return Pointer past the name, or start if no valid name found
 *
 * Note: Caller should verify first character is valid name start.
 * This function scans name characters until it finds a non-name char.
 *
 * Uses SIMD when available for 16+ byte sequences.
 */
inline static const char* xml_scan_name(const char* start, const char* end) {
    if (XML_UNLIKELY(!start || !end || start >= end)) {
        return start ? start : end;
    }

    /* First character must be valid name start */
    if (XML_UNLIKELY(!xml_is_name_start(*start))) {
        return start;
    }

    /* Use SIMD-optimized scanner from simd_helpers.h for longer names */
    if (end - start >= XML_SCANNER_SIMD_THRESHOLD) {
        return simd_scan_name(start, end);
    }

    /* Scalar fallback for short names */
    const char* p = start + 1;
    while (p < end && xml_is_name_char(*p)) {
        p++;
    }

    return p;
}

/**
 * Scan a name and get its length
 *
 * @param start    Start of input
 * @param end      End of input
 * @param out_len  Output: length of name (0 if invalid)
 * @return Pointer past the name, or start if invalid
 */
inline static const char* xml_scan_name_ex(const char* start, const char* end,
                                           size_t* out_len) {
    const char* result = xml_scan_name(start, end);

    if (out_len) {
        *out_len = (result > start) ? (size_t)(result - start) : 0;
    }

    return result;
}

/* ============================================================================
 * ATTRIBUTE VALUE SCANNING
 * ============================================================================ */

/**
 * Scan a quoted attribute value
 *
 * @param start     Start of input (should point to opening quote)
 * @param end       End of input
 * @param out_value Output: pointer to value content (inside quotes)
 * @param out_len   Output: length of value content
 * @return Pointer past closing quote, or start if not a quoted value
 */
inline static const char* xml_scan_attr_value(const char* start, const char* end,
                                              const char** out_value, size_t* out_len) {
    if (XML_UNLIKELY(!start || !end || start >= end)) {
        if (out_value) *out_value = NULL;
        if (out_len) *out_len = 0;
        return start ? start : end;
    }

    /* Check for quote character */
    char quote = *start;
    if (quote != '"' && quote != '\'') {
        if (out_value) *out_value = NULL;
        if (out_len) *out_len = 0;
        return start;
    }

    const char* value_start = start + 1;
    const char* p = value_start;

    /* Scan for closing quote */
    while (p < end && *p != quote) {
        if (*p == '<') {
            /* '<' not allowed in attribute values */
            if (out_value) *out_value = NULL;
            if (out_len) *out_len = 0;
            return start;
        }
        p++;
    }

    if (p >= end) {
        /* No closing quote found */
        if (out_value) *out_value = NULL;
        if (out_len) *out_len = 0;
        return start;
    }

    /* Success */
    if (out_value) *out_value = value_start;
    if (out_len) *out_len = (size_t)(p - value_start);

    return p + 1;  /* Past closing quote */
}

/* ============================================================================
 * TEXT CONTENT SCANNING
 * ============================================================================ */

/**
 * Scan text content until end tag or special character
 *
 * @param start     Start of text content
 * @param end       End of input
 * @param out_end   Output: pointer to end of text content
 * @return Type of terminator found: '<', '&', or '\0'
 */
inline static char xml_scan_text(const char* start, const char* end,
                                  const char** out_end) {
    if (XML_UNLIKELY(!start || !end || start >= end)) {
        if (out_end) *out_end = start ? start : end;
        return '\0';
    }

    const char* p = start;

    /* Scan for special characters */
    while (p < end) {
        char c = *p;
        if (c == '<' || c == '&') {
            if (out_end) *out_end = p;
            return c;
        }
        p++;
    }

    if (out_end) *out_end = p;
    return '\0';
}

/* ============================================================================
 * SIMD FIND CHARACTERS
 * ============================================================================ */

/**
 * Find first occurrence of a character using SIMD
 *
 * @param start Start of search range
 * @param end   End of search range
 * @param c     Character to find
 * @return Pointer to first occurrence, or end if not found
 */
inline static const char* xml_find_char(const char* start, const char* end, char c) {
    if (XML_UNLIKELY(!start || !end || start >= end)) {
        return end;
    }

    /* Use SIMD version from simd_helpers.h */
    return simd_find_char(start, end, c);
}

/**
 * Find first occurrence of any character from a set using SIMD
 *
 * @param start Start of search range
 * @param end   End of search range
 * @param chars Null-terminated string of characters to find
 * @return Pointer to first occurrence, or end if none found
 */
inline static const char* xml_find_char_any(const char* start, const char* end,
                                             const char* chars) {
    if (XML_UNLIKELY(!start || !end || start >= end || !chars)) {
        return end;
    }

    /* Use SIMD version from simd_helpers.h */
    return simd_find_char_any(start, end, chars);
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Skip XML declaration (<?xml ... ?>)
 *
 * @param start Start of input
 * @param end   End of input
 * @return Pointer past declaration, or start if not a declaration
 */
inline static const char* xml_skip_declaration(const char* start, const char* end) {
    if (XML_UNLIKELY(!start || !end || start >= end)) {
        return start ? start : end;
    }

    /* Check for <?xml */
    if (end - start < 5) return start;
    if (start[0] != '<' || start[1] != '?') return start;

    /* Find closing ?> */
    const char* p = start + 2;
    while (p < end - 1) {
        if (p[0] == '?' && p[1] == '>') {
            return p + 2;
        }
        p++;
    }

    return start;  /* No closing ?> found */
}

/**
 * Skip XML comment (<!-- ... -->)
 *
 * @param start Start of input
 * @param end   End of input
 * @return Pointer past comment, or start if not a comment
 */
inline static const char* xml_skip_comment(const char* start, const char* end) {
    if (XML_UNLIKELY(!start || !end || start >= end)) {
        return start ? start : end;
    }

    /* Check for <!-- */
    if (end - start < 4) return start;
    if (start[0] != '<' || start[1] != '!' || start[2] != '-' || start[3] != '-') {
        return start;
    }

    /* Find closing --> */
    const char* p = start + 4;
    while (p < end - 2) {
        if (p[0] == '-' && p[1] == '-' && p[2] == '>') {
            return p + 3;
        }
        p++;
    }

    return start;  /* No closing --> found */
}

/**
 * Skip CDATA section (<![CDATA[ ... ]]>)
 *
 * @param start Start of input
 * @param end   End of input
 * @return Pointer past CDATA, or start if not CDATA
 */
inline static const char* xml_skip_cdata(const char* start, const char* end) {
    if (XML_UNLIKELY(!start || !end || start >= end)) {
        return start ? start : end;
    }

    /* Check for <![CDATA[ */
    if (end - start < 9) return start;
    if (memcmp(start, "<![CDATA[", 9) != 0) return start;

    /* Find closing ]]> */
    const char* p = start + 9;
    while (p < end - 2) {
        if (p[0] == ']' && p[1] == ']' && p[2] == '>') {
            return p + 3;
        }
        p++;
    }

    return start;  /* No closing ]]> found */
}

#endif /* TAURUS_XML_SCANNER_H */
