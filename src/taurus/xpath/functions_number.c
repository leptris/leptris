/* functions_number.c - XPath number functions
 * Copyright (c) 2024, Ribose Inc.
 *
 * Implements XPath 1.0 number functions:
 * - number(object?) - Convert to number
 * - sum(node-set) - Sum numeric values
 * - floor(number) - Round down
 * - ceiling(number) - Round up
 * - round(number) - Round to nearest
 */

#include "functions_internal.h"

/* ============================================================================
 * XPath Number Functions
 * ============================================================================ */

/* number(object?) - Convert argument to number */
struct taurus_xpath_result* xpath_func_number(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count > 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "number() takes 0 or 1 argument");
        return NULL;
    }

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;

    if (arg_count == 0) {
        /* No argument: convert context node to number */
        char* str = get_node_text(context->context_node);
        const char* p = str;
        while (isspace((unsigned char)*p)) p++;

        if (*p == '\0') {
            result->value.number_value = NAN;
        } else {
            char* endptr;
            double value = strtod(p, &endptr);
            while (isspace((unsigned char)*endptr)) endptr++;
            result->value.number_value = (*endptr == '\0') ? value : NAN;
        }
        TAURUS_FREE(str);
    } else {
        /* Evaluate argument and convert to number */
        struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[0]);
        if (!arg_result) {
            xpath_result_free(result);
            return NULL;
        }
        result->value.number_value = xpath_to_number(arg_result);
        xpath_result_free(arg_result);
    }
    return result;
}

/* sum(node-set) - Sum the numeric values of all nodes */
struct taurus_xpath_result* xpath_func_sum(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "sum() requires exactly 1 argument");
        return NULL;
    }

    struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    if (arg_result->type != XPATH_RESULT_NODESET) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "sum() argument must be a nodeset");
        xpath_result_free(arg_result);
        return NULL;
    }

    XPathNodeSet* nodeset = arg_result->value.nodeset_value;
    double sum = 0.0;
    int has_nan = 0;

    if (nodeset) {
        size_t count = xpath_nodeset_count(nodeset);
        for (size_t i = 0; i < count; i++) {
            void* node = xpath_nodeset_get(nodeset, i);
            char* str = get_node_text(node);
            const char* p = str;
            while (isspace((unsigned char)*p)) p++;

            if (*p != '\0') {
                char* endptr;
                double value = strtod(p, &endptr);
                while (isspace((unsigned char)*endptr)) endptr++;
                if (*endptr == '\0') {
                    if (isnan(value)) {
                        has_nan = 1;
                    } else {
                        sum += value;
                    }
                } else {
                    /* Non-numeric value found */
                    has_nan = 1;
                }
            }
            TAURUS_FREE(str);
        }
    }
    xpath_result_free(arg_result);

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;

    if (has_nan) {
        result->value.number_value = NAN;
    } else {
        result->value.number_value = sum;
    }
    return result;
}

/* floor(number) - Largest integer not greater than argument */
struct taurus_xpath_result* xpath_func_floor(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "floor() requires exactly 1 argument");
        return NULL;
    }

    struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    double num = xpath_to_number(arg_result);
    xpath_result_free(arg_result);

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;
    result->value.number_value = floor(num);
    return result;
}

/* ceiling(number) - Smallest integer not less than argument */
struct taurus_xpath_result* xpath_func_ceiling(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "ceiling() requires exactly 1 argument");
        return NULL;
    }

    struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    double num = xpath_to_number(arg_result);
    xpath_result_free(arg_result);

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;
    result->value.number_value = ceil(num);
    return result;
}

/* round(number) - Round to nearest integer (half away from zero) */
struct taurus_xpath_result* xpath_func_round(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "round() requires exactly 1 argument");
        return NULL;
    }

    struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    double num = xpath_to_number(arg_result);
    xpath_result_free(arg_result);

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;

    /* XPath round() rounds half away from zero */
    if (isnan(num) || isinf(num) || num == 0.0) {
        result->value.number_value = num;
    } else {
        double frac = num - floor(num);
        if (frac == 0.5) {
            /* Half case: round away from zero - use ceil() for both positive and negative */
            result->value.number_value = ceil(num);
        } else {
            result->value.number_value = floor(num + 0.5);
        }
    }
    return result;
}
