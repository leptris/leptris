/* functions.c - XPath 1.0 function library implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C implementation of all 27 XPath 1.0 standard functions.
 * Converted from Ruby C extension to standalone C library.
 */

#include "functions.h"
#include "evaluator.h"
#include "evaluator_internal.h"
#include "../include/leptris.h"
#include "../dom/element.h"
#include "../dom/pi.h"
#include "../dom/text.h"
#include "../dom/cdata.h"
#include "../dom/comment.h"
#include "../common/port.h"
#include "../dtd/model.h"   /* ttdtd_lookup_attribute (id() §4.1) */
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <float.h>
#include <errno.h>

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

/* External evaluator function */
extern struct leptris_xpath_result* xpath_evaluate(XPathContext* context,
                                                   XPathASTNode* ast);

/* Helper functions.
 * get_node_text is implemented in evaluator_types.c; the extern
 * decl appears in the type-conversion block below. */
static char* result_to_string(struct leptris_xpath_result* result);
static int result_to_boolean(struct leptris_xpath_result* result);
static double result_to_number(struct leptris_xpath_result* result);

/* XPath round() per spec section 4.4: nearest integer, ties toward
 * positive infinity. Used by substring() and exposed as the public
 * round() function. NaN/Inf pass through unchanged. */
static double xpath_round_half_up(double num) {
    if (isnan(num) || isinf(num) || num == 0.0) return num;
    double frac = num - floor(num);
    if (frac == 0.5) return ceil(num);  /* tie -> +Inf */
    return floor(num + 0.5);
}

/* ============================================================================
 * Standard function registry singleton (TODO 113 perf optimization)
 *
 * The standard XPath 1.0 function set is fixed (27 functions). Every
 * leptris_xpath_eval call previously allocated a new registry, registered
 * all 27 functions (each with a heap-allocated name string), then freed
 * them. That's ~30 heap operations per query just for function setup.
 *
 * The singleton is initialized on first call and never freed. Reads
 * are concurrent-safe (no mutating operations after init). The
 * registry is internal-only — never modified at runtime.
 * ============================================================================ */

static XPathFunctionRegistry* g_standard_registry = NULL;

const XPathFunctionRegistry* xpath_function_registry_get_standard_impl(void) {
    return g_standard_registry;
}

/* Lazily initialize the global registry on first call. After init,
 * the registry is read-only.
 *
 * TODO.concurrency/08: built under a mutex — the old unlocked
 * lazy-init had a double-build race on the first concurrent eval.
 * (A constructor-time build was tried and reverted: it allocated
 * the registry in every process that links libleptris, even ones
 * that never evaluate XPath, and valgrind flagged it in binaries
 * where LTO made the global unreachable.) All reads go through the
 * same mutex — no unlocked fast path, so it is also race-clean
 * under ThreadSanitizer. */
static leptris_mutex_t g_standard_registry_mutex = LEPTRIS_MUTEX_INIT;

XPathFunctionRegistry* xpath_function_registry_get_standard(void) {
    LEPTRIS_MUTEX_LOCK(&g_standard_registry_mutex);
    if (!g_standard_registry) {
        XPathFunctionRegistry* r = xpath_function_registry_new();
        if (r) xpath_function_registry_init_standard(r);
        g_standard_registry = r;
    }
    LEPTRIS_MUTEX_UNLOCK(&g_standard_registry_mutex);
    return g_standard_registry;
}

/* ============================================================================
 * Function Registry Implementation
 * ============================================================================ */

XPathFunctionRegistry* xpath_function_registry_new(void) {
    XPathFunctionRegistry* registry = LEPTRIS_ALLOC(XPathFunctionRegistry);
    if (!registry) return NULL;

    registry->functions = NULL;
    registry->count = 0;
    registry->capacity = 0;

    return registry;
}

void xpath_function_registry_free(XPathFunctionRegistry* registry) {
    if (!registry) return;

    if (registry->functions) {
        LEPTRIS_FREE(registry->functions);
    }
    LEPTRIS_FREE(registry);
}

void xpath_function_registry_register_ud(
    XPathFunctionRegistry* registry,
    const char* name,
    XPathFunctionHandler handler,
    int min_args,
    int max_args,
    void* user_data
) {
    if (!registry || !name || !handler) return;
    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->functions[i].name, name) == 0) {
            registry->functions[i].handler = handler;
            registry->functions[i].min_args = min_args;
            registry->functions[i].max_args = max_args;
            registry->functions[i].user_data = user_data;
            return;
        }
    }
    xpath_function_registry_register(registry, name, handler,
                                     min_args, max_args);
    if (registry->count > 0)
        registry->functions[registry->count - 1].user_data = user_data;
}

void xpath_function_registry_register(
    XPathFunctionRegistry* registry,
    const char* name,
    XPathFunctionHandler handler,
    int min_args,
    int max_args
) {
    if (!registry || !name || !handler) return;

    /* Resize if needed */
    if (registry->count >= registry->capacity) {
        size_t new_capacity = registry->capacity == 0 ? 8 : registry->capacity * 2;
        XPathFunctionDef* new_functions = LEPTRIS_REALLOC_N(
            registry->functions,
            XPathFunctionDef,
            new_capacity
        );
        if (!new_functions) return;
        registry->functions = new_functions;
        registry->capacity = new_capacity;
    }

    /* Last registration WINS: the standard library loads first,
     * then ext31 families, then the XSLT bridge / EXSLT packs —
     * each layer overrides same-named core entries (the bridge's
     * format-number must own the name inside transforms). */
    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->functions[i].name, name) == 0) {
            registry->functions[i].handler = handler;
            registry->functions[i].min_args = min_args;
            registry->functions[i].max_args = max_args;
            registry->functions[i].user_data = NULL;
            return;
        }
    }

    /* Add function. user_data must be written explicitly: the
     * array is realloc'd, so a skipped field carries heap garbage
     * until someone patches it (#1111). */
    registry->functions[registry->count].name = name;
    registry->functions[registry->count].handler = handler;
    registry->functions[registry->count].min_args = min_args;
    registry->functions[registry->count].max_args = max_args;
    registry->functions[registry->count].user_data = NULL;
    registry->count++;
}

XPathFunctionHandler xpath_function_registry_lookup(
    XPathFunctionRegistry* registry,
    const char* name
) {
    if (!registry || !name) return NULL;

    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->functions[i].name, name) == 0) {
            return registry->functions[i].handler;
        }
    }

    return NULL;
}

XPathFunctionDef* xpath_function_registry_get(
    XPathFunctionRegistry* registry,
    const char* name
) {
    if (!registry || !name) return NULL;

    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->functions[i].name, name) == 0) {
            return &registry->functions[i];
        }
    }

    return NULL;
}

/* ============================================================================
 * Helper Functions for Type Conversion
 * ============================================================================ */

/* Get text content from typed node (handles elements and attributes)
 * This is declared in evaluator_internal.h and implemented in evaluator_types.c
 */
extern char* get_node_text(void* node);
extern char* xpath_number_to_string(double number);
extern char* xpath_int_to_string(long long v);

/* Backward compatibility wrapper */
static char* get_element_text(LeptrisElement element) {
    return get_node_text((void*)element);
}

/* Convert XPath result to string according to XPath 1.0 spec */
static char* result_to_string(struct leptris_xpath_result* result) {
    if (!result) return leptris_strdup("");

    switch (result->type) {
        case XPATH_RESULT_STRING:
            return result->value.string_value ?
                   leptris_strdup(result->value.string_value) : leptris_strdup("");

        case XPATH_RESULT_NUMBER: {
            if (result->is_int) return xpath_int_to_string(result->int_value);
            return xpath_number_to_string(result->value.number_value);
        }

        case XPATH_RESULT_BOOLEAN:
            return leptris_strdup(result->value.boolean_value ? "true" : "false");

        case XPATH_RESULT_NODESET: {
            /* String value of first node in document order */
            XPathNodeSet* nodeset = result->value.nodeset_value;
            if (!nodeset || xpath_nodeset_count(nodeset) == 0) {
                return leptris_strdup("");
            }
            void* first_node = xpath_nodeset_get(nodeset, 0);
            return get_node_text(first_node);
        }

        default:
            return leptris_strdup("");
    }
}

/* Convert XPath result to boolean according to XPath 1.0 spec */
static int result_to_boolean(struct leptris_xpath_result* result) {
    if (!result) return 0;

    switch (result->type) {
        case XPATH_RESULT_BOOLEAN:
            return result->value.boolean_value;
        case XPATH_RESULT_NUMBER:
            /* Number is false if 0 or NaN */
            return result->value.number_value != 0.0 &&
                   !isnan(result->value.number_value);
        case XPATH_RESULT_STRING:
            /* String is false if empty */
            return result->value.string_value &&
                   result->value.string_value[0] != '\0';
        case XPATH_RESULT_NODESET:
            /* Nodeset is false if empty */
            return xpath_nodeset_count(result->value.nodeset_value) > 0;
        default:
            return 0;
    }
}

/* Convert XPath result to number according to XPath 1.0 spec */
static double result_to_number(struct leptris_xpath_result* result) {
    if (!result) return NAN;

    switch (result->type) {
        case XPATH_RESULT_NUMBER:
            return result->value.number_value;

        case XPATH_RESULT_BOOLEAN:
            return result->value.boolean_value ? 1.0 : 0.0;

        case XPATH_RESULT_STRING: {
            if (!result->value.string_value) return NAN;

            const char* str = result->value.string_value;

            /* Skip leading whitespace */
            while (isspace((unsigned char)*str)) str++;

            /* Empty string or only whitespace -> NaN */
            if (*str == '\0') return NAN;

            /* Try to parse as number */
            char* endptr;
            double value = strtod(str, &endptr);

            /* Skip trailing whitespace */
            while (isspace((unsigned char)*endptr)) endptr++;

            /* If we didn't consume the entire string (after trimming), it's NaN */
            if (*endptr != '\0') return NAN;

            return value;
        }

        case XPATH_RESULT_NODESET: {
            /* Convert first node's string value to number */
            XPathNodeSet* nodeset = result->value.nodeset_value;
            if (!nodeset || xpath_nodeset_count(nodeset) == 0) {
                return NAN;
            }
            void* first_node = xpath_nodeset_get(nodeset, 0);
            char* str = get_node_text(first_node);

            /* Parse the string */
            const char* p = str;
            while (isspace((unsigned char)*p)) p++;

            if (*p == '\0') {
                LEPTRIS_FREE(str);
                return NAN;
            }

            char* endptr;
            double value = strtod(p, &endptr);

            while (isspace((unsigned char)*endptr)) endptr++;

            if (*endptr != '\0') {
                LEPTRIS_FREE(str);
                return NAN;
            }

            LEPTRIS_FREE(str);
            return value;
        }

        default:
            return NAN;
    }
}

/* ============================================================================
 * UTF-8 Helper Functions
 * ============================================================================ */

/* ============================================================================
 * Core XPath 1.0 Functions
 * ============================================================================ */

/* last() - Returns the context size (number of nodes in context nodeset) */
static struct leptris_xpath_result* xpath_func_last(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    (void)args;  /* Unused */

    if (arg_count != 0) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "last() takes no arguments");
        return NULL;
    }

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;

    result->value.number_value = (double)context->context_size;
    return result;
}

