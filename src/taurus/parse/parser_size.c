/* parser_size.c - Document Size Estimation for Compact Allocation
 * Copyright (c) 2024, Ribose Inc.
 *
 * First pass of two-pass parsing: scan the document and count everything.
 * This enables single-allocation parsing with exact size requirements.
 *
 * Performance: O(n) scan of input, no allocations
 */

#include "parser_size.h"
#include "../taurus_internal.h"
#include "../simd_helpers.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Size Estimation Implementation
 * ============================================================================ */

/**
 * Estimate document size by scanning (Pass 1 of two-pass parsing)
 *
 * This is a fast scan that:
 * 1. Counts all node types (elements, attributes, text, etc.)
 * 2. Measures all string bytes
 * 3. Does NOT allocate any memory
 * 4. Does NOT validate XML (that's Pass 2's job)
 *
 * @param xml XML input
 * @param len Length of XML input
 * @return DocumentSizeInfo with all counts
 */
DocumentSizeInfo parser_estimate_size(const char* xml, size_t len) {
    DocumentSizeInfo info;
    memset(&info, 0, sizeof(info));

    if (!xml || len == 0) return info;

    const char* p = xml;
    const char* end = xml + len;

    /* Track parsing state */
    int in_element = 0;       /* Inside an element tag */
    int in_attribute = 0;     /* Parsing attribute value */
    char quote_char = 0;      /* Current quote character for attr value */
    int depth = 0;            /* Nesting depth */

    while (p < end) {
        if (*p == '<') {
            /* Start of a tag */
            if (p + 1 < end) {
                char next = *(p + 1);

                if (next == '/') {
                    /* Closing tag - just count it in string bytes */
                    info.total_string_bytes += 2;  /* </ */
                    p += 2;
                    /* Skip to > */
                    while (p < end && *p != '>') {
                        info.total_string_bytes++;
                        p++;
                    }
                    if (p < end && *p == '>') {
                        info.total_string_bytes++;
                        p++;
                    }
                    depth--;
                }
                else if (next == '!') {
                    /* Comment, CDATA, or DOCTYPE */
                    if (p + 3 < end && strncmp(p, "<!--", 4) == 0) {
                        /* Comment */
                        info.comment_count++;
                        p += 4;
                        while (p + 2 < end && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) {
                            info.text_bytes++;
                            p++;
                        }
                        if (p + 2 < end) p += 3;
                    }
                    else if (p + 8 < end && strncmp(p, "<![CDATA[", 9) == 0) {
                        /* CDATA */
                        info.cdata_count++;
                        p += 9;
                        while (p + 2 < end && !(p[0] == ']' && p[1] == ']' && p[2] == '>')) {
                            info.text_bytes++;
                            p++;
                        }
                        if (p + 2 < end) p += 3;
                    }
                    else if (p + 9 < end && strncmp(p, "<!DOCTYPE", 9) == 0) {
                        /* DOCTYPE */
                        info.doctype_count++;
                        p += 9;
                        int doctype_depth = 1;
                        while (p < end && doctype_depth > 0) {
                            if (*p == '[') doctype_depth++;
                            else if (*p == ']') doctype_depth--;
                            else if (*p == '>') { doctype_depth = 0; }
                            info.text_bytes++;
                            p++;
                        }
                    }
                    else {
                        p++;
                    }
                }
                else if (next == '?') {
                    /* Processing instruction */
                    info.pi_count++;
                    p += 2;
                    /* Skip PI target and data */
                    while (p < end && !(*p == '?' && *(p+1) == '>')) {
                        info.text_bytes++;
                        p++;
                    }
                    if (p + 1 < end) p += 2;
                }
                else if (next == '[') {
                    /* Skip CDATA end marker false positive */
                    p++;
                }
                else {
                    /* Opening element tag */
                    info.element_count++;
                    in_element = 1;
                    p++;

                    /* Parse element name */
                    const char* name_start = p;
                    while (p < end && *p != '>' && *p != '/' && *p != ' ' &&
                           *p != '\t' && *p != '\n' && *p != '\r') {
                        p++;
                    }
                    info.element_name_bytes += (p - name_start) + 1;  /* +1 for null */
                    depth++;
                }
            }
            else {
                p++;
            }
        }
        else if (in_element) {
            /* Inside an element tag, looking for attributes */
            if (*p == '>') {
                in_element = 0;
                in_attribute = 0;
                p++;
            }
            else if (*p == '/' && p + 1 < end && *(p + 1) == '>') {
                /* Self-closing tag */
                in_element = 0;
                p += 2;
                depth--;
            }
            else if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
                /* Whitespace - skip */
                p++;
            }
            else if (!in_attribute) {
                /* Start of attribute name */
                const char* attr_name_start = p;
                while (p < end && *p != '=' && *p != '>' && *p != '/' &&
                       *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                    p++;
                }
                if (p < end && *p == '=') {
                    info.attribute_count++;
                    info.attr_name_bytes += (p - attr_name_start) + 1;

                    p++;  /* Skip = */

                    /* Skip whitespace */
                    while (p < end && (*p == ' ' || *p == '\t')) p++;

                    /* Get quote character */
                    if (p < end && (*p == '"' || *p == '\'')) {
                        quote_char = *p;
                        p++;
                        in_attribute = 1;

                        /* Parse attribute value */
                        const char* value_start = p;
                        while (p < end && *p != quote_char) {
                            p++;
                        }
                        info.attr_value_bytes += (p - value_start) + 1;

                        if (p < end) {
                            p++;  /* Skip closing quote */
                            in_attribute = 0;
                        }
                    }
                }
                else {
                    p++;
                }
            }
            else {
                p++;
            }
        }
        else {
            /* Text content outside of tags */
            if (*p == '&') {
                /* Entity reference - count as text */
                info.text_bytes++;
                p++;
            }
            else {
                info.text_bytes++;
                info.text_count++;
                p++;
            }
        }
    }

    /* Adjust text_count (we overcounted - each char was counted as a node) */
    /* In reality, we have fewer text nodes with longer content */
    if (info.text_bytes > 0) {
        /* Estimate: average text node is ~50 bytes */
        info.text_count = (info.text_bytes + 49) / 50;
        if (info.text_count < 1) info.text_count = 1;
    }

    /* Estimate namespace overhead (xmlns declarations) */
    /* Roughly 10% of elements have namespace declarations */
    info.namespace_bytes = info.element_count / 10 * 30;  /* 30 bytes avg per ns decl */

    return info;
}

