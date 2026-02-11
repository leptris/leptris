/* taurus_parse.c - XML parser core helper functions
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * Converted from ext/taurus/parse.c (Ruby C extension → pure C library)
 * Session 82: Helper functions (parse_name, parse_quoted_value, etc.)
 * Session 84: Modularized - moved element/content/document functions to separate files
 */

#include "parse_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* ==================================================================
 * STRING HELPERS
 * ================================================================== */

/* Duplicate string from buffer with length (NOT null-terminated in source) */
char *taurus_strndup(const char *str, size_t len) {
    char *dup;
    
    if (!str) return NULL;
    
    dup = (char*)taurus_malloc(len + 1);
    if (!dup) return NULL;
    
    memcpy(dup, str, len);
    dup[len] = '\0';
    
    return dup;
}

/* Extract prefix and local name from qualified name "prefix:local"
 * Returns 0 if no prefix, 1 if prefix found
 * Modifies qname buffer in place, caller must free qname */
int extract_prefix_and_local(char *qname, char **prefix, char **local_name) {
    char *colon;
    
    if (!qname || !prefix || !local_name) return -1;
    
    /* Find colon separator */
    colon = strchr(qname, ':');
    
    if (!colon) {
        /* No prefix */
        *prefix = NULL;
        *local_name = qname;
        return 0;
    }
    
    /* Split at colon */
    *colon = '\0';
    *prefix = qname;
    *local_name = colon + 1;
    
    return 1;
}

/* Extract just local name from qualified name (for end tag comparison) */
const char *extract_local_name_only(const char *qname) {
    const char *colon;
    
    if (!qname) return NULL;
    
    colon = strchr(qname, ':');
    if (!colon) {
        return qname;  /* No prefix, return whole name */
    }
    
    return colon + 1;  /* Return part after colon */
}


/* ==================================================================
 * PARSE CONTEXT MANAGEMENT
 * ================================================================== */

/* Initialize parse context with input buffer and options */
int taurus_parse_context_init(TaurusParseContext *ctx,
                               const char *xml,
                               size_t len,
                               TaurusParseOptions *opts) {
    if (!ctx || !xml) {
        return -1;
    }
    
    /* Initialize input buffer pointers */
    ctx->start = xml;
    ctx->pos = xml;
    ctx->end = xml + len;
    
    /* Initialize parse state */
    ctx->doc = NULL;
    ctx->current = NULL;
    taurus_attr_stack_init(&ctx->attr_stack);
    string_intern_table_init(&ctx->intern_table);
    
    /* Set options (use defaults if NULL) */
    if (opts) {
        ctx->opts = *opts;
    } else {
        taurus_parse_options_init(&ctx->opts);
    }
    
    /* Initialize error tracking */
    ctx->line = 1;
    ctx->column = 1;
    ctx->error[0] = '\0';
    
    return 0;
}

/* Free parse context resources */
void taurus_parse_context_free(TaurusParseContext *ctx) {
    if (!ctx) {
        return;
    }
    
    /* Cleanup attribute stack (frees heap memory if allocated) */
    taurus_attr_stack_cleanup(&ctx->attr_stack);
    
    /* Free string interning table */
    string_intern_table_free(&ctx->intern_table);
    
    /* Clear pointers (safety) */
    ctx->start = NULL;
    ctx->pos = NULL;
    ctx->end = NULL;
    ctx->doc = NULL;
    ctx->current = NULL;
}

/* ==================================================================
 * CORE PARSING FUNCTIONS
 * ================================================================== */

/* Parse an XML name (element or attribute name)
 *
 * Returns pointer to name in buffer and sets *len to name length.
 * Advances ctx->pos past the name.
 * Returns NULL on error (sets ctx->error).
 *
 * XML name rules:
 * - Must start with letter, underscore, or colon
 * - Can contain letters, digits, hyphens, periods, colons, underscores
 *
 * NOTE: Returned pointer is into parse buffer, NOT null-terminated.
 *       Caller must copy with taurus_strndup() if needed beyond parse.
 */
const char *parse_name(TaurusParseContext *ctx, size_t *len) {
    const char *start;
    
    /* Skip leading whitespace */
    taurus_skip_whitespace(&ctx->pos, ctx->end);
    
    /* Set start pointer */
    start = ctx->pos;
    
    /* Must start with letter or underscore (colon for namespaces) */
    if (ctx->pos >= ctx->end ||
        (!is_name_start_fast(*ctx->pos) && *ctx->pos != ':')) {
        taurus_parse_context_set_error(ctx,
            "Invalid name start character at line %d, column %d",
            ctx->line, ctx->column);
        return NULL;
    }
    ctx->pos++;
    
    /* Parse name characters using fast table lookup */
    while (ctx->pos < ctx->end && taurus_is_name_char(*ctx->pos)) {
        ctx->pos++;
    }
    
    /* Calculate length */
    *len = ctx->pos - start;
    
    /* Name cannot be empty */
    if (*len == 0) {
        taurus_parse_context_set_error(ctx,
            "Empty name at line %d, column %d",
            ctx->line, ctx->column);
        return NULL;
    }
    
    return start;
}

/* Parse a quoted attribute value
 *
 * Returns pointer to value in buffer (excluding quotes) and sets *len.
 * Advances ctx->pos past the closing quote.
 * Returns NULL on error (sets ctx->error).
 *
 * Handles both single and double quotes.
 * Uses SIMD optimization to find closing quote quickly.
 *
 * NOTE: Returned pointer is into parse buffer, NOT null-terminated.
 *       Caller must copy with taurus_strndup() if needed.
 */