/* position() - Returns the context position (1-based) */
static struct leptris_xpath_result* xpath_func_position(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    (void)args;  /* Unused */

    if (arg_count != 0) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "position() takes no arguments");
        return NULL;
    }

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;

    result->value.number_value = (double)context->context_position;
    return result;
}

/* ============================================================================
 * XPath String Functions
 * ============================================================================ */

/* string(object?) - Convert argument to string */
static struct leptris_xpath_result* xpath_func_string(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) return NULL;

    if (arg_count == 0) {
        /* No argument: convert context node to string — the kind-
         * aware walker covers attribute/namespace contexts (fn
         * steps like `@b/string()`); element text stays fast. */
        if (context->context_node &&
            XPATH_NODE_TYPE(context->context_node) !=
                LEPTRIS_NODE_ELEMENT) {
            result->value.string_value =
                get_node_text(context->context_node);
        } else {
            result->value.string_value =
                get_element_text(context->context_node);
        }
    } else {
        /* Evaluate argument and convert to string */
        struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[0]);
        if (!arg_result) {
            xpath_result_free(result);
            return NULL;
        }

        result->value.string_value =
            (context->xquery_spelling &&
             arg_result->type == XPATH_RESULT_NUMBER &&
             !arg_result->is_int)
                ? xpath_number_to_string_xq(
                      arg_result->value.number_value)
                : result_to_string(arg_result);
        xpath_result_free(arg_result);
    }

    return result;
}

/* concat(string, string, string*) - Concatenate strings */
static struct leptris_xpath_result* xpath_func_concat(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count < 2) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "concat() requires at least 2 arguments");
        return NULL;
    }

    /* Calculate total length needed */
    size_t total_length = 0;
    char** strings = LEPTRIS_ALLOC_N(char*, arg_count);
    if (!strings) return NULL;

    for (size_t i = 0; i < arg_count; i++) {
        struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[i]);
        if (!arg_result) {
            for (size_t j = 0; j < i; j++) {
                LEPTRIS_FREE(strings[j]);
            }
            LEPTRIS_FREE(strings);
            return NULL;
        }

        strings[i] = result_to_string(arg_result);
        total_length += strlen(strings[i]);
        xpath_result_free(arg_result);
    }

    /* Allocate result string */
    char* concat_str = LEPTRIS_ALLOC_N(char, total_length + 1);
    if (!concat_str) {
        for (size_t i = 0; i < arg_count; i++) {
            LEPTRIS_FREE(strings[i]);
        }
        LEPTRIS_FREE(strings);
        return NULL;
    }

    /* Concatenate all strings */
    memset(concat_str, 0, total_length + 1);
    size_t index = 0;
    for (size_t i = 0; i < arg_count; i++) {
        size_t len = strlen(strings[i]);  /* Get length BEFORE freeing */
        strcpy(concat_str + index, strings[i]);
        index += len;
        LEPTRIS_FREE(strings[i]);
    }
    LEPTRIS_FREE(strings);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        LEPTRIS_FREE(concat_str);
        return NULL;
    }
    result->value.string_value = concat_str;
    return result;
}

/* Collation-argument mode: 0 = codepoint (the default
 * semantics), 1 = html-ascii-case-insensitive, -1 = unknown
 * collation (error message set, callers return NULL). */
static int collation_mode(XPathContext* context, XPathASTNode* arg) {
    struct leptris_xpath_result* r = xpath_evaluate(context, arg);
    if (!r) return -1;
    char* uri = result_to_string(r);
    int mode = 0;
    if (uri && strcmp(uri,
            "http://www.w3.org/2005/xpath-functions/collation/"
            "html-ascii-case-insensitive") == 0) {
        mode = 1;
    } else if (uri && strcmp(uri,
            "http://www.w3.org/2005/xpath-functions/collation/"
            "codepoint") != 0) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "unknown collation %s", uri);
        mode = -1;
    }
    LEPTRIS_FREE(uri);
    xpath_result_free(r);
    return mode;
}

/* starts-with(string, string[, collation]) - Check if string
 * starts with prefix; the 3-arg form carries a collation URI. */
static struct leptris_xpath_result* xpath_func_starts_with(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 2 && arg_count != 3) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "starts-with() requires 2 or 3 arguments, got %zu", arg_count);
        return NULL;
    }

    struct leptris_xpath_result* str_result = xpath_evaluate(context, args[0]);
    if (!str_result) return NULL;

    struct leptris_xpath_result* prefix_result = xpath_evaluate(context, args[1]);
    if (!prefix_result) {
        xpath_result_free(str_result);
        return NULL;
    }

    int ascii_ci = 0;
    if (arg_count == 3) {
        int mode = collation_mode(context, args[2]);
        if (mode < 0) {
            xpath_result_free(str_result);
            xpath_result_free(prefix_result);
            return NULL;
        }
        ascii_ci = mode == 1;
    }

    char* str = result_to_string(str_result);
    char* prefix = result_to_string(prefix_result);

    int match;
    if (ascii_ci) {
        const char* a = str ? str : "";
        const char* b = prefix ? prefix : "";
        while (*a && *b &&
               tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        match = *b == '\0';
    } else {
        match = (strncmp(str ? str : "", prefix ? prefix : "",
                         prefix ? strlen(prefix) : 0) == 0);
    }

    LEPTRIS_FREE(str);
    LEPTRIS_FREE(prefix);
    xpath_result_free(str_result);
    xpath_result_free(prefix_result);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) return NULL;
    result->value.boolean_value = match;

    return result;
}

/* ends-with(string, string[, collation]) — XPath 2.0 (#684): true
 * when the first string's tail equals the suffix (an empty suffix
 * always matches); the 3-arg form carries a collation URI. */
static struct leptris_xpath_result* xpath_func_ends_with(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 2 && arg_count != 3) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "ends-with() requires 2 or 3 arguments, got %zu", arg_count);
        return NULL;
    }

    struct leptris_xpath_result* str_result = xpath_evaluate(context, args[0]);
    if (!str_result) return NULL;

    struct leptris_xpath_result* suffix_result = xpath_evaluate(context, args[1]);
    if (!suffix_result) {
        xpath_result_free(str_result);
        return NULL;
    }

    int ascii_ci = 0;
    if (arg_count == 3) {
        int mode = collation_mode(context, args[2]);
        if (mode < 0) {
            xpath_result_free(str_result);
            xpath_result_free(suffix_result);
            return NULL;
        }
        ascii_ci = mode == 1;
    }

    char* str = result_to_string(str_result);
    char* suffix = result_to_string(suffix_result);

    size_t sl = str ? strlen(str) : 0, fl = suffix ? strlen(suffix) : 0;
    int match = fl <= sl;
    if (match) {
        const char* a = (str ? str : "") + sl - fl;
        const char* b = suffix ? suffix : "";
        if (ascii_ci) {
            while (*a && *b &&
                   tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
                a++;
                b++;
            }
            match = *b == '\0';
        } else {
            match = memcmp(a, b, fl) == 0;
        }
    }

    LEPTRIS_FREE(str);
    LEPTRIS_FREE(suffix);
    xpath_result_free(str_result);
    xpath_result_free(suffix_result);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) return NULL;
    result->value.boolean_value = match;

    return result;
}

/* ---- fn:deep-equal (XPath 2.0, #684) ----
 * Sequences are equal at equal length with pairwise-equal items:
 * nodes compare by kind, name, unordered attribute set and deep
 * content; atomic items compare numerically for numbers, lexically
 * for strings/booleans; kind mismatches are false (not errors). */

/* Unordered attribute-set comparison: every attribute of A must
 * exist in B with the same value (lookup by NUL-terminated name
 * snapshot — attribute counts match before we get here). */
static int de_attrs_equal(LeptrisElement a, LeptrisElement b) {
    size_t ca = leptris_element_attribute_count(a);
    if (ca != leptris_element_attribute_count(b)) return 0;
    for (size_t i = 0; i < ca; i++) {
        struct leptris_attribute* aa =
            leptris_element_get_attribute_by_index(a, (uint8_t)i);
        if (!aa) return 0;
        LeptrisStringView nv = leptris_attribute_name_view(aa);
        char nbuf[256];
        size_t nl = nv.length < sizeof(nbuf) - 1
                        ? nv.length : sizeof(nbuf) - 1;
        memcpy(nbuf, nv.data, nl);
        nbuf[nl] = 0;
        const char* bval = leptris_element_attribute(b, nbuf);
        if (!bval) return 0;
        LeptrisStringView av = leptris_attribute_value_view(aa);
        size_t bl = strlen(bval);
        if (bl != av.length ||
            memcmp(av.data, bval, av.length) != 0)
            return 0;
    }
    return 1;
}

static int de_node_equal(LeptrisNodeRef a, LeptrisNodeRef b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    int ta = leptris_node_get_type(a);
    int tb = leptris_node_get_type(b);
    if (ta != tb) return 0;
    switch (ta) {
        case LEPTRIS_NODE_TYPE_ELEMENT: {
            const char* na = leptris_element_get_name((LeptrisElement)a);
            const char* nb = leptris_element_get_name((LeptrisElement)b);
            if (!na || !nb || strcmp(na, nb) != 0) return 0;
            if (!de_attrs_equal((LeptrisElement)a, (LeptrisElement)b))
                return 0;
            break;
        }
        case LEPTRIS_NODE_TYPE_TEXT:
            return strcmp(leptris_text_get_content((LeptrisTextNode*)a),
                          leptris_text_get_content((LeptrisTextNode*)b)) == 0;
        case LEPTRIS_NODE_TYPE_CDATA:
            return strcmp(((LeptrisCDATANode*)a)->content,
                          ((LeptrisCDATANode*)b)->content) == 0;
        case LEPTRIS_NODE_TYPE_COMMENT:
            return strcmp(
                       leptris_comment_get_content((LeptrisCommentNode*)a),
                       leptris_comment_get_content((LeptrisCommentNode*)b)) == 0;
        case LEPTRIS_NODE_TYPE_PI:
            return strcmp(leptris_pi_get_target((LeptrisPINode*)a),
                          leptris_pi_get_target((LeptrisPINode*)b)) == 0 &&
                   strcmp(leptris_pi_get_data((LeptrisPINode*)a),
                          leptris_pi_get_data((LeptrisPINode*)b)) == 0;
        default:
            return 0;
    }
    /* Element children: pairwise, in order. F&O 3.0: comment and
     * processing-instruction children are not significant to
     * deep-equality (cbcl-deep-equal-001: PI vs comment under the
     * same element compares equal). */
    LeptrisNodeRef ca = leptris_node_first_child(a);
    LeptrisNodeRef cb = leptris_node_first_child(b);
    while (ca && cb) {
        int tca = leptris_node_get_type(ca);
        int tcb = leptris_node_get_type(cb);
        if (tca == LEPTRIS_NODE_TYPE_COMMENT ||
            tca == LEPTRIS_NODE_TYPE_PI) {
            ca = leptris_node_next_sibling(ca);
            continue;
        }
        if (tcb == LEPTRIS_NODE_TYPE_COMMENT ||
            tcb == LEPTRIS_NODE_TYPE_PI) {
            cb = leptris_node_next_sibling(cb);
            continue;
        }
        if (!de_node_equal(ca, cb)) return 0;
        ca = leptris_node_next_sibling(ca);
        cb = leptris_node_next_sibling(cb);
    }
    while (ca && (leptris_node_get_type(ca) == LEPTRIS_NODE_TYPE_COMMENT ||
                  leptris_node_get_type(ca) == LEPTRIS_NODE_TYPE_PI))
        ca = leptris_node_next_sibling(ca);
    while (cb && (leptris_node_get_type(cb) == LEPTRIS_NODE_TYPE_COMMENT ||
                  leptris_node_get_type(cb) == LEPTRIS_NODE_TYPE_PI))
        cb = leptris_node_next_sibling(cb);
    return ca == NULL && cb == NULL;
}

