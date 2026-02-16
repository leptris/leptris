/* functions_core.c - XPath core functions
 * Copyright (c) 2024, Ribose Inc.
 *
 * Implements core XPath 1.0 functions:
 * - last() - Returns context size
 * - position() - Returns context position
 */

#include "functions_internal.h"

/* ============================================================================
 * Core XPath 1.0 Functions
 * ============================================================================ */

/* last() - Returns the context size (number of nodes in context nodeset) */
struct taurus_xpath_result* xpath_func_last(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    (void)args;  /* Unused */

    if (arg_count != 0) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "last() takes no arguments");
        return NULL;
    }

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;

    result->value.number_value = (double)context->context_size;
    return result;
}

/* position() - Returns the context position (1-based) */
struct taurus_xpath_result* xpath_func_position(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    (void)args;  /* Unused */

    if (arg_count != 0) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "position() takes no arguments");
        return NULL;
    }

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;

    result->value.number_value = (double)context->context_position;
    return result;
}