const char *parse_quoted_value(TaurusParseContext *ctx, size_t *len) {
    char quote;
    const char *value_start;
    const char *quote_pos;
    
    /* Skip leading whitespace */
    taurus_skip_whitespace(&ctx->pos, ctx->end);
    
    /* Must start with quote */
    if (ctx->pos >= ctx->end ||
        (*ctx->pos != '"' && *ctx->pos != '\'')) {
        taurus_parse_context_set_error(ctx,
            "Expected quote at line %d, column %d",
            ctx->line, ctx->column);
        return NULL;
    }
    
    /* Save quote character and skip it */
    quote = *ctx->pos++;
    value_start = ctx->pos;
    
    /* Find closing quote using SIMD optimization */
    quote_pos = simd_find_char(ctx->pos, ctx->end, quote);
    if (!quote_pos) {
        taurus_parse_context_set_error(ctx,
            "Unterminated quoted value at line %d, column %d",
            ctx->line, ctx->column);
        return NULL;
    }
    
    /* Calculate value length (excluding quotes) */
    *len = quote_pos - value_start;
    
    /* Advance position past closing quote */
    ctx->pos = quote_pos + 1;
    
    return value_start;
}

/* Skip XML comment <!-- ... -->
 *
 * Returns 0 on success, -1 on error (sets ctx->error).
 * Advances ctx->pos past the comment end marker "-->".
 *
 * Assumes ctx->pos is at '<!--' (caller must check and advance past it).
 * Uses SIMD optimization to find potential end markers quickly.
 */
int skip_comment(TaurusParseContext *ctx) {
    const char *dash_pos;
    
    /* Should be called after seeing "<!--" */
    /* Scan for "-->" pattern */
    while (ctx->pos + 2 < ctx->end) {
        /* Use SIMD to find next '-' quickly */
        dash_pos = simd_find_char(ctx->pos, ctx->end - 2, '-');
        if (!dash_pos) {
            /* No more dashes - unterminated comment */
            break;
        }
        
        /* Move to dash position */
        ctx->pos = dash_pos;
        
        /* Check if it's "-->" */
        if (ctx->pos[0] == '-' && ctx->pos[1] == '-' && ctx->pos[2] == '>') {
            ctx->pos += 3;  /* Skip "-->" */
            return 0;
        }
        
        /* Not a match, move past this dash and continue */
        ctx->pos++;
    }
    
    /* Unterminated comment */
    taurus_parse_context_set_error(ctx,
        "Unterminated comment at line %d, column %d",
        ctx->line, ctx->column);
    return -1;
}

/* Parse CDATA section <![CDATA[ ... ]]>
 *
 * Returns pointer to CDATA content (excluding markers) and sets *len.
 * Advances ctx->pos past the "]]>" end marker.
 * Returns NULL on error (sets ctx->error).
 *
 * Assumes ctx->pos is at "<![CDATA[" (caller must check and advance past it).
 * Uses SIMD optimization to find potential end markers quickly.
 *
 * NOTE: Returned pointer is into parse buffer, NOT null-terminated.
 */
const char *parse_cdata(TaurusParseContext *ctx, size_t *len) {
    const char *content_start;
    const char *bracket_pos;
    
    /* Should be called after seeing "<![CDATA[" */
    content_start = ctx->pos;
    
    /* Scan for "]]>" pattern */
    while (ctx->pos + 2 < ctx->end) {
        /* Use SIMD to find next ']' quickly */
        bracket_pos = simd_find_char(ctx->pos, ctx->end - 2, ']');
        if (!bracket_pos) {
            /* No more brackets - unterminated CDATA */
            break;
        }
        
        /* Move to bracket position */
        ctx->pos = bracket_pos;
        
        /* Check if it's "]]>" */
        if (ctx->pos[0] == ']' && ctx->pos[1] == ']' && ctx->pos[2] == '>') {
            /* Found end marker */
            *len = ctx->pos - content_start;
            ctx->pos += 3;  /* Skip "]]>" */
            return content_start;
        }
        
        /* Not a match, move past this bracket and continue */
        ctx->pos++;
    }
    
    /* Unterminated CDATA */
    taurus_parse_context_set_error(ctx,
        "Unterminated CDATA section at line %d, column %d",
        ctx->line, ctx->column);
    return NULL;
}

/* Parse text content (until '<' or end of buffer)
 *
 * Returns pointer to text content and sets *len.
 * Advances ctx->pos to the '<' character or end.
 * Returns NULL if text is empty or only whitespace (unless preserve_whitespace).
 *
 * Uses SIMD optimization to find '<' character quickly.
 *
 * NOTE: Returned pointer is into parse buffer, NOT null-terminated.
 */
const char *parse_text(TaurusParseContext *ctx, size_t *len) {
    const char *start;
    const char *text_end;
    const char *p;
    int has_non_whitespace;
    
    start = ctx->pos;
    has_non_whitespace = 0;
    
    /* Find next '<' using SIMD */
    text_end = simd_find_char(ctx->pos, ctx->end, '<');
    if (!text_end) {
        /* No '<' found - text goes to end of input */
        text_end = ctx->end;
    }
    
    /* Check for non-whitespace content */
    p = ctx->pos;
    while (p < text_end && !has_non_whitespace) {
        if (!taurus_is_whitespace(*p)) {
            has_non_whitespace = 1;
        }
        p++;
    }
    
    /* Advance position */
    ctx->pos = text_end;
    
    /* Calculate length */
    *len = text_end - start;
    
    /* Return NULL if only whitespace (unless preserving) */
    if (!has_non_whitespace && !ctx->opts.preserve_whitespace) {
        return NULL;
    }
    
    /* Return NULL if empty */
    if (*len == 0) {
        return NULL;
    }
    
    return start;
}