/* parse_element.c - Element parsing functions for XML parser
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * Session 84: Modularized from taurus_parse.c
 * Functions for parsing element start tags, end tags, and complete elements
 */

#include "parse_internal.h"
#include <string.h>
#include <stdint.h>

/* ==================================================================
 * ELEMENT START TAG PARSING
 * ================================================================== */

/* Parse element start tag <name attr="value" ...>
 * Returns newly created element or NULL on error
 * NOTE: Return value may have lowest bit set to indicate self-closing */
struct taurus_element *parse_start_tag(TaurusParseContext *ctx,
                                        struct taurus_element *parent) {
    struct taurus_element *elem;
    const char *name;
    size_t name_len, value_len;
    char *name_copy, *prefix, *local_name;
    int self_closing = 0;
    
    /* Parse element name */
    name = parse_name(ctx, &name_len);
    if (!name) return NULL;
    
    /* Create a copy for processing */
    name_copy = taurus_strndup(name, name_len);
    if (!name_copy) return NULL;
    
    /* Extract prefix and local name */
    extract_prefix_and_local(name_copy, &prefix, &local_name);
    
    /* Initialize attribute stack */
    taurus_attr_stack_init(&ctx->attr_stack);
    
    /* Parse attributes */
    while (ctx->pos < ctx->end) {
        taurus_skip_whitespace(&ctx->pos, ctx->end);
        
        if (ctx->pos >= ctx->end) break;
        if (*ctx->pos == '>' || *ctx->pos == '/') break;
        
        /* Parse attribute name */
        const char *attr_name = parse_name(ctx, &name_len);
        if (!attr_name) {
            taurus_attr_stack_cleanup(&ctx->attr_stack);
            taurus_free(name_copy);
            return NULL;
        }
        
        /* Expect '=' */
        taurus_skip_whitespace(&ctx->pos, ctx->end);
        if (ctx->pos >= ctx->end || *ctx->pos != '=') {
            taurus_parse_context_set_error(ctx, "Expected '=' after attribute name at line %d, column %d", ctx->line, ctx->column);
            taurus_attr_stack_cleanup(&ctx->attr_stack);
            taurus_free(name_copy);
            return NULL;
        }
        ctx->pos++;
        
        /* Parse attribute value */
        const char *attr_value = parse_quoted_value(ctx, &value_len);
        if (!attr_value) {
            taurus_attr_stack_cleanup(&ctx->attr_stack);
            taurus_free(name_copy);
            return NULL;
        }
        
        /* Push attribute onto stack */
        taurus_attr_stack_push(&ctx->attr_stack, attr_name, attr_value);
    }
    
    /* Check for self-closing */
    taurus_skip_whitespace(&ctx->pos, ctx->end);
    if (ctx->pos < ctx->end && *ctx->pos == '/') {
        self_closing = 1;
        ctx->pos++;  /* Skip '/' */
    }
    
    /* Expect '>' */
    if (ctx->pos >= ctx->end || *ctx->pos != '>') {
        taurus_parse_context_set_error(ctx, "Expected '>' at line %d, column %d", ctx->line, ctx->column);
        taurus_attr_stack_cleanup(&ctx->attr_stack);
        taurus_free(name_copy);
        return NULL;
    }
    ctx->pos++;  /* Skip '>' */
    
    /* Create element */
    elem = taurus_element_new(local_name);
    if (!elem) {
        taurus_attr_stack_cleanup(&ctx->attr_stack);
        taurus_free(name_copy);
        return NULL;
    }
    
    /* Store self-closing flag (we'll use text_content as a hack for now) */
    if (self_closing) {
        /* Mark as self-closing - we'll handle this in parse_element */
    }
    
    /* Process namespace declarations */
    process_namespace_declarations(elem, &ctx->attr_stack);
    
    /* Set element namespace */
    set_element_namespace(elem, prefix, parent);
    
    /* Add attributes (excluding xmlns) */
    add_attributes_to_element(elem, &ctx->attr_stack, &ctx->intern_table);
    
    /* Link to parent */
    if (parent) {
        taurus_element_add_child(parent, elem);
    }
    
    /* Cleanup */
    taurus_attr_stack_cleanup(&ctx->attr_stack);
    taurus_free(name_copy);
    
    /* Return element with self_closing status encoded */
    return self_closing ? (struct taurus_element*)((uintptr_t)elem | 1) : elem;
}

/* ==================================================================
 * ELEMENT END TAG PARSING
 * ================================================================== */

/* Parse element end tag </name>
 * Returns 0 on success, -1 on error */
