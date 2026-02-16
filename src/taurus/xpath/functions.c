/* functions.c - XPath 1.0 function library implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C implementation of all 27 XPath 1.0 standard functions.
 * Converted from Ruby C extension to standalone C library.
 */

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
 * Forward Declarations for functions in separate files
 * ============================================================================ */

/* Core functions - functions_core.c */
extern struct taurus_xpath_result* xpath_func_last(XPathContext* context,
                                                   XPathASTNode** args,
                                                   size_t arg_count);
extern struct taurus_xpath_result* xpath_func_position(XPathContext* context,
                                                       XPathASTNode** args,
                                                       size_t arg_count);

/* Boolean functions - functions_boolean.c */
extern struct taurus_xpath_result* xpath_func_boolean(XPathContext* context,
                                                      XPathASTNode** args,
                                                      size_t arg_count);
extern struct taurus_xpath_result* xpath_func_not(XPathContext* context,
                                                  XPathASTNode** args,
                                                  size_t arg_count);
extern struct taurus_xpath_result* xpath_func_true(XPathContext* context,
                                                   XPathASTNode** args,
                                                   size_t arg_count);
extern struct taurus_xpath_result* xpath_func_false(XPathContext* context,
                                                    XPathASTNode** args,
                                                    size_t arg_count);

/* Number functions - functions_number.c */
extern struct taurus_xpath_result* xpath_func_number(XPathContext* context,
                                                     XPathASTNode** args,
                                                     size_t arg_count);
extern struct taurus_xpath_result* xpath_func_sum(XPathContext* context,
                                                  XPathASTNode** args,
                                                  size_t arg_count);
extern struct taurus_xpath_result* xpath_func_floor(XPathContext* context,
                                                    XPathASTNode** args,
                                                    size_t arg_count);
extern struct taurus_xpath_result* xpath_func_ceiling(XPathContext* context,
                                                      XPathASTNode** args,
                                                      size_t arg_count);
extern struct taurus_xpath_result* xpath_func_round(XPathContext* context,
                                                    XPathASTNode** args,
                                                    size_t arg_count);

/* String functions - functions_string.c */
extern struct taurus_xpath_result* xpath_func_string(XPathContext* context,
                                                     XPathASTNode** args,
                                                     size_t arg_count);
extern struct taurus_xpath_result* xpath_func_concat(XPathContext* context,
                                                     XPathASTNode** args,
                                                     size_t arg_count);
extern struct taurus_xpath_result* xpath_func_starts_with(XPathContext* context,
                                                          XPathASTNode** args,
                                                          size_t arg_count);
extern struct taurus_xpath_result* xpath_func_contains(XPathContext* context,
                                                       XPathASTNode** args,
                                                       size_t arg_count);
extern struct taurus_xpath_result* xpath_func_substring(XPathContext* context,
                                                        XPathASTNode** args,
                                                        size_t arg_count);
extern struct taurus_xpath_result* xpath_func_substring_before(XPathContext* context,
                                                               XPathASTNode** args,
                                                               size_t arg_count);
extern struct taurus_xpath_result* xpath_func_substring_after(XPathContext* context,
                                                              XPathASTNode** args,
                                                              size_t arg_count);
extern struct taurus_xpath_result* xpath_func_string_length(XPathContext* context,
                                                            XPathASTNode** args,
                                                            size_t arg_count);
extern struct taurus_xpath_result* xpath_func_normalize_space(XPathContext* context,
                                                              XPathASTNode** args,
                                                              size_t arg_count);
extern struct taurus_xpath_result* xpath_func_translate(XPathContext* context,
                                                        XPathASTNode** args,
                                                        size_t arg_count);

/* Nodeset functions - functions_nodeset.c */
extern struct taurus_xpath_result* xpath_func_count(XPathContext* context,
                                                    XPathASTNode** args,
                                                    size_t arg_count);
extern struct taurus_xpath_result* xpath_func_id(XPathContext* context,
                                                 XPathASTNode** args,
                                                 size_t arg_count);
