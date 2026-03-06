/* xml_validation.h - Shared XML Validation Functions
 * Copyright (c) 2026, Ribose Inc.
 *
 * UNIFIED VALIDATION API FOR ALL PARSER MODES
 *
 * This module provides shared XML validation functions used by:
 * - ptr_parser.c (copy mode, 72-byte ptr_element)
 * - parser.c (inplace mode, 16-byte compact_element_v2)
 *
 * Design principles:
 * 1. Zero-overhead when inlined (no function call overhead)
 * 2. Consistent naming: xml_validate_* prefix for all functions
 * 3. XML 1.0 compliance for strict mode parsing
 *
 * Usage:
 * - Set strict_mode = 1 in parser to enable validation
 * - All functions return 1 on valid, 0 on invalid
 */

#ifndef TAURUS_XML_VALIDATION_H
#define TAURUS_XML_VALIDATION_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Include shared scanner for character classification */
#include "xml_scanner.h"

/* ============================================================================
 * BRANCH PREDICTION HINTS
 * ============================================================================ */

#if defined(__GNUC__) || defined(__clang__)
    #define VAL_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define VAL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define VAL_LIKELY(x)   (x)
    #define VAL_UNLIKELY(x) (x)
#endif

/* ============================================================================
 * NAME VALIDATION
 * ============================================================================ */

/**
 * Check if character is valid for starting an XML name
 *
 * XML 1.0 NameStartChar: ":" | [A-Z] | "_" | [a-z] | [#xC0-#xD6] | ...
 * For simplicity, we check basic ASCII - extended chars need full Unicode
 *
 * @param c Character to check
 * @return 1 if valid name start, 0 otherwise
 */
inline static int xml_validate_name_start(char c) {
    /* Use shared scanner's name start check */
    return xml_is_name_start(c);
}

/* ============================================================================
 * ATTRIBUTE VALUE VALIDATION
 * ============================================================================ */

/**
 * Validate attribute value for invalid characters
 *
 * XML 1.0 rules:
 * - '<' is not allowed in attribute values
 * - Attribute values can contain entity references
 *
 * @param value Pointer to attribute value
 * @param len   Length of attribute value
 * @return 1 if valid, 0 if invalid
 */
inline static int xml_validate_attr_value(const char* value, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (value[i] == '<') return 0;  /* Less-than not allowed */
    }
    return 1;
}

/* ============================================================================
 * PREDEFINED ENTITY CHECK
 * ============================================================================ */

/**
 * Check if entity name is a predefined XML entity
 *
 * Predefined entities: lt, gt, amp, apos, quot
 *
 * @param name Entity name (without & and ;)
 * @param len  Length of entity name
 * @return 1 if predefined, 0 otherwise
 */
inline static int xml_is_predefined_entity(const char* name, size_t len) {
    if (len == 2 && strncmp(name, "lt", 2) == 0) return 1;
    if (len == 2 && strncmp(name, "gt", 2) == 0) return 1;
    if (len == 3 && strncmp(name, "amp", 3) == 0) return 1;
    if (len == 4 && strncmp(name, "apos", 4) == 0) return 1;
    if (len == 4 && strncmp(name, "quot", 4) == 0) return 1;
    return 0;
}

/* ============================================================================
 * CHARACTER REFERENCE VALIDATION
 * ============================================================================ */

/**
 * Validate character reference and optionally return code point
 *
 * Formats: &#NN; (decimal) or &#xHH; (hexadecimal)
 *
 * @param p        Start of reference content (after &#)
 * @param end      End of input
 * @param out_code Output: Unicode code point (can be NULL)
 * @return 1 if valid, 0 if invalid
 */
inline static int xml_validate_charref(const char* p, const char* end,
                                        uint32_t* out_code) {
    if (VAL_UNLIKELY(p >= end)) return 0;

    int is_hex = 0;
    if (*p == 'x' || *p == 'X') {
        is_hex = 1;
        p++;
    }

    if (VAL_UNLIKELY(p >= end)) return 0;

    uint32_t value = 0;
    int has_digits = 0;

    while (p < end && *p != ';') {
        char c = *p;
        int digit;

        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (is_hex && c >= 'a' && c <= 'f') {
            digit = 10 + c - 'a';
        } else if (is_hex && c >= 'A' && c <= 'F') {
            digit = 10 + c - 'A';
        } else {
            return 0;  /* Invalid digit */
        }

        value = is_hex ? (value * 16 + digit) : (value * 10 + digit);
        has_digits = 1;
        p++;
    }

    if (VAL_UNLIKELY(!has_digits)) return 0;  /* Empty reference */
    if (VAL_UNLIKELY(p >= end || *p != ';')) return 0;  /* Missing semicolon */

    /* Check valid Unicode range (0x0-0x10FFFF, excluding surrogates) */
    if (value > 0x10FFFF) return 0;
    if (value >= 0xD800 && value <= 0xDFFF) return 0;  /* Surrogate range */

    if (out_code) *out_code = value;
    return 1;
}

/* ============================================================================
 * UTF-8 VALIDATION
 * ============================================================================ */

/**
 * Validate a single UTF-8 sequence
 *
 * @param p   Pointer to start of UTF-8 sequence
 * @param end End of input
 * @return Number of bytes in sequence, or 0 if invalid
 */
