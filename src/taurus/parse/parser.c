/* parser_v5.c - Ultra-Fast 16-Byte Parser with Zero-Check Allocator
 * Copyright (c) 2026, Ribose Inc.
 *
 * MAXIMUM PERFORMANCE PARSER - Target: 1.0x vs pugixml
 *
 * Key optimizations over v2:
 * 1. Zero-check allocator - pure bump pointer, NO size checks (2 cycles saved)
 * 2. Direct offset returns - no pointer-to-offset conversion
 * 3. In-place null-termination - no deferred tracking overhead
 * 4. SIMD character scanning - uses simd_helpers.h for fast scanning
 *
 * This is simpler than v4 and should be faster than v2.
 */

#include "compact_element.h"
#include "../memory/zero_check_alloc.h"
#include "../simd_helpers.h"  /* SIMD optimization functions */
#include "xml_scanner.h"      /* Shared XML scanning primitives */
#include "../taurus_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Parser State for v5
 * ============================================================================ */

#define MAX_STACK_DEPTH 1024

typedef struct {
    uint32_t elem_offset;
    uint32_t last_child_off;
    uint16_t name_len;      /* Store name length to avoid strlen() in closing tag */
    uint16_t padding;       /* Alignment padding */
    struct compact_element_v2* parent_ptr;  /* Cached pointer to avoid OFFSET_TO_TYPED */
} StackEntryV5;

typedef struct {
    const char* pos;
    const char* end;
    char* string_base;      /* Base for string offsets */
    char* node_base;        /* Node memory base (from allocator) */
    ZeroCheckAlloc* alloc;

    /* Fixed stack */
    StackEntryV5 stack[MAX_STACK_DEPTH];
    size_t stack_size;

    /* Error tracking */
    int has_error;          /* Non-zero if parse error occurred */
    int strict_mode;        /* Strict XML validation mode */
    int got_root;           /* Track if we already have a root element */
} ParserV5;

/* ============================================================================
 * Inline Scanning Functions
 *
 * Uses shared scanner from xml_scanner.h for consistent behavior.
 * ============================================================================ */

#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#else
#define FORCE_INLINE __attribute__((always_inline)) inline
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

/* Optimized name scanning - uses shared scanner with SIMD */
static FORCE_INLINE const char* scan_name_v5(const char* p, const char* end) {
    /* Use shared scanner - it handles SIMD threshold internally */
    return xml_scan_name(p, end);
}

/* Optimized whitespace skip - uses shared scanner with SIMD */
static FORCE_INLINE const char* skip_ws_v5(const char* p, const char* end) {
    return xml_scan_whitespace(p, end);
}

/* Check if range is whitespace only - uses shared scanner with SIMD */
static FORCE_INLINE int is_ws_only_v5(const char* p, const char* end) {
    return xml_is_whitespace_only(p, end);
}

/* ============================================================================
 * Strict Mode Validation Functions
 * ============================================================================ */

/* Check if character is valid for starting an XML name */
static int validate_name_start_strict(char c) {
    /* XML 1.0 NameStartChar: ":" | [A-Z] | "_" | [a-z] | [#xC0-#xD6] | [#xD8-#xF6] | [#xF8-#x2FF] | ...
     * For simplicity, we check basic ASCII - extended chars would need full Unicode support */
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c == '_' || c == ':') return 1;
    /* Reject digits, dot, hyphen at start */
    return 0;
}

/* Validate attribute value - check for invalid characters in strict mode */
static int validate_attr_value_strict(const char* value, size_t len) {
    for (size_t i = 0; i < len; i++) {
        /* Less-than is not allowed in attribute values */
        if (value[i] == '<') return 0;
    }
    return 1;
}

/* Validate character reference and return code point */
static int validate_charref_strict(const char* p, const char* end, uint32_t* out_code) {
    if (p >= end) return 0;

    int is_hex = 0;
    if (*p == 'x' || *p == 'X') {
        is_hex = 1;
        p++;
    }

    if (p >= end) return 0;

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

    if (!has_digits) return 0;  /* Empty reference */
    if (p >= end || *p != ';') return 0;  /* Missing semicolon */

    /* Check valid Unicode range (0x0-0x10FFFF, excluding surrogates) */
    if (value > 0x10FFFF) return 0;
    if (value >= 0xD800 && value <= 0xDFFF) return 0;  /* Surrogate range */

    *out_code = value;
    return 1;
}

/* Check for predefined entity */
static int is_predefined_entity(const char* name, size_t len) {
    if (len == 2 && strncmp(name, "lt", 2) == 0) return 1;
    if (len == 2 && strncmp(name, "gt", 2) == 0) return 1;
    if (len == 3 && strncmp(name, "amp", 3) == 0) return 1;
    if (len == 4 && strncmp(name, "apos", 4) == 0) return 1;
    if (len == 4 && strncmp(name, "quot", 4) == 0) return 1;
    return 0;
}