/* Direct-ctor strings compare as trees when the lexical forms
 * differ: attribute order is insignificant to deep-equality
 * (fn-deep-equal-maps-11), and the tree path also inherits the
 * comment/PI-insignificance rule. Unparseable markup compares
 * lexically (both sides then fail identically -> false). */
static int de_markup_equal_n(const char* a, size_t al,
                             const char* b, size_t bl) {
    char* xa = (char*)malloc(al + 1);
    char* xb = (char*)malloc(bl + 1);
    int eq = 0;
    if (xa && xb) {
        memcpy(xa, a, al); xa[al] = 0;
        memcpy(xb, b, bl); xb[bl] = 0;
        LeptrisStatus sa = LEPTRIS_OK, sb = LEPTRIS_OK;
        LeptrisDocument da = leptris_parse_string(xa, al, &sa);
        LeptrisDocument db = leptris_parse_string(xb, bl, &sb);
        if (da && db) {
            LeptrisElement ra = leptris_document_root(da);
            LeptrisElement rb = leptris_document_root(db);
            eq = ra && rb &&
                 de_node_equal((LeptrisNodeRef)ra, (LeptrisNodeRef)rb);
        }
        if (da) leptris_document_free(da);
        if (db) leptris_document_free(db);
    }
    free(xa);
    free(xb);
    return eq;
}

/* Map carriers ("\x03MAP" + ("\x02" key "\x01" value)*; entries are
 * canonically key-sorted at construction) compare entry-wise:
 * keys lexically, values with the markup fallback. Segment bytes
 * (\x02/\x01) cannot occur in XML content, so end-scan is safe. */
static int de_map_equal(const char* a, const char* b) {
    a += 4;
    b += 4;
    while (*a && *b) {
        if (*a != '\x02' || *b != '\x02') return 0;
        a++;
        b++;
        const char* ka = a;
        while (*a && *a != '\x01') a++;
        const char* kb = b;
        while (*b && *b != '\x01') b++;
        if (*a != '\x01' || *b != '\x01') return 0;
        if (a - ka != b - kb || memcmp(ka, kb, (size_t)(a - ka)) != 0)
            return 0;
        a++;
        b++;
        const char* va = a;
        while (*a && *a != '\x02') a++;
        const char* vb = b;
        while (*b && *b != '\x02') b++;
        if (a - va != b - vb ||
            memcmp(va, vb, (size_t)(a - va)) != 0) {
            if (va[0] != '<' || vb[0] != '<') return 0;
            if (!de_markup_equal_n(va, (size_t)(a - va),
                                   vb, (size_t)(b - vb)))
                return 0;
        }
    }
    return *a == 0 && *b == 0;
}

/* One comparable item: kind 0 = node, 1 = number, 2 = string,
 * 3 = boolean. Synthetic sequence members carry the "\x03N" numeric
 * marker (stripped here); other synthetic text is a string. */
typedef struct {
    int kind;
    double num;
    char* str;            /* owned for synthetic items */
    const char* borrow;   /* document-lifetime text */
    LeptrisNodeRef node;
    /* Typed-atom tag from the result (interned literal, not owned);
     * NULL for the untyped/string class. */
    const char* type;
} DeItem;

/* Numeric promotion rank: xs:decimal(1) < xs:float(2) <= the
 * double carrier (untyped numerics, xs:double, integers — integers
 * are int64-exact in the double carrier for eq purposes). */
static int de_num_rank(const char* t) {
    if (!t) return 3;
    if (strcmp(t, "xs:float") == 0) return 2;
    if (strcmp(t, "xs:decimal") == 0) return 1;
    return 3;
}

/* The untyped/string equivalence class: plain ctor/lexical strings,
 * xs:string, xs:anyURI, xs:untypedAtomic — and the types DERIVED
 * from xs:string, which all compare by codepoint
 * (K2-SeqDeepEqualFunc-35: "a" = xs:NCName("a")). */
static int de_str_class(const char* t) {
    if (!t) return 1;
    static const char* const k_derived[] = {
        "xs:string", "xs:anyURI", "xs:untypedAtomic", "xs:NCName",
        "xs:Name", "xs:token", "xs:normalizedString", "xs:language",
        "xs:NMTOKEN", "xs:ID", "xs:IDREF", "xs:ENTITY"};
    for (size_t i = 0; i < sizeof(k_derived) / sizeof(*k_derived); i++)
        if (strcmp(t, k_derived[i]) == 0) return 1;
    return 0;
}

static void de_item_clear(DeItem* it) {
    if (it->str) LEPTRIS_FREE(it->str);
    it->str = NULL;
}

static int de_item_equal(DeItem* a, DeItem* b) {
    if (a->kind != b->kind) return 0;
    switch (a->kind) {
        case 0: return de_node_equal(a->node, b->node);
        case 1: {
            /* deep-equal is atomic-eq based: NaN compares equal to
             * NaN (F&O 17.4.1 — unlike the eq operator). */
            if (isnan(a->num) && isnan(b->num)) return 1;
            /* Numeric promotion by rank: the lower side converts to
             * the higher (decimal->float rounds; float->double is
             * exact). xs:float rounds at construction, so the plain
             * compare against a double is the exact widening
             * (float(1.01) ne double(1.01)) while decimal vs float
             * rounds the decimal (mix-args-017 true). */
            int ra = de_num_rank(a->type);
            int rb = de_num_rank(b->type);
            double xa = a->num, xb = b->num;
            if (rb == 2 && ra < 2) xa = (double)(float)xa;
            else if (ra == 2 && rb < 2) xb = (double)(float)xb;
            return xa == xb;
        }
        case 2: {
            const char* as = a->str ? a->str : (a->borrow ? a->borrow : "");
            const char* bs = b->str ? b->str : (b->borrow ? b->borrow : "");
            if (de_str_class(a->type) && de_str_class(b->type)) {
                if (strncmp(as, "\x03MAP", 4) == 0 &&
                    strncmp(bs, "\x03MAP", 4) == 0)
                    return de_map_equal(as, bs);
                if (strcmp(as, bs) == 0) return 1;
                return as[0] == '<' && bs[0] == '<' &&
                       de_markup_equal_n(as, strlen(as), bs, strlen(bs));
            }
            if (!de_str_class(a->type) && !de_str_class(b->type) &&
                strcmp(a->type, b->type) == 0)
                return strcmp(as, bs) == 0;
            return 0;   /* typed (xs:date...) vs anything else */
        }
        case 4: {
            /* Attribute ctor carriers: name segment (after "\x03A",
             * before "\x01") and value segment must both match. */
            const char* as = a->str ? a->str : (a->borrow ? a->borrow : "");
            const char* bs = b->str ? b->str : (b->borrow ? b->borrow : "");
            const char* ase = strchr(as, '\x01');
            const char* bse = strchr(bs, '\x01');
            if (!ase || !bse) return 0;
            if ((size_t)(ase - as) != (size_t)(bse - bs) ||
                memcmp(as, bs, (size_t)(ase - as)) != 0)
                return 0;
            ase++;
            bse++;
            if (strcmp(ase, bse) == 0) return 1;
            return ase[0] == '<' && bse[0] == '<' &&
                   de_markup_equal_n(ase, strlen(ase), bse, strlen(bse));
        }
        default: return a->num == b->num;   /* booleans as 0/1 */
    }
}

/* Collect a result's items into out (cap-limited); returns count. */
static size_t de_collect(struct leptris_xpath_result* r, DeItem* out,
                         size_t cap) {
    if (r->type == XPATH_RESULT_NODESET && r->value.nodeset_value) {
        XPathNodeSet* ns = r->value.nodeset_value;
        size_t n = ns->count < cap ? ns->count : cap;
        for (size_t i = 0; i < n; i++) {
            out[i].str = NULL;
            out[i].borrow = NULL;
            out[i].node = NULL;
            out[i].type = NULL;
            int ty = XPATH_NODE_TYPE(ns->nodes[i]);
            if (ty == LEPTRIS_NODE_TEXT) {
                /* Synthetic sequence member (tag 8): content rides
                 * the XPathTextNode struct, numeric members carry
                 * the "\x03N" marker. */
                const char* c = ((XPathTextNode*)ns->nodes[i])->content;
                c = c ? c : "";
                if (c[0] == '\x03' && c[1] == 'B') {
                    out[i].kind = 3;
                    out[i].num = c[2] == 't' ? 1 : 0;
                } else if (c[0] == '\x03' && c[1] == 'A') {
                    /* attribute ctor carrier: kind 4 compares
                     * name + value (K2-SeqDeepEqualFunc-25/31/32) */
                    out[i].kind = 4;
                    out[i].borrow = c;
                } else if (c[0] == '\x03' &&
                    (c[1] == 'N' || c[1] == 'D' ||
                     (c[1] == 'F' &&
                      !((c[2] == 'N' && c[3] == '\x02') ||
                        c[2] == 'R')))) {
                    out[i].kind = 1;
                    out[i].type = c[1] == 'F' ? "xs:float"
                                : c[1] == 'D' ? "xs:decimal" : NULL;
                    out[i].num = strtod(c + 2, NULL);
                } else {
                    out[i].kind = 2;
                    out[i].borrow = c;
                }
            } else if (ty == LEPTRIS_NODE_TYPE_TEXT) {
                out[i].kind = 2;
                out[i].borrow = leptris_text_get_content(
                    (LeptrisTextNode*)ns->nodes[i]);
            } else {
                out[i].kind = 0;
                out[i].node = ns->nodes[i];
            }
        }
        return ns->count;
    }
    out[0].str = NULL;
    out[0].borrow = NULL;
    out[0].node = NULL;
    out[0].type = r->atomic_type;
    if (r->type == XPATH_RESULT_NUMBER) {
        out[0].kind = 1;
        out[0].num = r->value.number_value;
    } else if (r->type == XPATH_RESULT_BOOLEAN) {
        out[0].kind = 3;
        out[0].num = r->value.boolean_value ? 1 : 0;
    } else {
        out[0].kind = 2;
        out[0].str = result_to_string(r);
    }
    return 1;
}

