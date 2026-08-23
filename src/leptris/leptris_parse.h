/* leptris_parse.h - XML parser for libleptris (pure C)
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * Converted from ext/leptris/parse.c (Ruby C extension → pure C library)
 */

#ifndef LEPTRIS_PARSE_H
#define LEPTRIS_PARSE_H

#include "leptris_internal.h"
#include "parse_helpers.h"
#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================================================
 * PARSE OPTIONS
 * ================================================================== */

/* Parse options structure */
typedef struct leptris_parse_options {
    int strict;              /* Strict XML validation (default: 1) */
    int preserve_whitespace; /* Preserve whitespace-only text nodes (default: 0) */
    int track_positions;     /* Track line/column positions (default: 0) */
} LeptrisParseOptions;

/* Initialize parse options with defaults */
static inline void leptris_parse_options_init(LeptrisParseOptions *opts) {
    opts->strict = 1;
    opts->preserve_whitespace = 0;
    opts->track_positions = 0;
}

/* ==================================================================
 * PARSE CONTEXT
 * ================================================================== */

/* Parse context - internal state during parsing
 * NOTE: This structure is opaque to users. Use accessor functions. */
typedef struct leptris_parse_context {
    /* Input buffer */
    const char *start;              /* Start of input (for error reporting) */
    const char *pos;                /* Current position */
    const char *end;                /* End of input */

    /* Parse state */
    struct leptris_document *doc;    /* Document being built */
    struct leptris_element *current; /* Current element (for nesting) */
    LeptrisAttrStack attr_stack;     /* Reusable attribute stack */
    StringInternTable intern_table; /* String interning for attributes */

    /* Options */
    LeptrisParseOptions opts;

    /* Error tracking */
    int line;                       /* Current line number (1-based) */
    int column;                     /* Current column number (1-based) */
    char error[256];                /* Error message (empty if no error) */
} LeptrisParseContext;

/* Initialize parse context with input buffer and options
 * Returns 0 on success, -1 on error */
int leptris_parse_context_init(LeptrisParseContext *ctx,
                               const char *xml,
                               size_t len,
                               LeptrisParseOptions *opts);

/* Free parse context resources (attributes, interning table, etc.)
 * Does NOT free the document (caller owns that) */
void leptris_parse_context_free(LeptrisParseContext *ctx);

/* Get error message from parse context (empty string if no error) */
static inline const char *leptris_parse_context_error(const LeptrisParseContext *ctx) {
    return ctx->error;
}

/* Set error message in parse context */
static inline void leptris_parse_context_set_error(LeptrisParseContext *ctx,
                                                   const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(ctx->error, sizeof(ctx->error), fmt, args);
    va_end(args);
}

/* ==================================================================
 * MAIN PARSE API
 * ================================================================== */

/* Parse XML string into document structure
 *
 * Parameters:
 *   xml: XML string to parse (need not be null-terminated)
 *   len: Length of XML string
 *   opts: Parse options (NULL for defaults)
 *
 * Returns:
 *   Document structure on success, NULL on error
 *   Caller owns returned document and must free with leptris_document_free_tree()
 *
 * Thread safety: Each parse operation is independent (no shared state)
 *
 * Example:
 *   const char *xml = "<root><child>text</child></root>";
 *   struct leptris_document *doc = leptris_parse(xml, strlen(xml), NULL);
 *   if (!doc) {
 *       fprintf(stderr, "Parse error\n");
 *       return;
 *   }
 *   // Use document...
 *   leptris_document_free_tree(doc);
 */
/* Internal: callers use the public leptris_parse_string family. No
 * LEPTRIS_API - the export-surface gate keeps it out of the shared
 * library (TODO.concurrency/02). */
struct leptris_document *leptris_parse(const char *xml,
                                      size_t len,
                                      LeptrisParseOptions *opts);

/* Parse XML with error reporting
 *
 * Parameters:
 *   xml: XML string to parse
 *   len: Length of XML string
 *   opts: Parse options (NULL for defaults)
 *   error_buf: Buffer for error message (can be NULL)
 *   error_len: Size of error buffer
 *
 * Returns:
 *   Document structure on success, NULL on error
 *   If error_buf is provided, it will contain error message on failure
 *
 * Example:
 *   char error[256];
 *   struct leptris_document *doc = leptris_parse_with_error(
 *       xml, len, NULL, error, sizeof(error)
 *   );
 *   if (!doc) {
 *       fprintf(stderr, "Parse error: %s\n", error);
 *       return;
 *   }
 */
struct leptris_document *leptris_parse_with_error(const char *xml,
                                                  size_t len,
                                                  LeptrisParseOptions *opts,
                                                  char *error_buf,
                                                  size_t error_len);

/* ==================================================================
 * HELPER FUNCTIONS (exposed for testing)
 * ================================================================== */

/* Parse XML name - exposed for testing */
const char *parse_name(LeptrisParseContext *ctx, size_t *len);

/* Parse quoted attribute value - exposed for testing */
const char *parse_quoted_value(LeptrisParseContext *ctx, size_t *len);

/* Skip XML comment - exposed for testing */
int skip_comment(LeptrisParseContext *ctx);

/* Parse CDATA section - exposed for testing */
const char *parse_cdata(LeptrisParseContext *ctx, size_t *len);

/* Parse text content - exposed for testing */
const char *parse_text(LeptrisParseContext *ctx, size_t *len);

/* ==================================================================
 * ELEMENT PARSING FUNCTIONS (exposed for testing - Session 83)
 * ================================================================== */

/* Parse element start tag <name attr="value" ...>
 * Returns newly created element or NULL on error
 * NOTE: Return value may have lowest bit set to indicate self-closing */
struct leptris_element *parse_start_tag(LeptrisParseContext *ctx,
                                        struct leptris_element *parent);

/* Parse element end tag </name>
 * Returns 0 on success, -1 on error */
int parse_end_tag(LeptrisParseContext *ctx, const char *expected_name);

/* Parse complete element (start tag, content, end tag)
 * Returns newly created element or NULL on error */
struct leptris_element *parse_element(LeptrisParseContext *ctx,
                                      struct leptris_element *parent);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_PARSE_H */