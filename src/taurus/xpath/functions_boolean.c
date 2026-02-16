/* functions_boolean.c - XPath boolean functions
 * Copyright (c) 2024, Ribose Inc.
 *
 * Implements XPath 1.0 boolean functions:
 * - boolean(object) - Convert to boolean
 * - not(boolean) - Logical negation
 * - true() - Return boolean true
 * - false() - Return boolean false
 */

#include "functions_internal.h"

/* ============================================================================
 * XPath Boolean Functions
 * ============================================================================ */

/* boolean(object) - Convert any object to boolean */
struct taurus_xpath_result* xpath_func_boolean(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "boolean() requires exactly 1 argument");
        return NULL;
    }

    /* Evaluate argument */
    struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    /* Convert to boolean using evaluator_types.c function */
    int bool_value = xpath_to_boolean(arg_result);
    xpath_result_free(arg_result);

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) return NULL;
    result->value.boolean_value = bool_value;

    return result;
}

/* not(boolean) - Logical negation */
struct taurus_xpath_result* xpath_func_not(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "not() requires exactly 1 argument");
        return NULL;
    }

    /* Evaluate argument */
    struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    /* Convert to boolean and negate */
    int bool_value = xpath_to_boolean(arg_result);
    xpath_result_free(arg_result);

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) return NULL;
    result->value.boolean_value = !bool_value;

    return result;
}

/* true() - Returns boolean true */
struct taurus_xpath_result* xpath_func_true(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    (void)context;  /* Unused */
    (void)args;     /* Unused */

    if (arg_count != 0) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "true() takes no arguments");
        return NULL;
    }

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) return NULL;
    result->value.boolean_value = 1;

    return result;
}

/* false() - Returns boolean false */
struct taurus_xpath_result* xpath_func_false(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    (void)context;  /* Unused */
    (void)args;     /* Unused */

    if (arg_count != 0) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "false() takes no arguments");
        return NULL;
    }

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) return NULL;
    result->value.boolean_value = 0;

    return result;
}