/* deep-equal(arg1, arg2) - deep structural/value comparison. */
static struct leptris_xpath_result* xpath_func_deep_equal(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count < 2 || arg_count > 3) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "deep-equal() requires 2 or 3 arguments, got %zu", arg_count);
        return NULL;
    }
    if (arg_count == 3) {
        /* Collation argument: the default and codepoint URIs (and,
         * per the QT3 corpus, any URI here) fold to the codepoint
         * comparison this engine implements; evaluation errors
         * propagate. */
        struct leptris_xpath_result* c =
            xpath_evaluate(context, args[2]);
        if (!c) return NULL;
        leptris_xpath_result_free(c);
    }

    struct leptris_xpath_result* a = xpath_evaluate(context, args[0]);
    if (!a) return NULL;
    struct leptris_xpath_result* b = xpath_evaluate(context, args[1]);
    if (!b) {
        xpath_result_free(a);
        return NULL;
    }

    /* Growable item arrays: corpus sequences run to tens of
     * thousands of members (cbcl roundtrips over 65536 to 100000);
     * a fixed cap silently compares a prefix. */
    size_t capa = a->type == XPATH_RESULT_NODESET && a->value.nodeset_value
        ? a->value.nodeset_value->count : 1;
    size_t capb = b->type == XPATH_RESULT_NODESET && b->value.nodeset_value
        ? b->value.nodeset_value->count : 1;
    DeItem* ia = (DeItem*)malloc((capa ? capa : 1) * sizeof(DeItem));
    DeItem* ib = (DeItem*)malloc((capb ? capb : 1) * sizeof(DeItem));
    if (!ia || !ib) {
        free(ia);
        free(ib);
        xpath_result_free(a);
        xpath_result_free(b);
        return NULL;
    }
    size_t na = de_collect(a, ia, capa);
    size_t nb = de_collect(b, ib, capb);

    int equal = na == nb;
    for (size_t i = 0; equal && i < na; i++)
        equal = de_item_equal(&ia[i], &ib[i]);

    for (size_t i = 0; i < na; i++) de_item_clear(&ia[i]);
    for (size_t i = 0; i < nb; i++) de_item_clear(&ib[i]);
    free(ia);
    free(ib);
    xpath_result_free(a);
    xpath_result_free(b);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) return NULL;
    result->value.boolean_value = equal;
    return result;
}

/* ASCII-only case-insensitive substring search — the
 * html-ascii-case-insensitive collation folds ASCII letters and
 * nothing else (WHATWG ASCII case-insensitive matching; folding
 * never changes byte length, so byte-level scans are exact). */
static int ascii_ci_contains(const char* hay, const char* needle) {
    if (!*needle) return 1;
    for (; *hay; hay++) {
        const char* a = hay;
        const char* b = needle;
        while (*a && *b &&
               tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

/* contains(string, string[, collation]) - Check if string contains
 * substring. The 3-arg form carries a collation URI: codepoint is
 * the default semantics; html-ascii-case-insensitive folds ASCII. */
static struct leptris_xpath_result* xpath_func_contains(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 2 && arg_count != 3) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "contains() requires 2 or 3 arguments, got %zu", arg_count);
        return NULL;
    }

    struct leptris_xpath_result* str_result = xpath_evaluate(context, args[0]);
    if (!str_result) return NULL;

    struct leptris_xpath_result* substr_result = xpath_evaluate(context, args[1]);
    if (!substr_result) {
        xpath_result_free(str_result);
        return NULL;
    }

    int ascii_ci = 0;
    if (arg_count == 3) {
        int mode = collation_mode(context, args[2]);
        if (mode < 0) {
            xpath_result_free(str_result);
            xpath_result_free(substr_result);
            return NULL;
        }
        ascii_ci = mode == 1;
    }

    char* str = result_to_string(str_result);
    char* substr = result_to_string(substr_result);

    int match = ascii_ci ? ascii_ci_contains(str ? str : "",
                                              substr ? substr : "")
                         : (strstr(str ? str : "",
                                   substr ? substr : "") != NULL);

    LEPTRIS_FREE(str);
    LEPTRIS_FREE(substr);
    xpath_result_free(str_result);
    xpath_result_free(substr_result);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) return NULL;
    result->value.boolean_value = match;

    return result;
}

/* ============================================================================
 * XPath String Functions (substring, substring-before, substring-after, string-length, normalize-space, translate)
 * ============================================================================ */

/**
 * substring(string, start) -> string
 * substring(string, start, length) -> string
 * Returns the substring starting at position (1-indexed)
 */
static struct leptris_xpath_result* xpath_func_substring(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count < 2 || arg_count > 3) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "substring() requires 2 or 3 arguments");
        return NULL;
    }

    /* Evaluate string argument */
    struct leptris_xpath_result* str_result = xpath_evaluate(context, args[0]);
    if (!str_result) return NULL;
    char* str = result_to_string(str_result);
    xpath_result_free(str_result);
    if (!str) return NULL;

    size_t len = strlen(str);

    /* Evaluate start position argument */
    struct leptris_xpath_result* start_result = xpath_evaluate(context, args[1]);
    if (!start_result) {
        LEPTRIS_FREE(str);
        return NULL;
    }
    double start_double = result_to_number(start_result);
    xpath_result_free(start_result);

    /* XPath 1.0 substring() uses IEEE-754 comparisons with round() of
     * arguments. Per spec section 4.2: returned chars are those whose
     * 1-indexed position p satisfies
     *     round(start) <= p < round(start) + round(length)
     * (length omitted means +Inf, so the upper bound is +Inf.)
     * NaN start or length returns the empty string. */

    if (isnan(start_double)) {
        LEPTRIS_FREE(str);
        struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
        if (result) result->value.string_value = leptris_strdup("");
        return result;
    }

    /* round() per XPath spec: half rounds toward +Inf. */
    double start_rounded = xpath_round_half_up(start_double);

    double end_rounded;
    if (arg_count == 2) {
        end_rounded = INFINITY;  /* substring(str, start) = start..end */
    } else {
        struct leptris_xpath_result* len_result = xpath_evaluate(context, args[2]);
        if (!len_result) {
            LEPTRIS_FREE(str);
            return NULL;
        }
        double len_double = result_to_number(len_result);
        xpath_result_free(len_result);
        if (isnan(len_double)) {
            LEPTRIS_FREE(str);
            struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
            if (result) result->value.string_value = leptris_strdup("");
            return result;
        }
        end_rounded = start_rounded + xpath_round_half_up(len_double);
    }

    /* Walk CODEPOINT positions (XQuery positions are codepoint
     * indexes — a 4-byte astral char is ONE position; QT3's
     * non-BMP cases). ASCII fast path for the common case. */
    size_t out_len = 0;
    char* result_str = LEPTRIS_ALLOC_N(char, len + 1);
    if (!result_str) {
        LEPTRIS_FREE(str);
        return NULL;
    }
    int ascii_only = 1;
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)str[i] & 0x80) { ascii_only = 0; break; }
    }
    if (ascii_only) {
        for (size_t i = 0; i < len; i++) {
            double p = (double)(i + 1);
            if (p >= start_rounded && p < end_rounded)
                result_str[out_len++] = str[i];
        }
    } else {
        size_t i = 0;
        double p = 1.0;
        while (i < len) {
            size_t cp_len = 1;
            unsigned char b = (unsigned char)str[i];
            if ((b & 0x80) == 0) cp_len = 1;
            else if ((b & 0xE0) == 0xC0) cp_len = 2;
            else if ((b & 0xF0) == 0xE0) cp_len = 3;
            else if ((b & 0xF8) == 0xF0) cp_len = 4;
            if (i + cp_len > len) cp_len = 1;  /* truncated: byte */
            if (p >= start_rounded && p < end_rounded) {
                memcpy(result_str + out_len, str + i, cp_len);
                out_len += cp_len;
            }
            i += cp_len;
            p += 1.0;
        }
    }
    result_str[out_len] = '\0';
    LEPTRIS_FREE(str);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        LEPTRIS_FREE(result_str);
        return NULL;
    }
    result->value.string_value = result_str;
    return result;
}

/**
 * substring-before(string, pattern) -> string
 * Returns the substring before the first occurrence of pattern
 */
static struct leptris_xpath_result* xpath_func_substring_before(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    /* 3-arg (collation) overload: only the codepoint-collation URI
     * (and its UTF-8 sibling) is supported — under it the collation
     * is exactly the default byte comparison, so args[2] is
     * validated and dropped (QT3 fn-substring-before/-after). */
    if (arg_count == 3) {
        struct leptris_xpath_result* coll = xpath_evaluate(context, args[2]);
        if (!coll) return NULL;
        char* uri = coll->type == XPATH_RESULT_STRING
                        ? coll->value.string_value : NULL;
        static const char* kCP =
            "http://www.w3.org/2005/xpath-functions/collation/"
            "codepoint";
        int ok = uri && (strcmp(uri, kCP) == 0 || strcmp(uri, "") == 0);
        leptris_xpath_result_free(coll);
        if (!ok) {
            snprintf(context->error_msg, sizeof(context->error_msg),
                     "substring-before(): unsupported collation");
            return NULL;
        }
        arg_count = 2;
    }
    if (arg_count != 2) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "substring-before() requires exactly 2 or 3 arguments");
        return NULL;
    }

    /* Evaluate string argument */
    struct leptris_xpath_result* str_result = xpath_evaluate(context, args[0]);
    if (!str_result) return NULL;
    char* str = result_to_string(str_result);
    xpath_result_free(str_result);
    if (!str) return NULL;

    /* Evaluate pattern argument */
    struct leptris_xpath_result* pattern_result = xpath_evaluate(context, args[1]);
    if (!pattern_result) {
        LEPTRIS_FREE(str);
        return NULL;
    }
    char* pattern = result_to_string(pattern_result);
    xpath_result_free(pattern_result);

    if (!pattern) {
        LEPTRIS_FREE(str);
        return NULL;
    }

    char* result_str;
    if (pattern[0] == '\0') {
        /* Empty pattern returns empty string */
        result_str = leptris_strdup("");
    } else {
        char* pos = strstr(str, pattern);
        if (!pos) {
            result_str = leptris_strdup("");
        } else {
            size_t prefix_len = (size_t)(pos - str);
            result_str = LEPTRIS_ALLOC_N(char, prefix_len + 1);
            if (result_str) {
                memcpy(result_str, str, prefix_len);
                result_str[prefix_len] = '\0';
            }
        }
    }

    LEPTRIS_FREE(str);
    LEPTRIS_FREE(pattern);

    if (!result_str) return NULL;

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        LEPTRIS_FREE(result_str);
        return NULL;
    }
    result->value.string_value = result_str;
    return result;
}

/**
 * substring-after(string, pattern) -> string
 * Returns the substring after the first occurrence of pattern
 */
static struct leptris_xpath_result* xpath_func_substring_after(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    /* 3-arg (collation) overload: only the codepoint-collation URI
     * (and its UTF-8 sibling) is supported — under it the collation
     * is exactly the default byte comparison, so args[2] is
     * validated and dropped (QT3 fn-substring-before/-after). */
    if (arg_count == 3) {
        struct leptris_xpath_result* coll = xpath_evaluate(context, args[2]);
        if (!coll) return NULL;
        char* uri = coll->type == XPATH_RESULT_STRING
                        ? coll->value.string_value : NULL;
        static const char* kCP =
            "http://www.w3.org/2005/xpath-functions/collation/"
            "codepoint";
        int ok = uri && (strcmp(uri, kCP) == 0 || strcmp(uri, "") == 0);
        leptris_xpath_result_free(coll);
        if (!ok) {
            snprintf(context->error_msg, sizeof(context->error_msg),
                     "substring-after(): unsupported collation");
            return NULL;
        }
        arg_count = 2;
    }
    if (arg_count != 2) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "substring-after() requires exactly 2 or 3 arguments");
        return NULL;
    }

    /* Evaluate string argument */
    struct leptris_xpath_result* str_result = xpath_evaluate(context, args[0]);
    if (!str_result) return NULL;
    char* str = result_to_string(str_result);
    xpath_result_free(str_result);
    if (!str) return NULL;

    /* Evaluate pattern argument */
    struct leptris_xpath_result* pattern_result = xpath_evaluate(context, args[1]);
    if (!pattern_result) {
        LEPTRIS_FREE(str);
        return NULL;
    }
    char* pattern = result_to_string(pattern_result);
    xpath_result_free(pattern_result);

    if (!pattern) {
        LEPTRIS_FREE(str);
        return NULL;
    }

    /* XPath spec: substring-after with empty pattern returns empty string */
    char* result_str;
    if (pattern[0] == '\0') {
        /* Empty pattern returns empty string for substring-after */
        /* Actually per XPath spec: substring-after('', '') = '' and substring-after('test', '') = 'test' */
        result_str = leptris_strdup(str);  /* Return the entire string for empty pattern */
    } else {
        char* pos = strstr(str, pattern);
        if (!pos) {
            result_str = leptris_strdup("");
        } else {
            char* after = pos + strlen(pattern);
            size_t suffix_len = strlen(after);
            result_str = LEPTRIS_ALLOC_N(char, suffix_len + 1);
            if (result_str) {
                memcpy(result_str, after, suffix_len);
                result_str[suffix_len] = '\0';
            }
        }
    }

    LEPTRIS_FREE(str);
    LEPTRIS_FREE(pattern);

    if (!result_str) return NULL;

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        LEPTRIS_FREE(result_str);
        return NULL;
    }
    result->value.string_value = result_str;
    return result;
}