inline static int xml_validate_utf8_sequence(const char* p, const char* end) {
    if (VAL_UNLIKELY(p >= end)) return 0;

    unsigned char c = (unsigned char)*p;

    /* ASCII - single byte */
    if (c < 0x80) return 1;

    /* 0xFF and 0xFE are never valid in UTF-8 */
    if (VAL_UNLIKELY(c == 0xFF || c == 0xFE)) return 0;

    /* Overlong encodings (C0, C1 lead to overlong 2-byte) */
    if (VAL_UNLIKELY(c == 0xC0 || c == 0xC1)) return 0;

    /* Determine sequence length */
    int expected_bytes;
    if ((c & 0xE0) == 0xC0) expected_bytes = 2;
    else if ((c & 0xF0) == 0xE0) expected_bytes = 3;
    else if ((c & 0xF8) == 0xF0) expected_bytes = 4;
    else return 0;  /* Invalid UTF-8 start byte */

    /* Check continuation bytes */
    for (int i = 1; i < expected_bytes; i++) {
        if (VAL_UNLIKELY(p + i >= end)) return 0;  /* Incomplete sequence */
        unsigned char cont = (unsigned char)p[i];
        if ((cont & 0xC0) != 0x80) return 0;  /* Invalid continuation */
    }

    return expected_bytes;
}

/* ============================================================================
 * TEXT CONTENT VALIDATION
 * ============================================================================ */

/**
 * Validate text content for entity references and UTF-8
 *
 * Checks:
 * 1. Character references (&#NN; and &#xHH;) must be well-formed
 * 2. Entity references must be predefined (lt, gt, amp, apos, quot)
 * 3. All references must end with semicolon
 * 4. UTF-8 sequences must be valid
 *
 * @param p   Start of text content
 * @param end End of text content
 * @return 1 if valid, 0 if invalid
 */
inline static int xml_validate_text_content(const char* p, const char* end) {
    while (p < end) {
        unsigned char c = (unsigned char)*p;

        /* Check for invalid UTF-8 bytes */
        if (VAL_UNLIKELY(c == 0xFF || c == 0xFE)) return 0;
        if (VAL_UNLIKELY(c == 0xC0 || c == 0xC1)) return 0;

        /* Check UTF-8 sequence validity */
        if (c >= 0x80) {
            int bytes = xml_validate_utf8_sequence(p, end);
            if (VAL_UNLIKELY(bytes == 0)) return 0;
            p += bytes;
            continue;
        }

        if (*p == '&') {
            p++;
            if (VAL_UNLIKELY(p >= end)) return 0;  /* & at end */

            if (*p == '#') {
                /* Character reference */
                p++;
                if (!xml_validate_charref(p, end, NULL)) return 0;
                /* Skip to semicolon */
                while (p < end && *p != ';') p++;
                if (VAL_UNLIKELY(p >= end || *p != ';')) return 0;
                p++;  /* Skip semicolon */
            } else {
                /* Entity reference - scan name */
                const char* name_start = p;
                while (p < end && *p != ';' && *p != ' ' && *p != '<' && *p != '&') {
                    p++;
                }
                size_t name_len = p - name_start;

                if (VAL_UNLIKELY(name_len == 0)) return 0;  /* Empty entity name */
                if (VAL_UNLIKELY(p >= end || *p != ';')) return 0;  /* Missing semicolon */

                /* Check if predefined entity */
                if (!xml_is_predefined_entity(name_start, name_len)) {
                    return 0;  /* Undefined entity */
                }
                p++;  /* Skip semicolon */
            }
        } else {
            p++;
        }
    }
    return 1;
}

/* ============================================================================
 * COMMENT VALIDATION
 * ============================================================================ */

/**
 * Validate comment content
 *
 * XML 1.0 rules:
 * 1. Comment content must not contain "--"
 * 2. Comment content must not end with "-"
 *
 * @param p   Start of comment content (after <!--)
 * @param end End of comment content (before -->)
 * @return 1 if valid, 0 if invalid
 */
inline static int xml_validate_comment(const char* p, const char* end) {
    /* Check if content ends with "-" (which would make "--->" or similar invalid) */
    if (p < end && *(end - 1) == '-') {
        return 0;  /* Content ends with dash - invalid */
    }

    /* Check for "--" inside the content */
    while (p + 1 < end) {
        if (p[0] == '-' && p[1] == '-') {
            return 0;  /* "--" inside comment is invalid */
        }
        p++;
    }

    return 1;
}

/* ============================================================================
 * PROCESSING INSTRUCTION VALIDATION
 * ============================================================================ */

/**
 * Validate processing instruction target name
 *
 * XML 1.0 rules:
 * - Target must be a valid XML name
 * - Target must not be "xml" (case-insensitive)
 *
 * @param target PI target name
 * @param len    Length of target
 * @return 1 if valid, 0 if invalid
 */
inline static int xml_validate_pi_target(const char* target, size_t len) {
    if (VAL_UNLIKELY(len == 0)) return 0;

    /* Check first character is valid name start */
    if (!xml_is_name_start(*target)) return 0;

    /* Check remaining characters are valid name chars */
    for (size_t i = 1; i < len; i++) {
        if (!xml_is_name_char(target[i])) return 0;
    }

    /* Check target is not "xml" (case-insensitive) */
    if (len == 3) {
        if ((target[0] == 'x' || target[0] == 'X') &&
            (target[1] == 'm' || target[1] == 'M') &&
            (target[2] == 'l' || target[2] == 'L')) {
            return 0;
        }
    }

    return 1;
}

#endif /* TAURUS_XML_VALIDATION_H */