int parse_end_tag(TaurusParseContext *ctx, const char *expected_name) {
    const char *name;
    size_t len;
    char *name_copy;
    const char *local_name;
    int match;
    
    /* Should be at '</' */
    if (ctx->pos + 1 >= ctx->end ||
        ctx->pos[0] != '<' || ctx->pos[1] != '/') {
        taurus_parse_context_set_error(ctx, "Expected '</' at line %d, column %d", ctx->line, ctx->column);
        return -1;
    }
    ctx->pos += 2;
    
    /* Parse name */
    name = parse_name(ctx, &len);
    if (!name) return -1;
    
    /* Extract local name (remove prefix) */
    name_copy = taurus_strndup(name, len);
    if (!name_copy) return -1;
    
    local_name = extract_local_name_only(name_copy);
    
    /* Compare with expected */
    match = (strcmp(local_name, expected_name) == 0);
    
    taurus_free(name_copy);
    
    if (!match) {
        taurus_parse_context_set_error(ctx,
            "Mismatched end tag at line %d, column %d: expected %s",
            ctx->line, ctx->column, expected_name);
        return -1;
    }
    
    /* Expect '>' */
    taurus_skip_whitespace(&ctx->pos, ctx->end);
    if (ctx->pos >= ctx->end || *ctx->pos != '>') {
        taurus_parse_context_set_error(ctx,
            "Expected '>' in end tag at line %d, column %d", ctx->line, ctx->column);
        return -1;
    }
    ctx->pos++;
    
    return 0;
}

/* ==================================================================
 * COMPLETE ELEMENT PARSING
 * ================================================================== */

/* Parse complete element (start tag, content, end tag)
 * Returns newly created element or NULL on error */
struct taurus_element *parse_element(TaurusParseContext *ctx,
                                      struct taurus_element *parent) {
    struct taurus_element *elem;
    uintptr_t elem_ptr;
    int self_closing;
    
    /* Parse start tag */
    elem = parse_start_tag(ctx, parent);
    if (!elem) return NULL;
    
    /* Check if self-closing (encoded in lowest bit) */
    elem_ptr = (uintptr_t)elem;
    self_closing = elem_ptr & 1;
    elem = (struct taurus_element*)(elem_ptr & ~1);
    
    /* If self-closing, we're done */
    if (self_closing) {
        return elem;
    }
    
    /* Parse content */
    while (ctx->pos < ctx->end) {
        taurus_skip_whitespace(&ctx->pos, ctx->end);
        
        if (ctx->pos >= ctx->end) break;
        
        if (*ctx->pos == '<') {
            ctx->pos++;
            
            if (ctx->pos >= ctx->end) {
                taurus_parse_context_set_error(ctx, "Unexpected end after '<' at line %d, column %d", ctx->line, ctx->column);
                taurus_element_free_tree(elem);
                return NULL;
            }
            
            if (*ctx->pos == '/') {
                /* End tag - rewind and let parse_end_tag handle */
                ctx->pos--;
                break;
            } else if (ctx->pos + 2 < ctx->end &&
                       *ctx->pos == '!' && ctx->pos[1] == '-' && ctx->pos[2] == '-') {
                /* Comment */
                ctx->pos += 3;  /* Skip '!--' */
                if (skip_comment(ctx) < 0) {
                    taurus_element_free_tree(elem);
                    return NULL;
                }
            } else if (ctx->pos + 7 < ctx->end &&
                       *ctx->pos == '!' &&
                       strncmp(ctx->pos + 1, "[CDATA[", 7) == 0) {
                /* CDATA */
                ctx->pos += 8;  /* Skip '![CDATA[' */
                size_t len;
                const char *cdata = parse_cdata(ctx, &len);
                if (!cdata) {
                    taurus_element_free_tree(elem);
                    return NULL;
                }
                /* Add CDATA as text content */
                add_text_to_element(elem, cdata, len);
            } else {
                /* Child element - already positioned at element name after '<' */
                struct taurus_element *child = parse_element(ctx, elem);
                if (!child) {
                    taurus_element_free_tree(elem);
                    return NULL;
                }
                /* child already added to elem by parse_start_tag */
            }
        } else {
            /* Text content */
            size_t len;
            const char *text = parse_text(ctx, &len);
            if (text) {
                add_text_to_element(elem, text, len);
            }
        }
    }
    
    /* Parse end tag */
    if (parse_end_tag(ctx, elem->name) < 0) {
        taurus_element_free_tree(elem);
        return NULL;
    }
    
    return elem;
}