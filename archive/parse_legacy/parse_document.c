/* parse_document.c - Document-level parsing functions
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * Session 84: Modularized from leptris_parse.c
 * Main parsing entry points and document structure creation
 */

#include "parse_internal.h"
#include <string.h>

/* ==================================================================
 * PROCESSING INSTRUCTION PARSING
 * ================================================================== */

/* Parse processing instruction <?target data?>
 * Assumes positioned after '<?'
 * Returns newly created PI or NULL on error */
struct leptris_processing_instruction *parse_processing_instruction(LeptrisParseContext *ctx) {
    const char *target_start, *data_start;
    size_t target_len, data_len;
    char *target, *data;
    struct leptris_processing_instruction *pi;
    
    /* Parse target (PI name) */
    target_start = parse_name(ctx, &target_len);
    if (!target_start) {
        return NULL;
    }
    
    /* Copy target */
    target = leptris_strndup(target_start, target_len);
    if (!target) {
        return NULL;
    }
    
    /* Skip whitespace before data */
    leptris_skip_whitespace(&ctx->pos, ctx->end);
    
    /* Find '?>' end marker */
    data_start = ctx->pos;
    while (ctx->pos + 1 < ctx->end &&
           !(ctx->pos[0] == '?' && ctx->pos[1] == '>')) {
        if (*ctx->pos == '\n') {
            ctx->line++;
        }
        ctx->pos++;
    }
    
    if (ctx->pos + 1 >= ctx->end) {
        leptris_parse_context_set_error(ctx, "Unterminated processing instruction at line %d, column %d", ctx->line, ctx->column);
        leptris_free(target);
        return NULL;
    }
    
    /* Extract data (between target and '?>') */
    data_len = ctx->pos - data_start;
    data = (data_len > 0) ? leptris_strndup(data_start, data_len) : NULL;
    
    /* Skip '?>' */
    ctx->pos += 2;
    
    /* Create PI */
    pi = leptris_pi_new(target, data);
    leptris_free(target);
    if (data) leptris_free(data);
    
    return pi;
}

/* ==================================================================
 * MAIN PARSE FUNCTIONS
 * ================================================================== */

/* Parse XML string into document structure */
struct leptris_document *leptris_parse(const char *xml,
                                      size_t len,
                                      LeptrisParseOptions *opts) {
    LeptrisParseContext ctx;
    struct leptris_document *doc;
    struct leptris_element *root;
    
    /* Initialize context */
    if (leptris_parse_context_init(&ctx, xml, len, opts) < 0) {
        return NULL;
    }
    
    /* Create document */
    doc = leptris_document_new();
    if (!doc) {
        leptris_parse_context_free(&ctx);
        return NULL;
    }
    ctx.doc = doc;
    
    /* Skip leading whitespace */
    leptris_skip_whitespace(&ctx.pos, ctx.end);
    
    /* Parse processing instructions (including XML declaration) */
    while (ctx.pos + 1 < ctx.end &&
           ctx.pos[0] == '<' && ctx.pos[1] == '?') {
        ctx.pos += 2;  /* Skip '<?' */
        
        /* Parse processing instruction */
        struct leptris_processing_instruction *pi = parse_processing_instruction(&ctx);
        if (!pi) {
            leptris_document_free_internal(doc);
            leptris_parse_context_free(&ctx);
            return NULL;
        }
        
        /* Add to document PI list */
        pi->next = doc->pis;
        doc->pis = pi;
        
        leptris_skip_whitespace(&ctx.pos, ctx.end);
    }
    
    /* Parse root element */
    if (ctx.pos < ctx.end && *ctx.pos == '<') {
        ctx.pos++;  /* Skip '<' */
        root = parse_element(&ctx, NULL);
        if (!root) {
            leptris_document_free_internal(doc);
            leptris_parse_context_free(&ctx);
            return NULL;
        }
        
        /* Set as document root */
        doc->root = root;
    } else {
        leptris_parse_context_set_error(&ctx, "No root element found at line %d, column %d", ctx.line, ctx.column);
        leptris_document_free_internal(doc);
        leptris_parse_context_free(&ctx);
        return NULL;
    }
    
    /* Cleanup context */
    leptris_parse_context_free(&ctx);
    
    return doc;
}

/* Parse XML with error reporting */
struct leptris_document *leptris_parse_with_error(const char *xml,
                                                  size_t len,
                                                  LeptrisParseOptions *opts,
                                                  char *error_buf,
                                                  size_t error_len) {
    struct leptris_document *doc;
    LeptrisParseContext ctx;
    
    /* Initialize context */
    if (leptris_parse_context_init(&ctx, xml, len, opts) < 0) {
        if (error_buf && error_len > 0) {
            snprintf(error_buf, error_len, "Failed to initialize parse context");
        }
        return NULL;
    }
    
    /* Parse document */
    doc = leptris_parse(xml, len, opts);
    
    /* Copy error if parse failed */
    if (!doc && error_buf && error_len > 0) {
        const char *err = leptris_parse_context_error(&ctx);
        if (err[0] != '\0') {
            snprintf(error_buf, error_len, "%s", err);
        }
    }
    
    /* Cleanup */
    leptris_parse_context_free(&ctx);
    
    return doc;
}