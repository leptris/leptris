/* functions_string.c - XPath string functions
 * Copyright (c) 2024, Ribose Inc.
 *
 * Implements XPath 1.0 string functions:
 * - string(object?) - Convert to string
 * - concat(string, string, string*) - Concatenate strings
 * - starts-with(string, string) - Check prefix
 * - contains(string, string) - Check substring
 * - substring(string, number, number?) - Extract substring
 * - substring-before(string, string) - Get prefix
 * - substring-after(string, string) - Get suffix
 * - string-length(string?) - Get length
 * - normalize-space(string?) - Normalize whitespace
 * - translate(string, string, string) - Character mapping
 */

#include "functions_internal.h"

/* ============================================================================
 * Helper: Get element text (wrapper for evaluator_types.c function)
 * ============================================================================ */

static char* get_element_text(void* node) {
    return get_node_text(node);
}

/* ============================================================================
 * XPath String Functions
 * ============================================================================ */

/* string(object?) - Convert argument to string */
struct taurus_xpath_result* xpath_func_string(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count > 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "string() takes 0 or 1 argument");
        return NULL;
    }

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) return NULL;

    if (arg_count == 0) {
        /* No argument: convert context node to string */
        result->value.string_value = get_element_text(context->context_node);
    } else {
        /* Evaluate argument and convert to string */
        struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[0]);
        if (!arg_result) {
            xpath_result_free(result);
            return NULL;
        }
        result->value.string_value = xpath_to_string(arg_result);
        xpath_result_free(arg_result);
    }

    return result;
}

/* concat(string, string, string*) - Concatenate strings */
struct taurus_xpath_result* xpath_func_concat(XPathContext* context,
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
    char** strings = TAURUS_ALLOC_N(char*, arg_count);
    if (!strings) return NULL;

    for (size_t i = 0; i < arg_count; i++) {
        struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[i]);
        if (!arg_result) {
            for (size_t j = 0; j < i; j++) {
                TAURUS_FREE(strings[j]);
            }
            TAURUS_FREE(strings);
            return NULL;
        }

        strings[i] = xpath_to_string(arg_result);
        total_length += strlen(strings[i]);
        xpath_result_free(arg_result);
    }

    /* Allocate result string */
    char* concat_str = TAURUS_ALLOC_N(char, total_length + 1);
    if (!concat_str) {
        for (size_t i = 0; i < arg_count; i++) {
            TAURUS_FREE(strings[i]);
        }
        TAURUS_FREE(strings);
        return NULL;
    }

    /* Concatenate all strings */
    memset(concat_str, 0, total_length + 1);
    size_t index = 0;
    for (size_t i = 0; i < arg_count; i++) {
        size_t len = strlen(strings[i]);  /* Get length BEFORE freeing */
        strcpy(concat_str + index, strings[i]);
        index += len;
        TAURUS_FREE(strings[i]);
    }
    TAURUS_FREE(strings);

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        TAURUS_FREE(concat_str);
        return NULL;
    }
    result->value.string_value = concat_str;
    return result;
}

/* starts-with(string, string) - Check if string starts with prefix */
struct taurus_xpath_result* xpath_func_starts_with(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 2) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "starts-with() requires exactly 2 arguments, got %zu", arg_count);
        return NULL;
    }

    struct taurus_xpath_result* str_result = xpath_evaluate(context, args[0]);
    if (!str_result) return NULL;

    struct taurus_xpath_result* prefix_result = xpath_evaluate(context, args[1]);
    if (!prefix_result) {
        xpath_result_free(str_result);
        return NULL;
    }

    char* str = xpath_to_string(str_result);
    char* prefix = xpath_to_string(prefix_result);

    int match = (strncmp(str, prefix, strlen(prefix)) == 0);

    TAURUS_FREE(str);
    TAURUS_FREE(prefix);
    xpath_result_free(str_result);
    xpath_result_free(prefix_result);

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) return NULL;
    result->value.boolean_value = match;

    return result;
}

/* contains(string, string) - Check if string contains substring */
struct taurus_xpath_result* xpath_func_contains(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 2) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "contains() requires exactly 2 arguments, got %zu", arg_count);
        return NULL;
    }

    struct taurus_xpath_result* str_result = xpath_evaluate(context, args[0]);
    if (!str_result) return NULL;

    struct taurus_xpath_result* substr_result = xpath_evaluate(context, args[1]);
    if (!substr_result) {
        xpath_result_free(str_result);
        return NULL;
    }

    char* str = xpath_to_string(str_result);
    char* substr = xpath_to_string(substr_result);

    int match = (strstr(str, substr) != NULL);

    TAURUS_FREE(str);
    TAURUS_FREE(substr);
    xpath_result_free(str_result);
    xpath_result_free(substr_result);

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) return NULL;
    result->value.boolean_value = match;

    return result;
}