/**
 * Calculate total memory needed for compact document
 *
 * @param info Size information from parser_estimate_size
 * @return Total bytes to allocate
 */
size_t calculate_compact_size(const DocumentSizeInfo* info) {
    if (!info) return 0;

    size_t total = 0;

    /* Element structures */
    total += info->element_count * 32;  /* 28 bytes + alignment */

    /* Attribute structures */
    total += info->attribute_count * 24;  /* 20 bytes + alignment */

    /* Text nodes */
    total += (info->text_count + info->cdata_count + info->comment_count + info->pi_count)
             * 20;  /* 16 bytes + alignment */

    /* String storage (already includes null terminators) */
    total += info->element_name_bytes;
    total += info->attr_name_bytes;
    total += info->attr_value_bytes;
    total += info->text_bytes;
    total += info->namespace_bytes;

    /* String interning hash table */
    size_t string_bytes = info->element_name_bytes + info->attr_name_bytes +
                          info->attr_value_bytes + info->text_bytes;
    size_t hash_buckets = 64;
    while (hash_buckets < string_bytes / 8 && hash_buckets < 65536) {
        hash_buckets *= 2;
    }
    total += hash_buckets * sizeof(void*);  /* Hash bucket array */
    total += (info->element_count + info->attribute_count) * 16;  /* Hash entries */

    /* Alignment padding */
    total += 128;

    /* Document structure overhead */
    total += 256;

    return total;
}

/* ============================================================================
 * Validation Helpers
 * ============================================================================ */

/**
 * Validate size estimates are reasonable
 *
 * @param info Size information to validate
 * @return 1 if valid, 0 if invalid
 */
int validate_size_estimates(const DocumentSizeInfo* info) {
    if (!info) return 0;

    /* Must have at least one element */
    if (info->element_count == 0) return 0;

    /* Reasonable ratios */
    if (info->attribute_count > info->element_count * 1000) return 0;
    if (info->text_count > info->element_count * 10000) return 0;

    /* Non-negative sizes */
    if (info->element_name_bytes == 0) return 0;

    return 1;
}