/* Validate text content for entity references
 * Returns 1 if valid, 0 if invalid
 *
 * Checks:
 * 1. Character references (&#NN; and &#xHH;) must be well-formed
 * 2. Entity references must be predefined (lt, gt, amp, apos, quot)
 * 3. All references must end with semicolon
 * 4. UTF-8 sequences must be valid (in strict mode)
 */
static int validate_text_content_strict(const char* p, const char* end) {
    while (p < end) {
        unsigned char c = (unsigned char)*p;

        /* Check for invalid bytes in UTF-8 */
        /* 0xFF and 0xFE are never valid in UTF-8 */
        if (c == 0xFF || c == 0xFE) {
            return 0;
        }

        /* Check for overlong encodings (C0, C1 lead to overlong 2-byte) */
        if (c == 0xC0 || c == 0xC1) {
            return 0;
        }

        /* Check UTF-8 sequence validity */
        if (c >= 0x80) {
            /* Multi-byte sequence */
            int expected_bytes;
            if ((c & 0xE0) == 0xC0) expected_bytes = 2;
            else if ((c & 0xF0) == 0xE0) expected_bytes = 3;
            else if ((c & 0xF8) == 0xF0) expected_bytes = 4;
            else return 0;  /* Invalid UTF-8 start byte */

            /* Check continuation bytes */
            for (int i = 1; i < expected_bytes; i++) {
                if (p + i >= end) return 0;  /* Incomplete sequence */
                unsigned char cont = (unsigned char)p[i];
                if ((cont & 0xC0) != 0x80) return 0;  /* Invalid continuation */
            }

            /* Skip the multi-byte sequence */
            p += expected_bytes;
            continue;
        }

        if (*p == '&') {
            const char* ref_start = p;
            p++;

            if (p >= end) return 0;  /* & at end */

            if (*p == '#') {
                /* Character reference */
                p++;
                uint32_t code;
                if (!validate_charref_strict(p, end, &code)) {
                    return 0;
                }
                /* Skip to semicolon */
                while (p < end && *p != ';') p++;
                if (p >= end || *p != ';') return 0;
                p++;  /* Skip semicolon */
            } else {
                /* Entity reference - scan name */
                const char* name_start = p;
                while (p < end && *p != ';' && *p != ' ' && *p != '<' && *p != '&') {
                    p++;
                }
                size_t name_len = p - name_start;

                if (name_len == 0) return 0;  /* Empty entity name */
                if (p >= end || *p != ';') return 0;  /* Missing semicolon */

                /* Check if predefined entity */
                if (!is_predefined_entity(name_start, name_len)) {
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

/* Validate comment content
 * XML 1.0 rules:
 * 1. Comment content must not contain "--"
 * 2. Comment content must not end with "-"
 * 3. Comment must end with "-->"
 */
static int validate_comment_strict(const char* p, const char* end) {
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
 * Stack Operations - Inline
 * ============================================================================ */

#define STACK_PUSH_V5(p, off, nlen, ptr) do { \
    if ((p)->stack_size < MAX_STACK_DEPTH) { \
        (p)->stack[(p)->stack_size].elem_offset = (off); \
        (p)->stack[(p)->stack_size].last_child_off = 0; \
        (p)->stack[(p)->stack_size].name_len = (uint16_t)(nlen); \
        (p)->stack[(p)->stack_size].parent_ptr = (ptr); \
        (p)->stack_size++; \
    } \
} while(0)

#define STACK_PEEK_V5(p) (&(p)->stack[(p)->stack_size - 1])
#define STACK_POP_V5(p) do { (p)->stack_size--; } while(0)

/* ============================================================================
 * Zero-Check Allocation - Returns offset directly
 * ============================================================================ */

#define ALLOC_ELEM_V5(p) ALLOC_16((p)->alloc)
#define ALLOC_ATTR_V5(p) ALLOC_16((p)->alloc)
#define ALLOC_TEXT_V5(p) ALLOC_16((p)->alloc)

/* ============================================================================
 * Attribute Parsing (v5) - Direct null-termination
 * ============================================================================ */

static uint32_t parse_attr_v5(ParserV5* p) {
    const char* orig_pos = p->pos;  /* Save original position for restoration on failure */
    const char* name_start = p->pos;
    p->pos = scan_name_v5(p->pos, p->end);
    size_t name_len = p->pos - name_start;
    if (name_len == 0) return 0;

    /* Save character at end of name (don't null-terminate yet!) */
    char saved_name_char = *(p->pos);

    /* Skip to '=' (before null-terminating) */
    p->pos = skip_ws_v5(p->pos, p->end);
    if (p->pos >= p->end || *p->pos != '=') {
        /* Malformed attribute - name without '='
         * This is a parse error in strict mode */
        p->has_error = 1;
        p->pos = orig_pos;
        return 0;
    }
    p->pos++;

    /* Skip to quote */
    p->pos = skip_ws_v5(p->pos, p->end);
    if (p->pos >= p->end) {
        p->has_error = 1;
        p->pos = orig_pos;
        return 0;
    }
    char quote = *p->pos;
    if (quote != '"' && quote != '\'') {
        /* Malformed attribute - no quote after '=' */
        p->has_error = 1;
        p->pos = orig_pos;
        return 0;
    }
    p->pos++;

    /* Parse value */
    const char* value_start = p->pos;
    while (p->pos < p->end && *p->pos != quote) p->pos++;
    size_t value_len = p->pos - value_start;

    /* Check for unterminated quote */
    if (p->pos >= p->end) {
        p->has_error = 1;
        p->pos = orig_pos;
        return 0;
    }

    /* STRICT MODE: Check for invalid characters in attribute value */
    if (p->strict_mode && !validate_attr_value_strict(value_start, value_len)) {
        p->has_error = 1;
        p->pos = orig_pos;
        return 0;
    }

    /* STRICT MODE: Validate namespace declarations */
    if (p->strict_mode && name_len > 6 && strncmp(name_start, "xmlns:", 6) == 0) {
        /* This is a namespace declaration xmlns:prefix="uri" */
        const char* prefix = name_start + 6;
        size_t prefix_len = name_len - 6;

        /* Validate prefix name starts with valid name start character */
        if (prefix_len > 0) {
            char first_char = prefix[0];
            /* Check for valid name start char (letter or underscore) */
            if (!((first_char >= 'a' && first_char <= 'z') ||
                  (first_char >= 'A' && first_char <= 'Z') ||
                  first_char == '_' || first_char == ':')) {
                p->has_error = 1;
                p->pos = orig_pos;
                return 0;
            }
        }

        /* Check reserved 'xml' prefix - must have correct URI */
        if (prefix_len == 3 && strncmp(prefix, "xml", 3) == 0) {
            /* The xml prefix is reserved and must be bound to
             * http://www.w3.org/XML/1998/namespace */
            const char* expected_uri = "http://www.w3.org/XML/1998/namespace";
            if (value_len != 36 || strncmp(value_start, expected_uri, 36) != 0) {
                p->has_error = 1;
                p->pos = orig_pos;
                return 0;
            }
        }
    }

    /* Skip closing quote */
    p->pos++;

    /* NOW we know it's a valid attribute - null-terminate both strings */
    ((char*)name_start)[name_len] = '\0';
    ((char*)value_start)[value_len] = '\0';

    /* Allocate - check for out of memory */
    uint32_t attr_off = ALLOC_ATTR_V5(p);
    if (attr_off == UINT32_MAX) return 0;  /* Out of memory */

    struct compact_attribute_v2* attr = OFFSET_TO_TYPED(p->node_base, attr_off, struct compact_attribute_v2);

    attr->name_offset = (uint32_t)(name_start - p->string_base) | 0x80000000;
    attr->value_offset = (uint32_t)(value_start - p->string_base);
    attr->next_attr = UINT32_MAX;  /* Use UINT32_MAX for null */
    attr->flags = 0;

    /* After attribute value, we MUST see whitespace, '>', or '/'
     * If we see a name character directly after the quote, it's an error */
    if (p->pos < p->end && xml_is_name_start(*p->pos)) {
        /* Missing separator between attributes */
        p->has_error = 1;
        return 0;
    }

    /* Skip whitespace after attribute for next iteration */
    p->pos = skip_ws_v5(p->pos, p->end);

    return attr_off;
}

/* ============================================================================
 * Main Parsing Loop - v5 with Zero-Check Allocator
 * ============================================================================ */

static uint32_t parse_v5_main(ParserV5* p) {
    uint32_t root_off = 0;
    int got_root = 0;

    while (p->pos < p->end) {
        /* Only skip whitespace at document level (between elements).
         * Inside elements, whitespace is significant text content. */
        StackEntryV5* ctx = (p->stack_size > 0) ? STACK_PEEK_V5(p) : NULL;
        if (!ctx) {
            p->pos = skip_ws_v5(p->pos, p->end);
            if (p->pos >= p->end) break;
        }

        /* Text content - LIKELY path since most chars are text */
        if (LIKELY(*p->pos != '<')) {
            if (UNLIKELY(!ctx)) { p->pos++; continue; }

            const char* text_start = p->pos;

            /* SIMD-optimized text scanning - find '<' at 16 bytes/iteration */
            const char* found = simd_find_char(p->pos, p->end, '<');
            p->pos = found ? found : p->end;

            /* Whitespace-only text is UNLIKELY - skip it */
            if (UNLIKELY(is_ws_only_v5(text_start, p->pos))) continue;

            size_t text_len = p->pos - text_start;

            /* STRICT MODE: Validate text content (entities and UTF-8) */
            if (UNLIKELY(p->strict_mode) && !validate_text_content_strict(text_start, p->pos)) {
                p->has_error = 1;
                /* Skip to next tag */
                continue;
            }

            /* CRITICAL FIX: Save the char we're about to overwrite */
            char saved_char = *(p->pos);

            /* Null-terminate text IN PLACE - but restore before processing next tag */
            ((char*)text_start)[text_len] = '\0';

            /* Allocate text node - check for out of memory */
            uint32_t text_off = ALLOC_TEXT_V5(p);
            if (UNLIKELY(text_off == UINT32_MAX)) continue;  /* Out of memory, skip this node */

            struct compact_text_v2* text = OFFSET_TO_TYPED(p->node_base, text_off, struct compact_text_v2);

            text->text_offset = (uint32_t)(text_start - p->string_base);
            text->next_sibling = UINT32_MAX;  /* Use UINT32_MAX for null (0 is valid offset) */
            text->text_length = (uint32_t)text_len;
            text->flags = COMPACT_V2_TEXT_MARKER | COMPACT_V2_TYPE_TEXT;

            /* Link to parent */
            struct compact_element_v2* parent = OFFSET_TO_TYPED(p->node_base, ctx->elem_offset, struct compact_element_v2);

            if (ctx->last_child_off == 0) {
                /* Check if parent already has attributes (high bit set on first field of first_child) */
                if (parent->first_child != UINT32_MAX) {
                    uint32_t first_field = *(uint32_t*)(p->node_base + parent->first_child);
                    if (first_field & 0x80000000) {
                        /* Parent has attributes - find last attribute and link text to it */
                        struct compact_attribute_v2* attr = (struct compact_attribute_v2*)(p->node_base + parent->first_child);
                        while (attr->next_attr != 0 && attr->next_attr != UINT32_MAX) {
                            uint32_t next_field = *(uint32_t*)(p->node_base + attr->next_attr);
                            if (!(next_field & 0x80000000)) {
                                /* Not an attribute - stop here */
                                break;
                            }
                            attr = (struct compact_attribute_v2*)(p->node_base + attr->next_attr);
                        }
                        /* Link text node after attributes */
                        attr->next_attr = text_off;
                    } else {
                        /* No attributes - just set first_child */
                        parent->first_child = text_off;
                    }
                } else {
                    /* No first_child - just set it */
                    parent->first_child = text_off;
                }
            } else {
                uint32_t* sibling_ptr = (uint32_t*)(p->node_base + ctx->last_child_off + 4);
                *sibling_ptr = text_off;
            }
            ctx->last_child_off = text_off;

            /* CRITICAL FIX: Restore the '<' so closing tag detection works */
            ((char*)text_start)[text_len] = saved_char;

            continue;
        }

        /* We have '<' */
        p->pos++;
        char c = *p->pos;

        /* Closing tag */
        if (c == '/') {
            p->pos++;

            /* Check if stack is empty (no matching open tag) */
            if (p->stack_size == 0) {
                p->has_error = 1;
                while (p->pos < p->end && *p->pos != '>') p->pos++;
                if (p->pos < p->end) p->pos++;
                continue;
            }

            /* Parse closing tag name */
            const char* close_name_start = p->pos;
            p->pos = scan_name_v5(p->pos, p->end);
            size_t close_name_len = p->pos - close_name_start;

            /* Get expected name from stack */
            StackEntryV5* entry = STACK_PEEK_V5(p);
            /* Use cached pointer - NO OFFSET_TO_TYPED needed! */
            struct compact_element_v2* parent = entry->parent_ptr;
            const char* expected_name = p->string_base + COMPACT_V2_NAME_OFF(parent);
            size_t expected_len = entry->name_len;  /* Use stored length - NO strlen()! */

            /* Check for name mismatch */
            if (close_name_len != expected_len ||
                memcmp(close_name_start, expected_name, expected_len) != 0) {
                p->has_error = 1;
            }

            /* Skip whitespace before '>' */
            const char* after_name = p->pos;
            p->pos = skip_ws_v5(p->pos, p->end);

            /* Check for extra content (non-whitespace) before '>' */
            if (p->pos < p->end && *p->pos != '>') {
                p->has_error = 1;
                /* Skip to '>' to continue parsing */
                while (p->pos < p->end && *p->pos != '>') p->pos++;
            }

            /* Skip '>' */
            if (p->pos < p->end) p->pos++;

            STACK_POP_V5(p);
            continue;
        }

        /* Special nodes: PI, Comment, CDATA, DOCTYPE */
        if (c == '?' || c == '!') {
            /* Check for Processing Instruction */
            if (c == '?') {
                /* PI: <?target ...?> */
                const char* pi_target_start = p->pos + 1;  /* After <? */
                const char* pi_scan = pi_target_start;

                /* Scan PI target name */
                while (pi_scan < p->end &&
                       (*pi_scan == '_' || *pi_scan == ':' ||
                        (*pi_scan >= 'a' && *pi_scan <= 'z') ||
                        (*pi_scan >= 'A' && *pi_scan <= 'Z') ||
                        (*pi_scan >= '0' && *pi_scan <= '9') ||
                        *pi_scan == '.' || *pi_scan == '-')) {
                    pi_scan++;
                }
                size_t pi_target_len = pi_scan - pi_target_start;

                /* STRICT MODE: Check for reserved 'xml' target (case-insensitive)
                 * Only the XML declaration <?xml ...?> is allowed to use this target */
                if (p->strict_mode && pi_target_len == 3) {
                    if ((pi_target_start[0] == 'x' || pi_target_start[0] == 'X') &&
                        (pi_target_start[1] == 'm' || pi_target_start[1] == 'M') &&
                        (pi_target_start[2] == 'l' || pi_target_start[2] == 'L')) {
                        p->has_error = 1;
                    }
                }

                /* Skip to ?> */
                while (p->pos + 1 < p->end) {
                    if (p->pos[0] == '?' && p->pos[1] == '>') {
                        p->pos += 2;
                        break;
                    }
                    p->pos++;
                }
                continue;
            }

            /* Check for CDATA section */
            if (c == '!' && (p->pos + 1) < p->end && p->pos[1] == '[') {
                /* Potential CDATA: <![CDATA[...]]> or <![...]]> */
                if ((p->pos + 8) < p->end &&
                    p->pos[2] == 'C' && p->pos[3] == 'D' &&
                    p->pos[4] == 'A' && p->pos[5] == 'T' &&
                    p->pos[6] == 'A' && p->pos[7] == '[') {
                    /* CDATA section: extract content */
                    const char* cdata_start = p->pos + 8;  /* After <![CDATA[ */
                    p->pos = cdata_start;

                    /* Find ]]> */
                    while (p->pos + 2 < p->end) {
                        if (p->pos[0] == ']' && p->pos[1] == ']' && p->pos[2] == '>') {
                            break;
                        }
                        p->pos++;
                    }

                    if (ctx && p->pos > cdata_start) {
                        size_t cdata_len = p->pos - cdata_start;

                        /* Null-terminate CDATA content IN PLACE */
                        ((char*)cdata_start)[cdata_len] = '\0';

                        /* Allocate CDATA text node */
                        uint32_t text_off = ALLOC_TEXT_V5(p);
                        struct compact_text_v2* text = OFFSET_TO_TYPED(p->node_base, text_off, struct compact_text_v2);

                        text->text_offset = (uint32_t)(cdata_start - p->string_base);
                        text->next_sibling = UINT32_MAX;  /* Use UINT32_MAX for null */
                        text->text_length = (uint32_t)cdata_len;
                        text->flags = COMPACT_V2_TEXT_MARKER | COMPACT_V2_TYPE_CDATA;

                        /* Link to parent */
                        struct compact_element_v2* parent = OFFSET_TO_TYPED(p->node_base, ctx->elem_offset, struct compact_element_v2);

                        if (ctx->last_child_off == 0) {
                            parent->first_child = text_off;
                        } else {
                            uint32_t* sibling_ptr = (uint32_t*)(p->node_base + ctx->last_child_off + 4);
                            *sibling_ptr = text_off;
                        }
                        ctx->last_child_off = text_off;
                    }

                    /* Skip ]]> */
                    if (p->pos + 2 < p->end) p->pos += 3;
                    continue;
                }
            }

            /* Skip other special nodes (PI, Comment, DOCTYPE) */
            /* Check for comment */
            if (c == '!' && (p->pos + 1) < p->end && p->pos[1] == '-' && (p->pos + 2) < p->end && p->pos[2] == '-') {
                /* Comment: <!-- ... --> */
                const char* comment_start = p->pos + 3;  /* After <!-- */
                p->pos = comment_start;

                /* Find --> */
                while (p->pos + 2 < p->end) {
                    if (p->pos[0] == '-' && p->pos[1] == '-' && p->pos[2] == '>') {
                        break;
                    }
                    p->pos++;
                }

                /* STRICT MODE: Validate comment content */
                if (p->strict_mode && !validate_comment_strict(comment_start, p->pos)) {
                    p->has_error = 1;
                    continue;
                }

                /* Check for unclosed comment */
                if (p->pos + 2 >= p->end) {
                    if (p->strict_mode) {
                        p->has_error = 1;
                    }
                    continue;
                }

                /* Skip --> */
                p->pos += 3;
                continue;
            }

            while (p->pos < p->end && *p->pos != '>') p->pos++;
            if (p->pos < p->end) p->pos++;
            continue;
        }

        /* Parse element name */
        const char* name_start = p->pos;
        p->pos = scan_name_v5(p->pos, p->end);
        size_t name_len = p->pos - name_start;
        if (name_len == 0) continue;

        /* STRICT MODE: Validate name start character */
        if (p->strict_mode && !validate_name_start_strict(*name_start)) {
            p->has_error = 1;
            /* Skip to end of tag */
            while (p->pos < p->end && *p->pos != '>') p->pos++;
            if (p->pos < p->end && *p->pos == '>') p->pos++;
            continue;
        }

        /* CRITICAL: Save the character at end of name (before null-terminating)
         * This is usually '>' or whitespace before attributes */
        char saved_tag_char = *(p->pos);

        /* Null-terminate name IN PLACE */
        ((char*)name_start)[name_len] = '\0';

        /* Allocate element - check for out of memory */
        uint32_t elem_off = ALLOC_ELEM_V5(p);
        if (elem_off == UINT32_MAX) {
            /* Out of memory - restore character and skip this element */
            ((char*)name_start)[name_len] = saved_tag_char;
            continue;
        }

        struct compact_element_v2* elem = OFFSET_TO_TYPED(p->node_base, elem_off, struct compact_element_v2);

        /* Initialize - use UINT32_MAX for null offsets (0 is now valid) */
        elem->first_child = UINT32_MAX;
        elem->next_sibling = UINT32_MAX;
        elem->parent = ctx ? ctx->elem_offset : UINT32_MAX;
        elem->name_offset = (uint32_t)(name_start - p->string_base);

        /* Link to parent or set as root */
        if (ctx) {
            /* Use cached parent pointer - NO OFFSET_TO_TYPED needed! */
            struct compact_element_v2* parent = ctx->parent_ptr;
            if (ctx->last_child_off == 0) {
                /* Check if parent's first_child already has an attribute (high bit set) */
                uint32_t old_first = parent->first_child;
                if (old_first != UINT32_MAX) {
                    /* Read first field to check if it's an attribute */
                    uint32_t first_field = *(uint32_t*)(p->node_base + old_first);
                    if (first_field & 0x80000000) {
                        /* It's an attribute chain - link this element to end of attr chain */
                        struct compact_attribute_v2* last_attr = (struct compact_attribute_v2*)(p->node_base + old_first);
                        while (last_attr->next_attr != 0 && last_attr->next_attr != UINT32_MAX) {
                            uint32_t next_field = *(uint32_t*)(p->node_base + last_attr->next_attr);
                            if (!(next_field & 0x80000000)) {
                                /* Not an attribute - stop here */
                                break;
                            }
                            last_attr = (struct compact_attribute_v2*)(p->node_base + last_attr->next_attr);
                        }
                        /* Link element to end of attribute chain */
                        last_attr->next_attr = elem_off;
                    } else {
                        /* Not an attribute - replace first_child */
                        parent->first_child = elem_off;
                    }
                } else {
                    /* No existing first_child - just set it */
                    parent->first_child = elem_off;
                }
            } else {
                uint32_t* sibling_ptr = (uint32_t*)(p->node_base + ctx->last_child_off + 4);
                *sibling_ptr = elem_off;
            }
            ctx->last_child_off = elem_off;
        } else if (!got_root) {
            root_off = elem_off;
            got_root = 1;
        } else {
            /* STRICT MODE: Multiple root elements */
            if (p->strict_mode) {
                p->has_error = 1;
                /* Skip this element */
                while (p->pos < p->end && *p->pos != '>') p->pos++;
                if (p->pos < p->end && *p->pos == '>') p->pos++;
                continue;
            }
        }

        /* Parse attributes - restore char temporarily to check for '>' */
        ((char*)name_start)[name_len] = saved_tag_char;
        p->pos = skip_ws_v5(p->pos, p->end);
        uint32_t last_attr = 0;
        int self_closing = 0;  /* Will be set after attribute parsing */

        while (p->pos < p->end && *p->pos != '>' && *p->pos != '/') {
            if (xml_is_space(*p->pos)) {
                p->pos = skip_ws_v5(p->pos, p->end);
                continue;
            }

            if (xml_is_name_start(*p->pos)) {
                uint32_t attr_off = parse_attr_v5(p);
                if (attr_off == 0) {
                    /* Check if it was a parse error vs just no more attributes */
                    if (p->has_error) {
                        return UINT32_MAX;  /* Parse error */
                    }
                    break;
                }

                /* Link attribute */
                elem = OFFSET_TO_TYPED(p->node_base, elem_off, struct compact_element_v2);
                if (last_attr == 0) {
                    struct compact_attribute_v2* attr = OFFSET_TO_TYPED(p->node_base, attr_off, struct compact_attribute_v2);
                    attr->next_attr = elem->first_child;
                    elem->first_child = attr_off;
                } else {
                    struct compact_attribute_v2* la = OFFSET_TO_TYPED(p->node_base, last_attr, struct compact_attribute_v2);
                    la->next_attr = attr_off;
                }
                last_attr = attr_off;
                continue;
            }

            break;
        }

        /* Check for self-closing AFTER attribute parsing */
        if (p->pos < p->end && *p->pos == '/') {
            self_closing = 1;
            p->pos++;
        }

        /* Check for closing '>' */
        if (p->pos < p->end && *p->pos == '>') {
            p->pos++;
        }

        /* Re-null-terminate the element name for later access
         * (we restored it earlier to parse attributes) */
        ((char*)name_start)[name_len] = '\0';

        /* STRICT MODE: Check for undeclared namespace prefix */
        if (p->strict_mode) {
            /* Check if element name has a prefix (contains ':') */
            const char* colon = (const char*)memchr(name_start, ':', name_len);
            if (colon) {
                size_t prefix_len = colon - name_start;
                int prefix_declared = 0;

                /* Check if prefix is 'xml' (pre-declared) */
                if (prefix_len == 3 && strncmp(name_start, "xml", 3) == 0) {
                    prefix_declared = 1;
                }

                /* Check if prefix is declared on this element */
                if (!prefix_declared && elem->first_child != UINT32_MAX) {
                    uint32_t attr_off = elem->first_child;
                    while (attr_off != UINT32_MAX) {
                        uint32_t first_field = *(uint32_t*)(p->node_base + attr_off);
                        if (first_field & 0x80000000) {
                            struct compact_attribute_v2* attr = (struct compact_attribute_v2*)(p->node_base + attr_off);
                            const char* attr_name = p->string_base + (first_field & 0x7FFFFFFF);

                            /* Check if this is xmlns:prefix */
                            if (strncmp(attr_name, "xmlns:", 6) == 0) {
                                size_t attr_prefix_len = strlen(attr_name) - 6;
                                if (attr_prefix_len == prefix_len &&
                                    strncmp(attr_name + 6, name_start, prefix_len) == 0) {
                                    prefix_declared = 1;
                                    break;
                                }
                            }
                            attr_off = attr->next_attr;
                            /* Check if next is not an attribute */
                            if (attr_off != UINT32_MAX) {
                                uint32_t next_field = *(uint32_t*)(p->node_base + attr_off);
                                if (!(next_field & 0x80000000)) {
                                    break;
                                }
                            }
                        } else {
                            break;
                        }
                    }
                }

                if (!prefix_declared) {
                    p->has_error = 1;
                }
            }
        }

        if (!self_closing) {
            STACK_PUSH_V5(p, elem_off, name_len, elem);
        }
    }

    /* Check for unclosed tags - stack should be empty after parsing */
    if (p->stack_size > 0) {
        p->has_error = 1;
        return UINT32_MAX;
    }

    /* Return root offset if found, or UINT32_MAX (invalid offset) if not */
    return got_root ? root_off : UINT32_MAX;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Parse XML into 16-byte compact elements with v5 optimizations
 *
 * This is the MAXIMUM PERFORMANCE parser with:
 * - Zero-check allocator (no size checks)
 * - Direct offset returns
 * - In-place null-termination without branches
 *
 * @param xml MUTABLE XML string (will be modified in-place)
 * @param len Length of XML string
 * @param error_out Error output (0=success, 1=error)
 * @param strict_mode 1 for strict XML validation, 0 for lenient
 * @return taurus_document with 16-byte elements, or NULL on error
 */
struct taurus_document* taurus_parse_v5(char* xml, size_t len, int* error_out, int strict_mode) {
    if (!xml || len == 0) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Pre-allocate 2x input size to guarantee NO growth needed */
    size_t alloc_size = ZERO_CHECK_SIZE_ESTIMATE(len);
    if (alloc_size < ZERO_CHECK_MIN_SIZE) alloc_size = ZERO_CHECK_MIN_SIZE;

    ZeroCheckAlloc* alloc = zero_check_alloc_create(alloc_size);
    if (!alloc) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Initialize parser */
    ParserV5 parser;
    parser.pos = xml;
    parser.end = xml + len;
    parser.string_base = xml;
    parser.node_base = alloc->base;
    parser.alloc = alloc;
    parser.stack_size = 0;
    parser.has_error = 0;  /* Initialize error flag */
    parser.strict_mode = strict_mode;
    parser.got_root = 0;

    /* Skip prolog */
    parser.pos = skip_ws_v5(parser.pos, parser.end);

    /* Skip XML declaration */
    if (parser.pos < parser.end && *parser.pos == '<' &&
        parser.pos + 1 < parser.end && parser.pos[1] == '?') {
        /* Check for valid XML declaration vs invalid PI with 'xml' target */
        const char* decl_start = parser.pos;
        parser.pos += 2;  /* Skip <? */

        /* Check if this is an XML declaration (<?xml) or a PI */
        if (parser.pos + 3 < parser.end &&
            (parser.pos[0] == 'x' || parser.pos[0] == 'X') &&
            (parser.pos[1] == 'm' || parser.pos[1] == 'M') &&
            (parser.pos[2] == 'l' || parser.pos[2] == 'L')) {
            /* This is <?xml - check if it's a valid XML declaration */
            char next_char = parser.pos[3];
            if (next_char == ' ' || next_char == '\t' || next_char == '\n' || next_char == '\r') {
                /* Looks like XML declaration - validate version attribute in strict mode */
                if (strict_mode) {
                    /* Skip whitespace */
                    const char* p = parser.pos + 4;
                    while (p < parser.end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;

                    /* Check for version attribute */
                    if (p + 7 < parser.end &&
                        (p[0] == 'v' || p[0] == 'V') &&
                        (p[1] == 'e' || p[1] == 'E') &&
                        (p[2] == 'r' || p[2] == 'R') &&
                        (p[3] == 's' || p[3] == 'S') &&
                        (p[4] == 'i' || p[4] == 'I') &&
                        (p[5] == 'o' || p[5] == 'O') &&
                        (p[6] == 'n' || p[6] == 'N')) {
                        /* Found version - check for = */
                        p += 7;
                        while (p < parser.end && (*p == ' ' || *p == '\t')) p++;
                        if (p < parser.end && *p == '=') {
                            p++;
                            while (p < parser.end && (*p == ' ' || *p == '\t')) p++;
                            /* Check for quote */
                            if (p < parser.end && (*p == '"' || *p == '\'')) {
                                /* Valid declaration format */
                            } else {
                                /* Missing quote after = */
                                parser.has_error = 1;
                            }
                        } else {
                            /* Missing = after version */
                            parser.has_error = 1;
                        }
                    } else {
                        /* Missing or invalid version attribute - treat as invalid PI */
                        parser.has_error = 1;
                    }
                }
            } else {
                /* <?xml without whitespace is an invalid PI */
                if (strict_mode) {
                    parser.has_error = 1;
                }
            }
            /* Skip to ?> */
            while (parser.pos < parser.end) {
                if (*parser.pos == '?' && parser.pos + 1 < parser.end && parser.pos[1] == '>') {
                    parser.pos += 2;
                    break;
                }
                parser.pos++;
            }
        } else {
            /* Not <?xml - skip to > (treating as PI) */
            parser.pos = decl_start + 2;
            while (parser.pos < parser.end && *parser.pos != '>') parser.pos++;
            if (parser.pos < parser.end && *parser.pos == '>') parser.pos++;
        }
        parser.pos = skip_ws_v5(parser.pos, parser.end);
    }

    /* Parse */
    uint32_t raw_root = parse_v5_main(&parser);

    /* Check for parse errors */
    if (raw_root == UINT32_MAX || parser.has_error) {
        zero_check_alloc_destroy(alloc);
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Store root offset directly - UINT32_MAX means no root (already checked above)
     * NOTE: Offset 0 is VALID (first element in buffer) */
    uint32_t root_off = raw_root;

    /* Final null-termination of the buffer */
    xml[len] = '\0';

    /* Create document */
    struct taurus_document* doc = (struct taurus_document*)malloc(sizeof(struct taurus_document));
    if (!doc) {
        zero_check_alloc_destroy(alloc);
        if (error_out) *error_out = 1;
        return NULL;
    }

    memset(doc, 0, sizeof(struct taurus_document));
    doc->compact_alloc = alloc;  /* Using ZeroCheckAlloc - stored in compact_alloc */
    doc->compact_base = alloc->base;
    doc->compact_root_offset = root_off;  /* Raw offset, UINT32_MAX means no root */
    /* COMPACT-ONLY: No is_compact/compact_v2 flags needed */
    doc->xml_buffer = xml;
    doc->xml_buffer_len = len;
    doc->xml_buffer_needs_free = 0;
    doc->pool = NULL;

    if (error_out) *error_out = 0;
    return doc;
}