/* substring(string, start, length?) - Extract substring */
struct taurus_xpath_result* xpath_func_substring(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count < 2 || arg_count > 3) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "substring() requires 2 or 3 arguments");
        return NULL;
    }

    /* Evaluate string argument */
    struct taurus_xpath_result* str_result = xpath_evaluate(context, args[0]);
    if (!str_result) return NULL;
    char* str = xpath_to_string(str_result);
    xpath_result_free(str_result);
    if (!str) return NULL;

    size_t len = strlen(str);

    /* Evaluate start position argument */
    struct taurus_xpath_result* start_result = xpath_evaluate(context, args[1]);
    if (!start_result) {
        TAURUS_FREE(str);
        return NULL;
    }
    double start_double = xpath_to_number(start_result);
    xpath_result_free(start_result);

    /* XPath positions are 1-indexed, round to nearest integer */
    long start_rounded = (long)floor(start_double + 0.5);

    /* XPath spec: clamp start to minimum 1 */
    long effective_start = start_rounded < 1 ? 1 : start_rounded;

    /* Calculate actual start index (0-indexed) */
    size_t start_idx = (size_t)(effective_start - 1);

    if (start_idx >= len) {
        /* Start is past end of string */
        TAURUS_FREE(str);
        struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
        if (result) result->value.string_value = taurus_strdup("");
        return result;
    }

    size_t count;
    if (arg_count == 2) {
        /* substring(string, start) - return all characters from start to end */
        count = len - start_idx;
    } else {
        /* substring(string, start, length) */
        struct taurus_xpath_result* len_result = xpath_evaluate(context, args[2]);
        if (!len_result) {
            TAURUS_FREE(str);
            return NULL;
        }
        double len_double = xpath_to_number(len_result);
        xpath_result_free(len_result);

        long len_rounded = (long)floor(len_double + 0.5);
        if (len_rounded < 0) len_rounded = 0;
        count = (size_t)len_rounded;

        /* Don't exceed available characters */
        if (count > len - start_idx) {
            count = len - start_idx;
        }
    }

    char* result_str = TAURUS_ALLOC_N(char, count + 1);
    if (!result_str) {
        TAURUS_FREE(str);
        return NULL;
    }
    memcpy(result_str, str + start_idx, count);
    result_str[count] = '\0';
    TAURUS_FREE(str);

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        TAURUS_FREE(result_str);
        return NULL;
    }
    result->value.string_value = result_str;
    return result;
}

/* substring-before(string, pattern) - Get prefix before pattern */
struct taurus_xpath_result* xpath_func_substring_before(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 2) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "substring-before() requires exactly 2 arguments");
        return NULL;
    }

    /* Evaluate string argument */
    struct taurus_xpath_result* str_result = xpath_evaluate(context, args[0]);
    if (!str_result) return NULL;
    char* str = xpath_to_string(str_result);
    xpath_result_free(str_result);
    if (!str) return NULL;

    /* Evaluate pattern argument */
    struct taurus_xpath_result* pattern_result = xpath_evaluate(context, args[1]);
    if (!pattern_result) {
        TAURUS_FREE(str);
        return NULL;
    }
    char* pattern = xpath_to_string(pattern_result);
    xpath_result_free(pattern_result);

    if (!pattern) {
        TAURUS_FREE(str);
        return NULL;
    }

    char* result_str;
    if (pattern[0] == '\0') {
        /* Empty pattern returns empty string */
        result_str = taurus_strdup("");
    } else {
        char* pos = strstr(str, pattern);
        if (!pos) {
            result_str = taurus_strdup("");
        } else {
            size_t prefix_len = (size_t)(pos - str);
            result_str = TAURUS_ALLOC_N(char, prefix_len + 1);
            if (result_str) {
                memcpy(result_str, str, prefix_len);
                result_str[prefix_len] = '\0';
            }
        }
    }

    TAURUS_FREE(str);
    TAURUS_FREE(pattern);

    if (!result_str) return NULL;

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        TAURUS_FREE(result_str);
        return NULL;
    }
    result->value.string_value = result_str;
    return result;
}

