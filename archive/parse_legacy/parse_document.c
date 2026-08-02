/* parse_document.c - Document-level parsing functions
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * Session 84: Modularized from taurus_parse.c
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
struct taurus_processing_instruction *parse_processing_instruction(TaurusParseContext *ctx) {
    const char *target_start, *data_start;
    size_t target_len, data_len;
    char *target, *data;
    struct taurus_processing_instruction *pi;
    
    /* Parse target (PI name) */
    target_start = parse_name(ctx, &target_len);
    if (!target_start) {
        return NULL;
    }
    
    /* Copy target */
    target = taurus_strndup(target_start, target_len);
    if (!target) {
        return NULL;
    }
    
    /* Skip whitespace before data */
    taurus_skip_whitespace(&ctx->pos, ctx->end);
    
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
        taurus_parse_context_set_error(ctx, "Unterminated processing instruction at line %d, column %d", ctx->line, ctx->column);
        taurus_free(target);
        return NULL;
    }
    
    /* Extract data (between target and '?>') */
    data_len = ctx->pos - data_start;
    data = (data_len > 0) ? taurus_strndup(data_start, data_len) : NULL;
    
    /* Skip '?>' */
    ctx->pos += 2;
    
    /* Create PI */
    pi = taurus_pi_new(target, data);
    taurus_free(target);
    if (data) taurus_free(data);
    
    return pi;
}

/* ==================================================================
 * MAIN PARSE FUNCTIONS
 * ================================================================== */

/* Parse XML string into document structure */
struct taurus_document *taurus_parse(const char *xml,
                                      size_t len,
                                      TaurusParseOptions *opts) {
    TaurusParseContext ctx;
    struct taurus_document *doc;
    struct taurus_element *root;
    
    /* Initialize context */
    if (taurus_parse_context_init(&ctx, xml, len, opts) < 0) {
        return NULL;
    }
    
    /* Create document */
    doc = taurus_document_new();
    if (!doc) {
        taurus_parse_context_free(&ctx);
        return NULL;
    }
    ctx.doc = doc;
    
    /* Skip leading whitespace */
    taurus_skip_whitespace(&ctx.pos, ctx.end);
    
    /* Parse processing instructions (including XML declaration) */
    while (ctx.pos + 1 < ctx.end &&
           ctx.pos[0] == '<' && ctx.pos[1] == '?') {
        ctx.pos += 2;  /* Skip '<?' */
        
        /* Parse processing instruction */
        struct taurus_processing_instruction *pi = parse_processing_instruction(&ctx);
        if (!pi) {
            taurus_document_free_internal(doc);
            taurus_parse_context_free(&ctx);
            return NULL;
        }
        
        /* Add to document PI list */
        pi->next = doc->pis;
        doc->pis = pi;
        
        taurus_skip_whitespace(&ctx.pos, ctx.end);
    }
    
    /* Parse root element */
    if (ctx.pos < ctx.end && *ctx.pos == '<') {
        ctx.pos++;  /* Skip '<' */
        root = parse_element(&ctx, NULL);
        if (!root) {
            taurus_document_free_internal(doc);
            taurus_parse_context_free(&ctx);
            return NULL;
        }
        
        /* Set as document root */
        doc->root = root;
    } else {
        taurus_parse_context_set_error(&ctx, "No root element found at line %d, column %d", ctx.line, ctx.column);
        taurus_document_free_internal(doc);
        taurus_parse_context_free(&ctx);
        return NULL;
    }
    
    /* Cleanup context */
    taurus_parse_context_free(&ctx);
    
    return doc;
}

/* Parse XML with error reporting */
struct taurus_document *taurus_parse_with_error(const char *xml,
                                                  size_t len,
                                                  TaurusParseOptions *opts,
                                                  char *error_buf,
                                                  size_t error_len) {
    struct taurus_document *doc;
    TaurusParseContext ctx;
    
    /* Initialize context */
    if (taurus_parse_context_init(&ctx, xml, len, opts) < 0) {
        if (error_buf && error_len > 0) {
            snprintf(error_buf, error_len, "Failed to initialize parse context");
        }
        return NULL;
    }
    
    /* Parse document */
    doc = taurus_parse(xml, len, opts);
    
    /* Copy error if parse failed */
    if (!doc && error_buf && error_len > 0) {
        const char *err = taurus_parse_context_error(&ctx);
        if (err[0] != '\0') {
            snprintf(error_buf, error_len, "%s", err);
        }
    }
    
    /* Cleanup */
    taurus_parse_context_free(&ctx);
    
    return doc;
}