extern struct taurus_xpath_result* xpath_func_local_name(XPathContext* context,
                                                         XPathASTNode** args,
                                                         size_t arg_count);
extern struct taurus_xpath_result* xpath_func_namespace_uri(XPathContext* context,
                                                            XPathASTNode** args,
                                                            size_t arg_count);
extern struct taurus_xpath_result* xpath_func_name(XPathContext* context,
                                                   XPathASTNode** args,
                                                   size_t arg_count);
extern struct taurus_xpath_result* xpath_func_lang(XPathContext* context,
                                                   XPathASTNode** args,
                                                   size_t arg_count);

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

/* External evaluator function */
extern struct taurus_xpath_result* xpath_evaluate(XPathContext* context,
                                                   XPathASTNode* ast);

/* Helper functions */
static char* get_node_text(void* node);
static char* result_to_string(struct taurus_xpath_result* result);
static int result_to_boolean(struct taurus_xpath_result* result);
static double result_to_number(struct taurus_xpath_result* result);

/* UTF-8 helpers */
static size_t utf8_strlen(const char* str);
static size_t utf8_char_offset(const char* str, size_t char_pos);
static char* utf8_substring(const char* str, size_t start_char, size_t char_count);

/* ============================================================================
 * Function Registry Implementation
 * ============================================================================ */

XPathFunctionRegistry* xpath_function_registry_new(void) {
    XPathFunctionRegistry* registry = TAURUS_ALLOC(XPathFunctionRegistry);
    if (!registry) return NULL;

    registry->functions = NULL;
    registry->count = 0;
    registry->capacity = 0;

    return registry;
}

void xpath_function_registry_free(XPathFunctionRegistry* registry) {
    if (!registry) return;

    if (registry->functions) {
        TAURUS_FREE(registry->functions);
    }
    TAURUS_FREE(registry);
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
        XPathFunctionDef* new_functions = TAURUS_REALLOC_N(
            registry->functions,
            XPathFunctionDef,
            new_capacity
        );
        if (!new_functions) return;
        registry->functions = new_functions;
        registry->capacity = new_capacity;
    }

    /* Add function */
    registry->functions[registry->count].name = name;
    registry->functions[registry->count].handler = handler;
    registry->functions[registry->count].min_args = min_args;
    registry->functions[registry->count].max_args = max_args;
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

/* Backward compatibility wrapper */
static char* get_element_text(TaurusElement element) {
    return get_node_text((void*)element);
}

/* Convert XPath result to string according to XPath 1.0 spec */
static char* result_to_string(struct taurus_xpath_result* result) {
    if (!result) return taurus_strdup("");

    switch (result->type) {
        case XPATH_RESULT_STRING:
            return result->value.string_value ?
                   taurus_strdup(result->value.string_value) : taurus_strdup("");

        case XPATH_RESULT_NUMBER: {
            double num = result->value.number_value;
            char buffer[64];

            /* Handle special values per XPath spec */
            if (isnan(num)) {
                return taurus_strdup("NaN");
            } else if (isinf(num)) {
                return taurus_strdup(num > 0 ? "Infinity" : "-Infinity");
            } else if (num == 0.0) {
                return taurus_strdup("0");
            } else if (num == floor(num)) {
                /* Integer - no decimal point */
                snprintf(buffer, sizeof(buffer), "%.0f", num);
            } else {
                snprintf(buffer, sizeof(buffer), "%g", num);
            }
            return taurus_strdup(buffer);
        }

        case XPATH_RESULT_BOOLEAN:
            return taurus_strdup(result->value.boolean_value ? "true" : "false");

        case XPATH_RESULT_NODESET: {
            /* String value of first node in document order */
            XPathNodeSet* nodeset = result->value.nodeset_value;
            if (!nodeset || xpath_nodeset_count(nodeset) == 0) {
                return taurus_strdup("");
            }
            void* first_node = xpath_nodeset_get(nodeset, 0);
            return get_node_text(first_node);
        }

        default:
            return taurus_strdup("");
    }
}