/**
 * string-length(string) -> number
 * Returns the length of the string
 */
static struct leptris_xpath_result* xpath_func_string_length(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count > 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "string-length() requires 0 or 1 argument");
        return NULL;
    }

    char* str = NULL;

    if (arg_count == 1) {
        /* Evaluate string argument */
        struct leptris_xpath_result* str_result = xpath_evaluate(context, args[0]);
        if (!str_result) return NULL;
        str = result_to_string(str_result);
        xpath_result_free(str_result);
    } else {
        /* No argument - use context node's string value */
        struct leptris_xpath_result* str_result = xpath_func_string(context, NULL, 0);
        if (!str_result) return NULL;
        str = result_to_string(str_result);
        xpath_result_free(str_result);
    }

    /* Codepoint count, not byte count (QT3 fn-string-length-20:
     * an astral char is one character, not four bytes). The table
     * holds the CONTINUATION-byte count (len-1): the for-loop's
     * own p++ adds the lead byte's step, so a 4-byte lead stores 3.
     * (Storing the full length double-advanced and walked past the
     * NUL terminator — CI-only failure, the byte past the NUL was
     * zero locally.) */
    size_t len = 0;
    if (str) {
        static const unsigned char sl_u8cont[256] = {
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
            2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,0,0,0,0,0,0,0,0
        };
        for (const unsigned char* p = (const unsigned char*)str; *p; p++) {
            len++;
            p += sl_u8cont[*p];
        }
    }
    LEPTRIS_FREE(str);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;
    result->value.number_value = (double)len;
    return result;
}

/**
 * normalize-space(string?) -> string
 * Returns the input string with whitespace normalized
 */
static struct leptris_xpath_result* xpath_func_normalize_space(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count > 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "normalize-space() requires 0 or 1 argument");
        return NULL;
    }

    char* str = NULL;

    if (arg_count == 1) {
        /* Evaluate string argument */
        struct leptris_xpath_result* str_result = xpath_evaluate(context, args[0]);
        if (!str_result) return NULL;
        str = result_to_string(str_result);
        xpath_result_free(str_result);
    } else {
        /* No argument - use context node's string value */
        struct leptris_xpath_result* str_result = xpath_func_string(context, NULL, 0);
        if (!str_result) return NULL;
        str = result_to_string(str_result);
        xpath_result_free(str_result);
    }

    if (!str) {
        struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
        if (result) result->value.string_value = leptris_strdup("");
        return result;
    }

    /* Normalize whitespace: trim leading/trailing, collapse internal whitespace */
    char* src = str;
    char* dst = str;
    int in_whitespace = 0;

    /* Skip leading whitespace */
    while (isspace((unsigned char)*src)) src++;

    while (*src) {
        if (isspace((unsigned char)*src)) {
            if (!in_whitespace) {
                /* Start of whitespace sequence - add single space */
                *dst++ = ' ';
                in_whitespace = 1;
            }
            /* Skip remaining whitespace */
        } else {
            *dst++ = *src;
            in_whitespace = 0;
        }
        src++;
    }

    /* Remove trailing space if present */
    if (dst > str && dst[-1] == ' ') {
        dst--;
    }
    *dst = '\0';

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        LEPTRIS_FREE(str);
        return NULL;
    }

    /* str may have been modified in place, but we need to keep it valid */
    result->value.string_value = leptris_strdup(str);
    LEPTRIS_FREE(str);

    return result;
}

/**
 * translate(string, from, to) -> string
 * Returns string with characters from replaced by corresponding characters in to
 */
static struct leptris_xpath_result* xpath_func_translate(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 3) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "translate() requires exactly 3 arguments");
        return NULL;
    }

    /* Evaluate arguments */
    struct leptris_xpath_result* str_result = xpath_evaluate(context, args[0]);
    if (!str_result) return NULL;
    char* str = result_to_string(str_result);
    xpath_result_free(str_result);
    if (!str) return NULL;

    struct leptris_xpath_result* from_result = xpath_evaluate(context, args[1]);
    if (!from_result) {
        LEPTRIS_FREE(str);
        return NULL;
    }
    char* from = result_to_string(from_result);
    xpath_result_free(from_result);

    struct leptris_xpath_result* to_result = xpath_evaluate(context, args[2]);
    if (!to_result) {
        LEPTRIS_FREE(str);
        LEPTRIS_FREE(from);
        return NULL;
    }
    char* to = result_to_string(to_result);
    xpath_result_free(to_result);

    if (!from || !to) {
        LEPTRIS_FREE(str);
        LEPTRIS_FREE(from);
        LEPTRIS_FREE(to);
        return NULL;
    }

    /* Codepoint-correct translate (QT3 fn-translate): UTF-8 strings
     * are decoded into codepoints — a byte-level table mangles any
     * multi-byte character (each of its bytes matched separately,
     * and a 256-entry map cannot hold astral or most Latin-1+ chars
     * at all). Map entries store the REPLACEMENT BYTES so a
     * codepoint can translate to a different-width codepoint. */
    size_t from_len = strlen(from);
    size_t to_len = strlen(to);

    /* UTF-8 lead-byte -> sequence length (0 = continuation/invalid). */
    static const unsigned char u8len[256] = {
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3, 4,4,4,4,4,4,4,4,0,0,0,0,0,0,0,0
    };

    typedef struct { unsigned long cp; const char* repl; size_t repl_len; } tr_map_ent;
    tr_map_ent* map = (tr_map_ent*)malloc((from_len ? from_len : 1) * sizeof(*map));
    if (!map) {
        LEPTRIS_FREE(str);
        LEPTRIS_FREE(from);
        LEPTRIS_FREE(to);
        return NULL;
    }
    size_t map_n = 0;

    /* Decode 'to' into its codepoint byte-spans (pointers INTO to). */
    const char** to_cp = (const char**)malloc((to_len + 1) * sizeof(*to_cp));
    size_t* to_cpl = (size_t*)malloc((to_len + 1) * sizeof(*to_cpl));
    size_t to_n = 0;
    if (!to_cp || !to_cpl) {
        LEPTRIS_FREE(map);
        LEPTRIS_FREE(to_cp);
        LEPTRIS_FREE(to_cpl);
        LEPTRIS_FREE(str);
        LEPTRIS_FREE(from);
        LEPTRIS_FREE(to);
        return NULL;
    }
    for (size_t i = 0; i < to_len; ) {
        unsigned char l = u8len[(unsigned char)to[i]];
        if (!l) l = 1;
        to_cp[to_n] = to + i;
        to_cpl[to_n] = l;
        to_n++;
        i += l;
    }

    /* First occurrence in 'from' governs (F&O: duplicates later in
     * the map string are ignored). */
    size_t from_cp_idx = 0;   /* codepoint ordinal of from[i] */
    for (size_t i = 0; i < from_len; ) {
        unsigned char l = u8len[(unsigned char)from[i]];
        if (!l) l = 1;
        unsigned long cp = 0;
        const unsigned char* fp = (const unsigned char*)from + i;
        switch (l) {
            case 1: cp = fp[0]; break;
            case 2: cp = ((fp[0] & 0x1Fu) << 6) | (fp[1] & 0x3Fu); break;
            case 3: cp = ((fp[0] & 0x0Fu) << 12) | ((fp[1] & 0x3Fu) << 6)
                        | (fp[2] & 0x3Fu); break;
            default: cp = ((fp[0] & 0x07u) << 18) | ((fp[1] & 0x3Fu) << 12)
                        | ((fp[2] & 0x3Fu) << 6) | (fp[3] & 0x3Fu); break;
        }
        int seen = 0;
        for (size_t k = 0; k < map_n; k++) {
            if (map[k].cp == cp) { seen = 1; break; }
        }
        if (!seen) {
            size_t idx = from_cp_idx;
            if (idx < to_n) {
                map[map_n].cp = cp;
                map[map_n].repl = to_cp[idx];
                map[map_n].repl_len = to_cpl[idx];
            } else {
                map[map_n].cp = cp;
                map[map_n].repl = NULL;   /* delete */
                map[map_n].repl_len = 0;
            }
            map_n++;
        }
        from_cp_idx++;
        i += l;
    }

    /* Translate: walk str by codepoint, emit into a fresh buffer
     * (replacements can change byte width, so in-place shifting is
     * not possible). Worst case: every codepoint maps to a 4-byte
     * replacement. */
    size_t str_len = strlen(str);
    size_t cap = str_len * 4 + 4;
    char* out = (char*)malloc(cap);
    if (!out) {
        LEPTRIS_FREE(map);
        LEPTRIS_FREE(to_cp);
        LEPTRIS_FREE(to_cpl);
        LEPTRIS_FREE(str);
        LEPTRIS_FREE(from);
        LEPTRIS_FREE(to);
        return NULL;
    }
    size_t w = 0;
    for (size_t i = 0; i < str_len; ) {
        unsigned char l = u8len[(unsigned char)str[i]];
        if (!l) l = 1;
        const unsigned char* sp = (const unsigned char*)str + i;
        unsigned long cp = 0;
        switch (l) {
            case 1: cp = sp[0]; break;
            case 2: cp = ((sp[0] & 0x1Fu) << 6) | (sp[1] & 0x3Fu); break;
            case 3: cp = ((sp[0] & 0x0Fu) << 12) | ((sp[1] & 0x3Fu) << 6)
                        | (sp[2] & 0x3Fu); break;
            default: cp = ((sp[0] & 0x07u) << 18) | ((sp[1] & 0x3Fu) << 12)
                        | ((sp[2] & 0x3Fu) << 6) | (sp[3] & 0x3Fu); break;
        }
        int found = 0;
        const char* repl = NULL;
        size_t repl_len = 0;
        for (size_t k = 0; k < map_n; k++) {
            if (map[k].cp == cp) {
                found = 1;
                repl = map[k].repl;
                repl_len = map[k].repl_len;
                break;
            }
        }
        if (found && !repl) {
            i += l;
            continue;   /* mapped to nothing: delete */
        }
        const char* emit = found ? repl : str + i;
        size_t emit_len = found ? repl_len : l;
        if (w + emit_len + 1 > cap) {
            cap = (w + emit_len + 1) * 2;
            char* grown = (char*)realloc(out, cap);
            if (!grown) { free(out); out = NULL; break; }
            out = grown;
        }
        memcpy(out + w, emit, emit_len);
        w += emit_len;
        i += l;
    }

    LEPTRIS_FREE(map);
    LEPTRIS_FREE(to_cp);
    LEPTRIS_FREE(to_cpl);
    LEPTRIS_FREE(from);
    LEPTRIS_FREE(to);

    if (!out) {
        LEPTRIS_FREE(str);
        return NULL;
    }
    out[w] = '\0';
    LEPTRIS_FREE(str);
    str = out;

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        LEPTRIS_FREE(str);
        return NULL;
    }
    result->value.string_value = str;
    return result;
}

