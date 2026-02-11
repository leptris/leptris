/* taurus_parse.h - XML parser for libtaurus (pure C)
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * Converted from ext/taurus/parse.c (Ruby C extension → pure C library)
 */

#ifndef TAURUS_PARSE_H
#define TAURUS_PARSE_H

#include "taurus_internal.h"
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
typedef struct taurus_parse_options {
    int strict;              /* Strict XML validation (default: 1) */
    int preserve_whitespace; /* Preserve whitespace-only text nodes (default: 0) */
    int track_positions;     /* Track line/column positions (default: 0) */
} TaurusParseOptions;

/* Initialize parse options with defaults */
static inline void taurus_parse_options_init(TaurusParseOptions *opts) {
    opts->strict = 1;
    opts->preserve_whitespace = 0;
    opts->track_positions = 0;
}

/* ==================================================================
 * PARSE CONTEXT
 * ================================================================== */

/* Parse context - internal state during parsing
 * NOTE: This structure is opaque to users. Use accessor functions. */
typedef struct taurus_parse_context {
    /* Input buffer */
    const char *start;              /* Start of input (for error reporting) */
    const char *pos;                /* Current position */
    const char *end;                /* End of input */
    
    /* Parse state */
    struct taurus_document *doc;    /* Document being built */
    struct taurus_element *current; /* Current element (for nesting) */
    TaurusAttrStack attr_stack;     /* Reusable attribute stack */
    StringInternTable intern_table; /* String interning for attributes */
    
    /* Options */
    TaurusParseOptions opts;
    
    /* Error tracking */
    int line;                       /* Current line number (1-based) */
    int column;                     /* Current column number (1-based) */
    char error[256];                /* Error message (empty if no error) */
} TaurusParseContext;

/* Initialize parse context with input buffer and options
 * Returns 0 on success, -1 on error */
int taurus_parse_context_init(TaurusParseContext *ctx,
                               const char *xml,
                               size_t len,
                               TaurusParseOptions *opts);

/* Free parse context resources (attributes, interning table, etc.)
 * Does NOT free the document (caller owns that) */
void taurus_parse_context_free(TaurusParseContext *ctx);

/* Get error message from parse context (empty string if no error) */
static inline const char *taurus_parse_context_error(const TaurusParseContext *ctx) {
    return ctx->error;
}

/* Set error message in parse context */
static inline void taurus_parse_context_set_error(TaurusParseContext *ctx,
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
 *   Caller owns returned document and must free with taurus_document_free_tree()
 * 
 * Thread safety: Each parse operation is independent (no shared state)
 * 
 * Example:
 *   const char *xml = "<root><child>text</child></root>";
 *   struct taurus_document *doc = taurus_parse(xml, strlen(xml), NULL);
 *   if (!doc) {
 *       fprintf(stderr, "Parse error\n");
 *       return;
 *   }
 *   // Use document...
 *   taurus_document_free_tree(doc);
 */
struct taurus_document *taurus_parse(const char *xml,
                                      size_t len,
                                      TaurusParseOptions *opts);

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
 *   struct taurus_document *doc = taurus_parse_with_error(
 *       xml, len, NULL, error, sizeof(error)
 *   );
 *   if (!doc) {
 *       fprintf(stderr, "Parse error: %s\n", error);
 *       return;
 *   }
 */
struct taurus_document *taurus_parse_with_error(const char *xml,
                                                  size_t len,
                                                  TaurusParseOptions *opts,
                                                  char *error_buf,
                                                  size_t error_len);

/* ==================================================================
 * HELPER FUNCTIONS (exposed for testing)
 * ================================================================== */

/* Parse XML name - exposed for testing */
const char *parse_name(TaurusParseContext *ctx, size_t *len);

/* Parse quoted attribute value - exposed for testing */
const char *parse_quoted_value(TaurusParseContext *ctx, size_t *len);

/* Skip XML comment - exposed for testing */
int skip_comment(TaurusParseContext *ctx);

/* Parse CDATA section - exposed for testing */
const char *parse_cdata(TaurusParseContext *ctx, size_t *len);

/* Parse text content - exposed for testing */
const char *parse_text(TaurusParseContext *ctx, size_t *len);

/* ==================================================================
 * ELEMENT PARSING FUNCTIONS (exposed for testing - Session 83)
 * ================================================================== */

/* Parse element start tag <name attr="value" ...>
 * Returns newly created element or NULL on error
 * NOTE: Return value may have lowest bit set to indicate self-closing */
struct taurus_element *parse_start_tag(TaurusParseContext *ctx,
                                        struct taurus_element *parent);

/* Parse element end tag </name>
 * Returns 0 on success, -1 on error */
int parse_end_tag(TaurusParseContext *ctx, const char *expected_name);

/* Parse complete element (start tag, content, end tag)
 * Returns newly created element or NULL on error */
struct taurus_element *parse_element(TaurusParseContext *ctx,
                                      struct taurus_element *parent);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_PARSE_H */