/* Convert XPath result to boolean according to XPath 1.0 spec */
static int result_to_boolean(struct taurus_xpath_result* result) {
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
static double result_to_number(struct taurus_xpath_result* result) {
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
                TAURUS_FREE(str);
                return NAN;
            }

            char* endptr;
            double value = strtod(p, &endptr);

            while (isspace((unsigned char)*endptr)) endptr++;

            if (*endptr != '\0') {
                TAURUS_FREE(str);
                return NAN;
            }

            TAURUS_FREE(str);
            return value;
        }

        default:
            return NAN;
    }
}

/* ============================================================================
 * UTF-8 Helper Functions
 * ============================================================================ */

/* Count UTF-8 characters (not bytes) in a string */
size_t utf8_strlen(const char* str) {
    if (!str) return 0;

    size_t count = 0;
    const unsigned char* p = (const unsigned char*)str;

    while (*p) {
        /* Count leading bytes only (not continuation bytes 10xxxxxx) */
        if ((*p & 0xC0) != 0x80) {
            count++;
        }
        p++;
    }

    return count;
}

/* Get byte offset for UTF-8 character at position (0-based character index) */
size_t utf8_char_offset(const char* str, size_t char_pos) {
    if (!str) return 0;

    size_t count = 0;
    size_t offset = 0;
    const unsigned char* p = (const unsigned char*)str;

    /* Special case: position 0 is offset 0 */
    if (char_pos == 0) return 0;

    while (*p && count < char_pos) {
        /* Check if this is a new character (not a continuation byte) */
        if ((*p & 0xC0) != 0x80) {
            count++;
        }
        /* Move to next byte */
        offset++;
        p++;
    }

    /* Return the current offset */
    return offset;
}

/* Extract substring by character positions (0-based character indices) */
char* utf8_substring(const char* str, size_t start_char, size_t char_count) {
    if (!str || char_count == 0) return taurus_strdup("");

    size_t str_len_chars = utf8_strlen(str);

    /* Clamp start position to valid range */
    if (start_char >= str_len_chars) {
        return taurus_strdup("");
    }

    /* Clamp character count to available characters */
    if (start_char + char_count > str_len_chars) {
        char_count = str_len_chars - start_char;
    }

    /* Get byte offsets */
    size_t start_byte = utf8_char_offset(str, start_char);
    size_t end_char = start_char + char_count;
    size_t end_byte = utf8_char_offset(str, end_char);

    size_t length = end_byte - start_byte;
    if (length == 0) return taurus_strdup("");

    char* result = TAURUS_ALLOC_N(char, length + 1);
    if (!result) return taurus_strdup("");

    memcpy(result, str + start_byte, length);
    result[length] = '\0';

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
TAURUS_API int taurus_xpath_function_supported(const char* function_name) {
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
TAURUS_API const char** taurus_xpath_supported_functions(void) {
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
    xpath_function_registry_register(registry, "starts-with", xpath_func_starts_with, 2, 2);
    xpath_function_registry_register(registry, "contains", xpath_func_contains, 2, 2);
    xpath_function_registry_register(registry, "substring", xpath_func_substring, 2, 3);
    xpath_function_registry_register(registry, "substring-before", xpath_func_substring_before, 2, 2);
    xpath_function_registry_register(registry, "substring-after", xpath_func_substring_after, 2, 2);
    xpath_function_registry_register(registry, "string-length", xpath_func_string_length, 0, 1);
    xpath_function_registry_register(registry, "normalize-space", xpath_func_normalize_space, 0, 1);
    xpath_function_registry_register(registry, "translate", xpath_func_translate, 3, 3);

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
    xpath_function_registry_register(registry, "round", xpath_func_round, 1, 1);

    /* Node-set functions (5 implemented) */
    xpath_function_registry_register(registry, "count", xpath_func_count, 1, 1);
    xpath_function_registry_register(registry, "id", xpath_func_id, 1, 1);
    xpath_function_registry_register(registry, "local-name", xpath_func_local_name, 0, 1);
    xpath_function_registry_register(registry, "namespace-uri", xpath_func_namespace_uri, 0, 1);
    xpath_function_registry_register(registry, "name", xpath_func_name, 0, 1);
}
