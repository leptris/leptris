/* functions_internal.h - XPath functions internal declarations
 * Copyright (c) 2024, Ribose Inc.
 *
 * Internal macros and declarations for XPath function implementations.
 */

#ifndef XPATH_FUNCTIONS_INTERNAL_H
#define XPATH_FUNCTIONS_INTERNAL_H

#include "functions.h"
#include "evaluator.h"
#include "../include/taurus.h"
#include "../dom/element.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <float.h>
#include <errno.h>

/* ============================================================================
 * Argument Validation Macros
 * ============================================================================ */

/* Validate argument count */
#define VALIDATE_ARGS(ctx, name, expected) \
    if (arg_count != expected) { \
        snprintf((ctx)->error_msg, sizeof((ctx)->error_msg), \
                "%s() requires exactly %d argument(s)", name, expected); \
        return NULL; \
    }

/* Validate minimum argument count */
#define VALIDATE_MIN_ARGS(ctx, name, min) \
    if (arg_count < min) { \
        snprintf((ctx)->error_msg, sizeof((ctx)->error_msg), \
                "%s() requires at least %d argument(s)", name, min); \
        return NULL; \
    }

/* Validate argument count range */
#define VALIDATE_ARGS_RANGE(ctx, name, min, max) \
    if (arg_count < min || arg_count > max) { \
        snprintf((ctx)->error_msg, sizeof((ctx)->error_msg), \
                "%s() requires %d to %d arguments", name, min, max); \
        return NULL; \
    }

/* ============================================================================
 * Result Creation Macros
 * ============================================================================ */

/* Create and return a number result */
#define RETURN_NUMBER(val) \
    do { \
        struct taurus_xpath_result* r = xpath_result_new(XPATH_RESULT_NUMBER); \
        if (r) r->value.number_value = (val); \
        return r; \
    } while(0)

/* Create and return a boolean result */
#define RETURN_BOOLEAN(val) \
    do { \
        struct taurus_xpath_result* r = xpath_result_new(XPATH_RESULT_BOOLEAN); \
        if (r) r->value.boolean_value = (val) ? 1 : 0; \
        return r; \
    } while(0)

/* Create and return a string result */
#define RETURN_STRING(val) \
    do { \
        struct taurus_xpath_result* r = xpath_result_new(XPATH_RESULT_STRING); \
        if (r) r->value.string_value = (val); \
        return r; \
    } while(0)

/* Create and return an empty string result */
#define RETURN_EMPTY_STRING() \
    do { \
        struct taurus_xpath_result* r = xpath_result_new(XPATH_RESULT_STRING); \
        if (r) r->value.string_value = taurus_strdup(""); \
        return r; \
    } while(0)

/* ============================================================================
 * Helper Function Declarations
 * ============================================================================ */

/* External evaluator function */
extern struct taurus_xpath_result* xpath_evaluate(XPathContext* context,
                                                   XPathASTNode* ast);

/* Get text content from typed node (handles elements and attributes) */
extern char* get_node_text(void* node);

/* UTF-8 helper functions */
size_t utf8_strlen(const char* str);
size_t utf8_char_offset(const char* str, size_t char_pos);
char* utf8_substring(const char* str, size_t start_char, size_t char_count);

/* Type conversion helpers - from evaluator_types.c */
extern char* xpath_to_string(struct taurus_xpath_result* result);
extern int xpath_to_boolean(struct taurus_xpath_result* result);
extern double xpath_to_number(struct taurus_xpath_result* result);

/* ============================================================================
 * Core Function Declarations
 * ============================================================================ */

struct taurus_xpath_result* xpath_func_last(XPathContext* context,
                                             XPathASTNode** args,
                                             size_t arg_count);

struct taurus_xpath_result* xpath_func_position(XPathContext* context,
                                                 XPathASTNode** args,
                                                 size_t arg_count);

struct taurus_xpath_result* xpath_func_count(XPathContext* context,
                                              XPathASTNode** args,
                                              size_t arg_count);

/* ============================================================================
 * String Function Declarations
 * ============================================================================ */