/* ============================================================================
 * XPath Boolean Functions
 * ============================================================================ */

/* boolean(object) - Convert any object to boolean */
static struct leptris_xpath_result* xpath_func_boolean(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "boolean() requires exactly 1 argument");
        return NULL;
    }

    /* Evaluate argument */
    struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    /* Convert to boolean */
    int bool_value = result_to_boolean(arg_result);
    xpath_result_free(arg_result);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) return NULL;
    result->value.boolean_value = bool_value;

    return result;
}

/* not(boolean) - Logical negation */
static struct leptris_xpath_result* xpath_func_not(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "not() requires exactly 1 argument");
        return NULL;
    }

    /* Evaluate argument */
    struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    /* Convert to boolean and negate */
    int bool_value = result_to_boolean(arg_result);
    xpath_result_free(arg_result);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) return NULL;
    result->value.boolean_value = !bool_value;

    return result;
}

/* true() - Returns boolean true */
static struct leptris_xpath_result* xpath_func_true(XPathContext* context,
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

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) return NULL;
    result->value.boolean_value = 1;

    return result;
}

/* false() - Returns boolean false */
static struct leptris_xpath_result* xpath_func_false(XPathContext* context,
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

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) return NULL;
    result->value.boolean_value = 0;

    return result;
}

/* ============================================================================
 * XPath Number Functions
 * ============================================================================ */

/* number(object?) - Convert argument to number */
static struct leptris_xpath_result* xpath_func_number(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count > 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "number() takes 0 or 1 argument");
        return NULL;
    }

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;

    if (arg_count == 0) {
        /* No argument: convert context node to number */
        char* str = get_element_text(context->context_node);
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
        LEPTRIS_FREE(str);
    } else {
        /* Evaluate argument and convert to number */
        struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[0]);
        if (!arg_result) {
            xpath_result_free(result);
            return NULL;
        }
        result->value.number_value = result_to_number(arg_result);
        xpath_result_free(arg_result);
    }
    return result;
}

/* sum(node-set) - Sum the numeric values of all nodes */
static struct leptris_xpath_result* xpath_func_sum(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "sum() requires exactly 1 argument");
        return NULL;
    }

    struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[0]);
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
            LEPTRIS_FREE(str);
        }
    }
    xpath_result_free(arg_result);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;

    if (has_nan) {
        result->value.number_value = NAN;
    } else {
        result->value.number_value = sum;
    }
    return result;
}

/* floor(number) - Largest integer not greater than argument */
static struct leptris_xpath_result* xpath_func_floor(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "floor() requires exactly 1 argument");
        return NULL;
    }

    struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    double num = result_to_number(arg_result);
    xpath_result_free(arg_result);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;
    result->value.number_value = floor(num);
    return result;
}

/* ceiling(number) - Smallest integer not less than argument */
static struct leptris_xpath_result* xpath_func_ceiling(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "ceiling() requires exactly 1 argument");
        return NULL;
    }

    struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    double num = result_to_number(arg_result);
    xpath_result_free(arg_result);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;
    result->value.number_value = ceil(num);
    return result;
}

/* round(number) - Round to nearest integer; ties toward +Inf (XPath 1.0 spec 4.4). */
static struct leptris_xpath_result* xpath_func_round(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count < 1 || arg_count > 2) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "round() requires 1 or 2 arguments");
        return NULL;
    }

    struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    double num = result_to_number(arg_result);
    xpath_result_free(arg_result);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;

    /* XPath round() rounds half toward +Inf (spec 4.4). The 2-arg
     * form (2.0) scales by 10^precision first — negative precision
     * rounds to tens, hundreds, ... */
    double scale = 1.0;
    if (arg_count == 2) {
        struct leptris_xpath_result* p = xpath_evaluate(context, args[1]);
        if (!p) { xpath_result_free(result); return NULL; }
        double pd = result_to_number(p);
        xpath_result_free(p);
        if (!(pd >= -100 && pd <= 100)) {
            snprintf(context->error_msg, sizeof(context->error_msg),
                     "round(): precision out of range");
            xpath_result_free(result);
            return NULL;
        }
        scale = pow(10.0, pd);
    }
    result->value.number_value = xpath_round_half_up(num * scale) / scale;
    return result;
}

/* ============================================================================
 * XPath Node-set Functions
 * ============================================================================ */

/* XPath §4.1: an attribute identifies its element when the DTD
 * declares it ID-typed (any QName — bug-163's myns:id) or it is the
 * conventional unprefixed "id" (xml:id-style documents without a
 * DTD still resolve). */
static int attr_is_id_typed(struct leptris_document* doc,
                            LeptrisElement elem, const char* attr_name) {
    if (strcmp(attr_name, "id") == 0 || strcmp(attr_name, "xml:id") == 0)
        return 1;
    if (!doc || !doc->dtd) return 0;
    DTDAttributeDecl* ad = ttdtd_lookup_attribute(
        (const LeptrisDTD*)doc->dtd,
        leptris_element_get_name(elem), attr_name);
    return ad && ad->attr_type && strcmp(ad->attr_type, "ID") == 0;
}

/* Helper to recursively find elements by id */
static void find_elements_by_id(struct leptris_document* doc,
    LeptrisElement node, const char* id,
    XPathNodeSet* result) {
    if (!node) return;

    /* Check every attribute — an ID may live under any QName the
     * DTD declares ID-typed. */
    size_t na = leptris_element_attribute_count(node);
    for (size_t i = 0; i < na; i++) {
        const char* an = leptris_element_attribute_name_at(node, i);
        const char* av = leptris_element_attribute_value_at(node, i);
        if (an && av && strcmp(av, id) == 0 &&
            attr_is_id_typed(doc, node, an)) {
            xpath_nodeset_add(result, node);
            break;
        }
    }

    /* Recursively check children using compact accessor functions */
    LeptrisElement child_elem = leptris_element_get_first_child(node);
    while (child_elem) {
        /* Only recurse into element nodes */
        find_elements_by_id(doc, child_elem, id, result);
        child_elem = leptris_element_get_next_sibling(child_elem);
    }
}

/* ============================================================================
 * Node-Set Functions (local-name, namespace-uri, name, id)
 * ============================================================================ */

/**
 * Get local name of a node (without namespace prefix)
 * XPath: local-name(nodeset?)
 */
static struct leptris_xpath_result* xpath_func_local_name(
    XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    (void)context;
    void* node = NULL;
    char* synth_name = NULL;  /* owned copy from a synthetic node */
    int node_tag = -1;        /* captured before the arg result is freed */

    /* Get context node if no argument */
    if (arg_count == 0) {
        node = context->context_node;
        if (node) {
            node_tag = (int)XPATH_NODE_TYPE(node);
            if (node_tag == LEPTRIS_NODE_ATTRIBUTE) {
                const char* n = ((LeptrisAttributeNode*)node)->name;
                synth_name = n ? leptris_strdup(n) : NULL;
            } else if (node_tag == LEPTRIS_NODE_NAMESPACE) {
                const char* n = ((LeptrisNamespaceNode*)node)->prefix;
                synth_name = n ? leptris_strdup(n) : NULL;
            } else if (node_tag == LEPTRIS_NODE_TYPE_PI) {
                const char* t = leptris_pi_get_target((LeptrisPINode*)node);
                synth_name = t ? leptris_strdup(t) : NULL;
            }
        }
    } else if (arg_count == 1) {
        /* Use first node from nodeset */
        struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[0]);
        if (!arg_result) return NULL;

        if (arg_result->type == XPATH_RESULT_NODESET) {
            XPathNodeSet* nodeset = arg_result->value.nodeset_value;
            if (nodeset && xpath_nodeset_count(nodeset) > 0) {
                node = xpath_nodeset_get(nodeset, 0);
                /* Synthetic attribute/namespace nodes are owned by
                 * arg_result and die at the free below — copy the
                 * string out while the node is still alive. */
                if (node) {
                    int tag = (int)XPATH_NODE_TYPE(node);
                    node_tag = tag;
                    if (tag == LEPTRIS_NODE_ATTRIBUTE) {
                        const char* n = ((LeptrisAttributeNode*)node)->name;
                        synth_name = n ? leptris_strdup(n) : NULL;
                    } else if (tag == LEPTRIS_NODE_NAMESPACE) {
                        const char* n = ((LeptrisNamespaceNode*)node)->prefix;
                        synth_name = n ? leptris_strdup(n) : NULL;
                    }
                }
            }
        }
        xpath_result_free(arg_result);
    } else {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "local-name() requires 0 or 1 argument");
        return NULL;
    }

    if (!node) {
        /* Return empty string for empty nodeset or no node */
        struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
        if (!result) return NULL;
        result->value.string_value = leptris_strdup("");
        return result;
    }

    /* Get local name (everything after last colon, or full name if no colon) */
    const char* full_name = NULL;
    if (node_tag == LEPTRIS_NODE_ELEMENT) {
        full_name = leptris_element_get_name((LeptrisElement)node);
    } else if (synth_name) {
        full_name = synth_name;
    }

    if (!full_name) {
        LEPTRIS_FREE(synth_name);
        struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
        if (!result) return NULL;
        result->value.string_value = leptris_strdup("");
        return result;
    }

    /* Find last colon */
    const char* last_colon = strrchr(full_name, ':');
    const char* local_name = last_colon ? last_colon + 1 : full_name;

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        LEPTRIS_FREE(synth_name);
        return NULL;
    }
    result->value.string_value = leptris_strdup(local_name);
    LEPTRIS_FREE(synth_name);
    return result;
}

/**
 * Get namespace URI of a node
 * XPath: namespace-uri(nodeset?)
 */
static struct leptris_xpath_result* xpath_func_namespace_uri(
    XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    (void)context;
    void* node = NULL;
    char* synth_uri = NULL;  /* owned copy from a synthetic node */
    int node_tag = -1;       /* captured before the arg result is freed */

    /* Get context node if no argument */
    if (arg_count == 0) {
        node = context->context_node;
        if (node) node_tag = (int)XPATH_NODE_TYPE(node);
    } else if (arg_count == 1) {
        /* Use first node from nodeset */
        struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[0]);
        if (!arg_result) return NULL;

        if (arg_result->type == XPATH_RESULT_NODESET) {
            XPathNodeSet* nodeset = arg_result->value.nodeset_value;
            if (nodeset && xpath_nodeset_count(nodeset) > 0) {
                node = xpath_nodeset_get(nodeset, 0);
                /* Synthetic attribute/namespace nodes are owned by
                 * arg_result and die at the free below — copy the
                 * string out while the node is still alive. */
                if (node) {
                    int tag = (int)XPATH_NODE_TYPE(node);
                    node_tag = tag;
                    if (tag == LEPTRIS_NODE_ATTRIBUTE) {
                        const char* u = ((LeptrisAttributeNode*)node)->namespace_uri;
                        synth_uri = u ? leptris_strdup(u) : NULL;
                    } else if (tag == LEPTRIS_NODE_NAMESPACE) {
                        const char* u = ((LeptrisNamespaceNode*)node)->uri;
                        synth_uri = u ? leptris_strdup(u) : NULL;
                    }
                }
            }
        }
        xpath_result_free(arg_result);
    } else {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "namespace-uri() requires 0 or 1 argument");
        return NULL;
    }

    if (!node) {
        /* Return empty string for empty nodeset or no node */
        struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
        if (!result) return NULL;
        result->value.string_value = leptris_strdup("");
        return result;
    }

    /* Get namespace URI */
    const char* uri = NULL;
    if (node_tag == LEPTRIS_NODE_ELEMENT) {
        uri = leptris_element_get_namespace_uri((LeptrisElement)node);
    } else if (synth_uri) {
        uri = synth_uri;
    }

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        LEPTRIS_FREE(synth_uri);
        return NULL;
    }
    result->value.string_value = uri ? leptris_strdup(uri) : leptris_strdup("");
    LEPTRIS_FREE(synth_uri);
    return result;
}