/* substring-after(string, pattern) - Get suffix after pattern */
struct taurus_xpath_result* xpath_func_substring_after(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 2) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "substring-after() requires exactly 2 arguments");
        return NULL;
    }

    /* Evaluate string argument */
    struct taurus_xpath_result* str_result = xpath_evaluate(context, args[0]);
    if (!str_result) return NULL;
    char* str = xpath_to_string(str_result);
    xpath_result_free(str_result);
    if (!str) return NULL;

    /* Evaluate pattern argument */
    struct taurus_xpath_result* pattern_result = xpath_evaluate(context, args[1]);
    if (!pattern_result) {
        TAURUS_FREE(str);
        return NULL;
    }
    char* pattern = xpath_to_string(pattern_result);
    xpath_result_free(pattern_result);

    if (!pattern) {
        TAURUS_FREE(str);
        return NULL;
    }

    char* result_str;
    if (pattern[0] == '\0') {
        /* Empty pattern returns entire string per XPath spec */
        result_str = taurus_strdup(str);
    } else {
        char* pos = strstr(str, pattern);
        if (!pos) {
            result_str = taurus_strdup("");
        } else {
            char* after = pos + strlen(pattern);
            size_t suffix_len = strlen(after);
            result_str = TAURUS_ALLOC_N(char, suffix_len + 1);
            if (result_str) {
                memcpy(result_str, after, suffix_len);
                result_str[suffix_len] = '\0';
            }
        }
    }

    TAURUS_FREE(str);
    TAURUS_FREE(pattern);

    if (!result_str) return NULL;

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        TAURUS_FREE(result_str);
        return NULL;
    }
    result->value.string_value = result_str;
    return result;
}

/* string-length(string?) - Get string length */
struct taurus_xpath_result* xpath_func_string_length(XPathContext* context,
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
        struct taurus_xpath_result* str_result = xpath_evaluate(context, args[0]);
        if (!str_result) return NULL;
        str = xpath_to_string(str_result);
        xpath_result_free(str_result);
    } else {
        /* No argument - use context node's string value */
        struct taurus_xpath_result* str_result = xpath_func_string(context, NULL, 0);
        if (!str_result) return NULL;
        str = xpath_to_string(str_result);
        xpath_result_free(str_result);
    }

    size_t len = str ? strlen(str) : 0;
    TAURUS_FREE(str);

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;
    result->value.number_value = (double)len;
    return result;
}

/* normalize-space(string?) - Normalize whitespace */
struct taurus_xpath_result* xpath_func_normalize_space(XPathContext* context,
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
        struct taurus_xpath_result* str_result = xpath_evaluate(context, args[0]);
        if (!str_result) return NULL;
        str = xpath_to_string(str_result);
        xpath_result_free(str_result);
    } else {
        /* No argument - use context node's string value */
        struct taurus_xpath_result* str_result = xpath_func_string(context, NULL, 0);
        if (!str_result) return NULL;
        str = xpath_to_string(str_result);
        xpath_result_free(str_result);
    }

    if (!str) {
        struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
        if (result) result->value.string_value = taurus_strdup("");
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

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        TAURUS_FREE(str);
        return NULL;
    }

    /* str may have been modified in place, but we need to keep it valid */
    result->value.string_value = taurus_strdup(str);
    TAURUS_FREE(str);

    return result;
}

/* translate(string, from, to) - Character mapping */
struct taurus_xpath_result* xpath_func_translate(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 3) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "translate() requires exactly 3 arguments");
        return NULL;
    }

    /* Evaluate arguments */
    struct taurus_xpath_result* str_result = xpath_evaluate(context, args[0]);
    if (!str_result) return NULL;
    char* str = xpath_to_string(str_result);
    xpath_result_free(str_result);
    if (!str) return NULL;

    struct taurus_xpath_result* from_result = xpath_evaluate(context, args[1]);
    if (!from_result) {
        TAURUS_FREE(str);
        return NULL;
    }
    char* from = xpath_to_string(from_result);
    xpath_result_free(from_result);

    struct taurus_xpath_result* to_result = xpath_evaluate(context, args[2]);
    if (!to_result) {
        TAURUS_FREE(str);
        TAURUS_FREE(from);
        return NULL;
    }
    char* to = xpath_to_string(to_result);
    xpath_result_free(to_result);

    if (!from || !to) {
        TAURUS_FREE(str);
        TAURUS_FREE(from);
        TAURUS_FREE(to);
        return NULL;
    }

    /* Build translation table for first 256 characters */
    unsigned char map[256];
    for (int i = 0; i < 256; i++) {
        map[i] = (unsigned char)i;  /* Default: keep character */
    }

    size_t from_len = strlen(from);
    size_t to_len = strlen(to);

    /* For each character in 'from', map to corresponding in 'to' */
    for (size_t i = 0; i < from_len; i++) {
        unsigned char c = (unsigned char)from[i];
        if (i < to_len) {
            map[c] = (unsigned char)to[i];
        } else {
            map[c] = 0;  /* Remove character if no corresponding replacement */
        }
    }

    /* Translate the string */
    for (char* p = str; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (map[c] == 0) {
            /* Remove character by shifting */
            char* q = p;
            while (*q) {
                *q = *(q + 1);
                q++;
            }
            p--;  /* Re-check this position */
        } else if (map[c] != c) {
            *p = (char)map[c];
        }
    }

    TAURUS_FREE(from);
    TAURUS_FREE(to);

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        TAURUS_FREE(str);
        return NULL;
    }
    result->value.string_value = str;
    return result;
}