struct taurus_xpath_result* xpath_func_string(XPathContext* context,
                                               XPathASTNode** args,
                                               size_t arg_count);

struct taurus_xpath_result* xpath_func_concat(XPathContext* context,
                                               XPathASTNode** args,
                                               size_t arg_count);

struct taurus_xpath_result* xpath_func_starts_with(XPathContext* context,
                                                    XPathASTNode** args,
                                                    size_t arg_count);

struct taurus_xpath_result* xpath_func_contains(XPathContext* context,
                                                 XPathASTNode** args,
                                                 size_t arg_count);

struct taurus_xpath_result* xpath_func_substring(XPathContext* context,
                                                  XPathASTNode** args,
                                                  size_t arg_count);

struct taurus_xpath_result* xpath_func_substring_before(XPathContext* context,
                                                         XPathASTNode** args,
                                                         size_t arg_count);

struct taurus_xpath_result* xpath_func_substring_after(XPathContext* context,
                                                        XPathASTNode** args,
                                                        size_t arg_count);

struct taurus_xpath_result* xpath_func_string_length(XPathContext* context,
                                                      XPathASTNode** args,
                                                      size_t arg_count);

struct taurus_xpath_result* xpath_func_normalize_space(XPathContext* context,
                                                        XPathASTNode** args,
                                                        size_t arg_count);

struct taurus_xpath_result* xpath_func_translate(XPathContext* context,
                                                  XPathASTNode** args,
                                                  size_t arg_count);

/* ============================================================================
 * Boolean Function Declarations
 * ============================================================================ */

struct taurus_xpath_result* xpath_func_boolean(XPathContext* context,
                                                XPathASTNode** args,
                                                size_t arg_count);

struct taurus_xpath_result* xpath_func_not(XPathContext* context,
                                            XPathASTNode** args,
                                            size_t arg_count);

struct taurus_xpath_result* xpath_func_true(XPathContext* context,
                                             XPathASTNode** args,
                                             size_t arg_count);

struct taurus_xpath_result* xpath_func_false(XPathContext* context,
                                              XPathASTNode** args,
                                              size_t arg_count);

struct taurus_xpath_result* xpath_func_lang(XPathContext* context,
                                             XPathASTNode** args,
                                             size_t arg_count);

/* ============================================================================
 * Number Function Declarations
 * ============================================================================ */

struct taurus_xpath_result* xpath_func_number(XPathContext* context,
                                               XPathASTNode** args,
                                               size_t arg_count);

struct taurus_xpath_result* xpath_func_sum(XPathContext* context,
                                            XPathASTNode** args,
                                            size_t arg_count);

struct taurus_xpath_result* xpath_func_floor(XPathContext* context,
                                              XPathASTNode** args,
                                              size_t arg_count);

struct taurus_xpath_result* xpath_func_ceiling(XPathContext* context,
                                                XPathASTNode** args,
                                                size_t arg_count);

struct taurus_xpath_result* xpath_func_round(XPathContext* context,
                                              XPathASTNode** args,
                                              size_t arg_count);

/* ============================================================================
 * Node-set Function Declarations
 * ============================================================================ */

struct taurus_xpath_result* xpath_func_id(XPathContext* context,
                                           XPathASTNode** args,
                                           size_t arg_count);

struct taurus_xpath_result* xpath_func_local_name(XPathContext* context,
                                                   XPathASTNode** args,
                                                   size_t arg_count);

struct taurus_xpath_result* xpath_func_namespace_uri(XPathContext* context,
                                                      XPathASTNode** args,
                                                      size_t arg_count);

struct taurus_xpath_result* xpath_func_name(XPathContext* context,
                                             XPathASTNode** args,
                                             size_t arg_count);

/* ============================================================================
 * Custom Function Support
 * ============================================================================ */

/**
 * Look up a custom function by name
 *
 * @param name Function name
 * @return Function handler or NULL if not found
 */
XPathFunctionHandler xpath_custom_function_lookup(const char* name);

#endif /* XPATH_FUNCTIONS_INTERNAL_H */