/**
 * Get qualified name of a node
 * XPath: name(nodeset?)
 */
static struct leptris_xpath_result* xpath_func_name(
    XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    (void)context;
    void* node = NULL;
    char* synth_name = NULL;  /* owned copy from a synthetic node */
    int node_tag = -1;        /* captured before the arg result is freed */

    /* Get context node if no argument */
    if (arg_count == 0) {
        node = context->context_node;
        if (node) {
            node_tag = (int)XPATH_NODE_TYPE(node);
            if (node_tag == LEPTRIS_NODE_ATTRIBUTE) {
                const char* n = ((LeptrisAttributeNode*)node)->name;
                synth_name = n ? leptris_strdup(n) : NULL;
            } else if (node_tag == LEPTRIS_NODE_NAMESPACE) {
                const char* n = ((LeptrisNamespaceNode*)node)->prefix;
                synth_name = n ? leptris_strdup(n) : NULL;
            } else if (node_tag == LEPTRIS_NODE_TYPE_PI) {
                const char* t = leptris_pi_get_target((LeptrisPINode*)node);
                synth_name = t ? leptris_strdup(t) : NULL;
            }
        }
    } else if (arg_count == 1) {
        /* Use first node from nodeset */
        struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[0]);
        if (!arg_result) return NULL;

        if (arg_result->type == XPATH_RESULT_NODESET) {
            XPathNodeSet* nodeset = arg_result->value.nodeset_value;
            if (nodeset && xpath_nodeset_count(nodeset) > 0) {
                node = xpath_nodeset_get(nodeset, 0);
                /* Synthetic attribute/namespace nodes are owned by
                 * arg_result and die at the free below — copy the
                 * strings out while the node is still alive. */
                if (node) {
                    int tag = (int)XPATH_NODE_TYPE(node);
                    node_tag = tag;
                    if (tag == LEPTRIS_NODE_ATTRIBUTE) {
                        const char* n = ((LeptrisAttributeNode*)node)->name;
                        synth_name = n ? leptris_strdup(n) : NULL;
                    } else if (tag == LEPTRIS_NODE_NAMESPACE) {
                        const char* n = ((LeptrisNamespaceNode*)node)->prefix;
                        synth_name = n ? leptris_strdup(n) : NULL;
                    }
                }
            }
        }
        xpath_result_free(arg_result);
    } else {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "name() requires 0 or 1 argument");
        return NULL;
    }

    if (!node) {
        /* Return empty string for empty nodeset or no node */
        struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
        if (!result) return NULL;
        result->value.string_value = leptris_strdup("");
        return result;
    }

    /* Get qualified name */
    const char* name = NULL;
    char* temp_name = NULL;  /* For temporary allocations */
    if (node_tag == LEPTRIS_NODE_ELEMENT) {
        /* For elements, get qualified name (prefix:local if prefix exists) */
        LeptrisElement elem = (LeptrisElement)node;
        const char* local_name = leptris_element_get_name(elem);
        const char* prefix = leptris_element_get_prefix(elem);

        if (prefix && prefix[0] != '\0') {
            /* Construct qualified name: prefix:local */
            size_t prefix_len = strlen(prefix);
            size_t local_len = strlen(local_name);
            temp_name = LEPTRIS_ALLOC_N(char, prefix_len + 1 + local_len + 1);
            if (temp_name) {
                memcpy(temp_name, prefix, prefix_len);
                temp_name[prefix_len] = ':';
                memcpy(temp_name + prefix_len + 1, local_name, local_len);
                temp_name[prefix_len + 1 + local_len] = '\0';
                name = temp_name;
            }
        } else {
            /* No prefix, just use local name */
            name = local_name;
        }
    } else if (synth_name) {
        /* Synthetic attribute node: qualified attribute name.
         * Synthetic namespace node: its prefix (the name of a
         * namespace node per XPath 1.0). */
        name = synth_name;
    }

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        if (temp_name) LEPTRIS_FREE(temp_name);
        LEPTRIS_FREE(synth_name);
        return NULL;
    }
    result->value.string_value = name ? leptris_strdup(name) : leptris_strdup("");

    /* Free temporary allocation */
    if (temp_name) LEPTRIS_FREE(temp_name);
    LEPTRIS_FREE(synth_name);

    return result;
}

/**
 * Get elements by ID
 * XPath: id(string)
 */
static struct leptris_xpath_result* xpath_func_id(
    XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "id() requires exactly 1 argument");
        return NULL;
    }

    /* Get ID string argument */
    struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    char* id_value = result_to_string(arg_result);
    if (!id_value) {
        xpath_result_free(arg_result);
        return NULL;
    }

    /* Create nodeset result */
    XPathNodeSet* nodeset = xpath_nodeset_new();
    if (!nodeset) {
        LEPTRIS_FREE(id_value);
        xpath_result_free(arg_result);
        return NULL;
    }

    /* Search document for elements with matching id attribute */
    LeptrisElement root = (LeptrisElement)context->document->new_dom_root;
    if (root && id_value[0] != '\0') {
        /* XPath spec: id(string) can contain multiple space-separated IDs */
        /* Tokenize the string and search for each ID */
        char* str = id_value;
        char* token;
        char* rest = str;
        while ((token = strtok_r(rest, " \t\n\r", &rest))) {
            find_elements_by_id(context->document, root, token, nodeset);
        }
    }

    LEPTRIS_FREE(id_value);
    xpath_result_free(arg_result);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_NODESET);
    if (!result) {
        xpath_nodeset_free(nodeset);
        return NULL;
    }
    result->value.nodeset_value = nodeset;
    return result;
}

/* count(node-set) - Returns the number of nodes */
static struct leptris_xpath_result* xpath_func_count(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "count() requires exactly 1 argument");
        return NULL;
    }

    struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    /* XQuery 1.0+: count() accepts any sequence — an atomic
     * value is one item (QT3: count(fn:substring("",0)) = 1).
     * Empty sequences arrive as NULL results higher up. */
    size_t count = 1;
    if (arg_result->type == XPATH_RESULT_NODESET) {
        XPathNodeSet* nodeset = arg_result->value.nodeset_value;
        count = nodeset ? xpath_nodeset_count(nodeset) : 0;
    }
    xpath_result_free(arg_result);

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;
    result->value.number_value = (double)count;
    return result;
}

/* ============================================================================
 * XPath Language Functions
 * ============================================================================ */

/* Helper: Check if target language is a sublanguage of specified language */
static int _tb_is_sublanguage(const char* lang, const char* target) {
    /* Per XPath spec: lang('en') matches 'en', 'en-US', 'en-GB', etc. */
    if (!lang || !target) return 0;

    size_t lang_len = strlen(lang);
    size_t target_len = strlen(target);

    /* Target must be at least as long as lang */
    if (target_len < lang_len) return 0;

    /* Check if target starts with lang (case-insensitive) */
    if (strncasecmp(lang, target, lang_len) != 0) return 0;

    /* If lengths match, it's an exact match */
    if (target_len == lang_len) return 1;

    /* If target is longer, next char must be '-' for sublanguage */
    return (target[lang_len] == '-');
}

/* lang(string) - Check xml:lang matches */
static struct leptris_xpath_result* xpath_func_lang(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "lang() requires exactly 1 argument, got %zu", arg_count);
        return NULL;
    }

    struct leptris_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    /* §3.2 argument coercion: any type converts via string() —
     * libxslt stylesheets call lang(ja) with an element-name
     * argument (empty nodeset -> ""), which is false, not an error
     * (libxslt bug-142 surfaced this through issue 627's raise). */
    char* language = result_to_string(arg_result);
    xpath_result_free(arg_result);

    /* Walk up the ancestor chain looking for xml:lang attribute */
    /* XML namespace URI for xml:lang */
    const char* xml_ns_uri = "http://www.w3.org/XML/1998/namespace";
    int match = 0;

    /* Start from context node and go up through ancestors */
    LeptrisElement node = (LeptrisElement)context->context_node;
    while (node && !match) {

        /* Check for xml:lang attribute (in XML namespace) */
        /* First try by namespace URI */
        const char* lang_attr = NULL;

        /* Check attributes with namespace URI - walk linked list.
         *
         * TODO 34: route StringView conversions through the document
         * pool so we don't have to manually free() each one. */
        struct leptris_attribute* attr = leptris_element_get_first_attribute(node);
        while (attr && !lang_attr) {
            if (!attr) continue;

            LeptrisMemoryPool* pool = context->document ? context->document->pool : NULL;

            /* Get namespace URI (TODO 173: via ns_cache side table) */
            const char* ns_uri = attr_get_namespace_uri(attr);
            if (!ns_uri) {
                LeptrisStringView ns_view = attr_get_namespace_uri_view(attr);
                if (!leptris_sv_is_empty(&ns_view)) {
                    ns_uri = pool
                        ? leptris_sv_to_cstr_pooled(&ns_view, pool)
                        : leptris_sv_to_cstr(&ns_view);
                }
            }

            /* Get attribute name (single representation: the view IS
             * the NUL-terminated string; no conversion, no free). */
            const char* attr_name = attr_cname(attr);

            /* Check if this is xml:lang (try by namespace URI, by prefixed name, or by prefix) */
            int is_xml_lang_attr = 0;
            if (attr_name) {
                /* Check for plain "lang" name (namespace should be XML namespace) */
                if (strcmp(attr_name, "lang") == 0) {
                    is_xml_lang_attr = 1;
                }
                /* Also check for "xml:lang" prefixed form (when stored with prefix) */
                else if (strcmp(attr_name, "xml:lang") == 0) {
                    is_xml_lang_attr = 1;
                }
            }

            if (is_xml_lang_attr) {
                /* Check by namespace URI first */
                int is_xml_lang = 0;
                if (ns_uri && strcmp(ns_uri, xml_ns_uri) == 0) {
                    is_xml_lang = 1;
                }
                /* Also check if the prefix is "xml" (for compatibility) */
                else {
                    const char* prefix = attr_get_prefix(attr);
                    if (!prefix) {
                        LeptrisStringView pfx_view = attr_get_prefix_view(attr);
                        if (!leptris_sv_is_empty(&pfx_view)) {
                            LeptrisMemoryPool* pool2 = context->document ? context->document->pool : NULL;
                            prefix = pool2
                                ? leptris_sv_to_cstr_pooled(&pfx_view, pool2)
                                : leptris_sv_to_cstr(&pfx_view);
                        }
                    }
                    if (prefix && strcmp(prefix, "xml") == 0) {
                        is_xml_lang = 1;
                    }
                    /* Pool-owned: nothing to free. */
                }

                /* Also check if there's no namespace URI at all (xml:lang might be stored without ns) */
                if (!is_xml_lang && !ns_uri) {
                    /* This might be xml:lang stored without namespace information */
                    is_xml_lang = 1;
                }

                if (is_xml_lang) {
                    lang_attr = attr_cvalue(attr);
                }
            }

            /* Pool-routed conversions (TODO 34) don't need free.
             * Only legacy calloc'd intermediates need explicit free. */
            LeptrisMemoryPool* free_pool = context->document ? context->document->pool : NULL;
            if (!free_pool) {
                if (attr_get_namespace_uri(attr) != ns_uri && ns_uri) free((char*)ns_uri);
            }
            /* Pool-allocated: pool will reclaim on document free. */

            attr = leptris_attr_next(attr);
        }

        /* Check if we found xml:lang attribute — §4.3: the NEAREST
         * declaration decides. A closer non-matching declaration
         * ends the search; do not walk past it to outer ancestors
         * (libxslt bug-142: ja span inside an fr root). */
        if (lang_attr && lang_attr[0] != '\0') {
            match = _tb_is_sublanguage(language, lang_attr);
            break;
        }

        /* Move to parent if no match */
        if (!match) {
            /* Get parent element */
            LeptrisElement parent = leptris_element_get_parent(node);
            if (parent) {
                node = parent;
            } else {
                break; /* Reached root */
            }
        }
    }

    /* Create result */
    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) {
        LEPTRIS_FREE(language);
        return NULL;
    }
    result->value.boolean_value = match;

    LEPTRIS_FREE(language);
    return result;
}

