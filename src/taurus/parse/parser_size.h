/* parser_size.h - Document Size Estimation for Compact Allocation
 * Copyright (c) 2024, Ribose Inc.
 *
 * First pass of two-pass parsing: scan the document and count everything.
 * This enables single-allocation parsing with exact size requirements.
 */

#ifndef TAURUS_PARSER_SIZE_H
#define TAURUS_PARSER_SIZE_H

#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * Document Size Information
 * ============================================================================ */

/**
 * Document size information for compact allocation
 *
 * Collected during first pass of two-pass parsing.
 * Used to calculate exact memory requirements before allocation.
 */
typedef struct {
    /* Node counts */
    size_t element_count;      /* Number of elements */
    size_t attribute_count;    /* Number of attributes */
    size_t text_count;         /* Number of text nodes */
    size_t cdata_count;        /* Number of CDATA sections */
    size_t comment_count;      /* Number of comments */
    size_t pi_count;           /* Number of processing instructions */
    size_t doctype_count;      /* Number of DOCTYPE declarations */

    /* String byte counts (including null terminators) */
    size_t element_name_bytes; /* Total bytes for element names */
    size_t attr_name_bytes;    /* Total bytes for attribute names */
    size_t attr_value_bytes;   /* Total bytes for attribute values */
    size_t text_bytes;         /* Total bytes for text content */
    size_t namespace_bytes;    /* Estimated bytes for namespace URIs */
    size_t total_string_bytes; /* Sum of all string bytes */
} DocumentSizeInfo;

/* ============================================================================
 * Size Estimation Functions
 * ============================================================================ */

/**
 * Estimate document size by scanning (Pass 1 of two-pass parsing)
 *
 * This is a fast O(n) scan that:
 * 1. Counts all node types (elements, attributes, text, etc.)
 * 2. Measures all string bytes
 * 3. Does NOT allocate any memory
 * 4. Does NOT validate XML (that's Pass 2's job)
 *
 * @param xml XML input
 * @param len Length of XML input
 * @return DocumentSizeInfo with all counts
 */
DocumentSizeInfo parser_estimate_size(const char* xml, size_t len);

/**
 * Calculate total memory needed for compact document
 *
 * @param info Size information from parser_estimate_size
 * @return Total bytes to allocate
 */
size_t calculate_compact_size(const DocumentSizeInfo* info);

/**
 * Validate size estimates are reasonable
 *
 * @param info Size information to validate
 * @return 1 if valid, 0 if invalid
 */
int validate_size_estimates(const DocumentSizeInfo* info);

#endif /* TAURUS_PARSER_SIZE_H */