/* ---- XPath 2.0+ string functions (XSLT 3.0) ---- */

/* upper-case(string) */
static struct leptris_xpath_result* xpath_func_upper_case(
    XPathContext* context, XPathASTNode** args, size_t arg_count
) {
    struct leptris_xpath_result* arg = xpath_evaluate(context, args[0]);
    if (!arg) return NULL;
    char* src = result_to_string(arg);
    xpath_result_free(arg);
    if (!src) return NULL;
    size_t n = strlen(src);
    char* out = LEPTRIS_ALLOC_N(char, n + 1);
    if (!out) { LEPTRIS_FREE(src); return NULL; }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)src[i];
        out[i] = (char)((c >= 'a' && c <= 'z') ? c - 32 : c);
    }
    out[n] = 0;
    LEPTRIS_FREE(src);
    struct leptris_xpath_result* result =
        xpath_result_new(XPATH_RESULT_STRING);
    if (!result) { LEPTRIS_FREE(out); return NULL; }
    result->value.string_value = out;
    return result;
}

/* lower-case(string) */
static struct leptris_xpath_result* xpath_func_lower_case(
    XPathContext* context, XPathASTNode** args, size_t arg_count
) {
    struct leptris_xpath_result* arg = xpath_evaluate(context, args[0]);
    if (!arg) return NULL;
    char* src = result_to_string(arg);
    xpath_result_free(arg);
    if (!src) return NULL;
    size_t n = strlen(src);
    char* out = LEPTRIS_ALLOC_N(char, n + 1);
    if (!out) { LEPTRIS_FREE(src); return NULL; }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)src[i];
        out[i] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
    }
    out[n] = 0;
    LEPTRIS_FREE(src);
    struct leptris_xpath_result* result =
        xpath_result_new(XPATH_RESULT_STRING);
    if (!result) { LEPTRIS_FREE(out); return NULL; }
    result->value.string_value = out;
    return result;
}

/* string-join(sequence, separator): joins each member's string form.
 * A nodeset joins per-node (the sequence's members); a scalar is a
 * one-member sequence. */
static struct leptris_xpath_result* xpath_func_string_join(
    XPathContext* context, XPathASTNode** args, size_t arg_count
) {
    struct leptris_xpath_result* seq = xpath_evaluate(context, args[0]);
    if (!seq) return NULL;
    /* XPath 3.1 one-arg form: separator defaults to the zero-length
     * string ($f(('a','b')) via string-join#1 depends on it). */
    char* sep;
    if (arg_count >= 2) {
        struct leptris_xpath_result* sep_r = xpath_evaluate(context, args[1]);
        if (!sep_r) { xpath_result_free(seq); return NULL; }
        sep = result_to_string(sep_r);
        xpath_result_free(sep_r);
        if (!sep) { xpath_result_free(seq); return NULL; }
    } else {
        sep = (char*)LEPTRIS_ALLOC_N(char, 1);
        if (!sep) { xpath_result_free(seq); return NULL; }
        sep[0] = 0;
    }
    size_t sep_len = strlen(sep);

    size_t cap = 64, len = 0;
    char* acc = (char*)LEPTRIS_ALLOC_N(char, cap);
    if (!acc) { LEPTRIS_FREE(sep); xpath_result_free(seq); return NULL; }
    acc[0] = 0;

    size_t n = 1;
    XPathNodeSet* ns = NULL;
    if (seq->type == XPATH_RESULT_NODESET &&
        seq->value.nodeset_value) {
        ns = seq->value.nodeset_value;
        n = ns->count;
    }
    for (size_t i = 0; i < n; i++) {
        char* piece;
        if (ns) {
            piece = get_node_text(ns->nodes[i]);
        } else {
            piece = result_to_string(seq);
        }
        if (!piece) piece = (char*)calloc(1, 1);
        size_t pl = strlen(piece);
        if (i > 0 && sep_len) {
            while (len + sep_len + pl + 1 > cap) cap *= 2;
            char* grown = (char*)realloc(acc, cap);
            if (!grown) { free(piece); break; }
            acc = grown;
            memcpy(acc + len, sep, sep_len);
            len += sep_len;
        } else {
            while (len + pl + 1 > cap) cap *= 2;
            char* grown = (char*)realloc(acc, cap);
            if (!grown) { free(piece); break; }
            acc = grown;
        }
        memcpy(acc + len, piece, pl + 1);
        len += pl;
        free(piece);
        if (!ns) break;   /* scalar sequence: one member */
    }
    LEPTRIS_FREE(sep);
    xpath_result_free(seq);

    struct leptris_xpath_result* result =
        xpath_result_new(XPATH_RESULT_STRING);
    if (!result) { free(acc); return NULL; }
    result->value.string_value = acc;
    return result;
}

/* ============================================================================
 * XPath 1.0 Functions Initialization
 * ============================================================================ */

/* Array of all supported function names (NULL-terminated) */
static const char* g_supported_functions[] = {
    /* Core */
    "last",
    "position",
    /* String */
    "string",
    "concat",
    "starts-with",
    "contains",
    "substring-before",
    "substring-after",
    "substring",
    "string-length",
    "normalize-space",
    "translate",
    /* XPath 2.0+ (XSLT 3.0) */
    "upper-case",
    "lower-case",
    "string-join",
    /* Boolean */
    "boolean",
    "not",
    "true",
    "false",
    "lang",
    /* Number */
    "number",
    "sum",
    "floor",
    "ceiling",
    "round",
    /* Node-set */
    "count",
    "id",
    "local-name",
    "namespace-uri",
    "name",
    NULL  /* Terminator */
};

/**
 * Check if XPath function is supported
 */
LEPTRIS_API int leptris_xpath_function_supported(const char* function_name) {
    if (!function_name) return 0;

    for (size_t i = 0; g_supported_functions[i] != NULL; i++) {
        if (strcmp(function_name, g_supported_functions[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * Get list of supported XPath functions
 */
LEPTRIS_API const char** leptris_xpath_supported_functions(void) {
    return g_supported_functions;
}

/* ============================================================================
 * Standard Function Registry Initialization
 * ============================================================================ */

/**
 * Initialize standard XPath 1.0 functions
 * Registers all implemented XPath 1.0 functions in the registry
 */
void xpath_function_registry_init_standard(XPathFunctionRegistry* registry) {
    if (!registry) return;

    /* Core functions (2) */
    xpath_function_registry_register(registry, "last", xpath_func_last, 0, 0);
    xpath_function_registry_register(registry, "position", xpath_func_position, 0, 0);

    /* String functions (10 implemented) */
    xpath_function_registry_register(registry, "string", xpath_func_string, 0, 1);
    xpath_function_registry_register(registry, "concat", xpath_func_concat, 2, -1);
    xpath_function_registry_register(registry, "starts-with", xpath_func_starts_with, 2, 3);
    xpath_function_registry_register(registry, "ends-with", xpath_func_ends_with, 2, 3);
    xpath_function_registry_register(registry, "deep-equal", xpath_func_deep_equal, 2, 3);
    xpath_function_registry_register(registry, "contains", xpath_func_contains, 2, 3);
    xpath_function_registry_register(registry, "substring", xpath_func_substring, 2, 3);
    xpath_function_registry_register(registry, "substring-before", xpath_func_substring_before, 2, 3);
    xpath_function_registry_register(registry, "substring-after", xpath_func_substring_after, 2, 3);
    xpath_function_registry_register(registry, "string-length", xpath_func_string_length, 0, 1);
    xpath_function_registry_register(registry, "normalize-space", xpath_func_normalize_space, 0, 1);
    xpath_function_registry_register(registry, "translate", xpath_func_translate, 3, 3);

    /* XPath 2.0+ string functions (XSLT 3.0) */
    xpath_function_registry_register(registry, "upper-case", xpath_func_upper_case, 1, 1);
    xpath_function_registry_register(registry, "lower-case", xpath_func_lower_case, 1, 1);
    xpath_function_registry_register(registry, "string-join", xpath_func_string_join, 1, 2);

    /* Boolean functions (5) */
    xpath_function_registry_register(registry, "boolean", xpath_func_boolean, 1, 1);
    xpath_function_registry_register(registry, "not", xpath_func_not, 1, 1);
    xpath_function_registry_register(registry, "true", xpath_func_true, 0, 0);
    xpath_function_registry_register(registry, "false", xpath_func_false, 0, 0);
    xpath_function_registry_register(registry, "lang", xpath_func_lang, 1, 1);

    /* Number functions (5) */
    xpath_function_registry_register(registry, "number", xpath_func_number, 0, 1);
    xpath_function_registry_register(registry, "sum", xpath_func_sum, 1, 1);
    xpath_function_registry_register(registry, "floor", xpath_func_floor, 1, 1);
    xpath_function_registry_register(registry, "ceiling", xpath_func_ceiling, 1, 1);
    xpath_function_registry_register(registry, "round", xpath_func_round, 1, 2);

    /* Node-set functions (5 implemented) */
    xpath_function_registry_register(registry, "count", xpath_func_count, 1, 1);
    xpath_function_registry_register(registry, "id", xpath_func_id, 1, 1);

    /* XPath 2.0/3.1 extension families (TODO.xslt-full 01/02/03):
     * sequences, math:, the regex trio. */
    extern void xpath_register_fn31(XPathFunctionRegistry*);
    xpath_register_fn31(registry);
    xpath_function_registry_register(registry, "local-name", xpath_func_local_name, 0, 1);
    xpath_function_registry_register(registry, "namespace-uri", xpath_func_namespace_uri, 0, 1);
    xpath_function_registry_register(registry, "name", xpath_func_name, 0, 1);
}
