/* evaluator_types.c - XPath type conversion functions
 * Copyright (c) 2024, Ribose Inc.
 *
 * Type conversions per XPath 1.0 specification Section 4
 */

#include "evaluator_internal.h"
#include "../taurus_internal.h"
#include "../dom/element.h"  /* For TaurusElement structure */
#include "../../include/taurus.h"  /* For taurus_text() function */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * Type Conversions (XPath 1.0 Spec Section 4)
 * ============================================================================ */

/* Get text content from typed node (handles elements and attributes) */
char* get_node_text(void* node) {
    if (!node) return taurus_strdup("");

    TaurusNodeType node_type = XPATH_NODE_TYPE(node);

    switch (node_type) {
        case TAURUS_NODE_ELEMENT: {
            TaurusElement element = (TaurusElement)node;

            /* Use the New DOM text extraction function */
            return taurus_element_get_text_content(element);
        }

        case TAURUS_NODE_ATTRIBUTE: {
            TaurusAttributeNode* attr_node = (TaurusAttributeNode*)node;
            return taurus_strdup(attr_node->value ? attr_node->value : "");
        }

        default:
            return taurus_strdup("");
    }
}

int xpath_to_boolean(struct taurus_xpath_result* result) {
    if (!result) return 0;

    switch (result->type) {
        case XPATH_RESULT_BOOLEAN:
            return result->value.boolean_value;
        case XPATH_RESULT_NUMBER:
            return result->value.number_value != 0.0 && !isnan(result->value.number_value);
        case XPATH_RESULT_STRING:
            return result->value.string_value && result->value.string_value[0] != '\0';
        case XPATH_RESULT_NODESET:
            return xpath_nodeset_count(result->value.nodeset_value) > 0;
        default:
            return 0;
    }
}

double xpath_to_number(struct taurus_xpath_result* result) {
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

            /* Per W3C XPath 1.0 Section 4.4: any string that doesn't match
             * the number pattern (optional whitespace, optional minus, Number,
             * whitespace) is converted to NaN. Empty string = NaN. */
            if (*str == '\0') return NAN;

            /* Parse number */
            char* endptr;
            double value = strtod(str, &endptr);

            /* Skip trailing whitespace */
            while (isspace((unsigned char)*endptr)) endptr++;

            /* Must consume entire string */
            return (*endptr == '\0') ? value : NAN;
        }
        case XPATH_RESULT_NODESET: {
            /* Convert first node's string value to number */
            XPathNodeSet* nodeset = result->value.nodeset_value;
            if (!nodeset || xpath_nodeset_count(nodeset) == 0) {
                return NAN;
            }
            void* first = xpath_nodeset_get(nodeset, 0);
            char* str = get_node_text(first);
            if (!str) return NAN;

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

            int valid = (*endptr == '\0');
            TAURUS_FREE(str);
            return valid ? value : NAN;
        }
        default:
            return NAN;
    }
}

char* xpath_to_string(struct taurus_xpath_result* result) {
    if (!result) return taurus_strdup("");

    switch (result->type) {
        case XPATH_RESULT_STRING:
            return taurus_strdup(result->value.string_value ?
                               result->value.string_value : "");
        case XPATH_RESULT_NUMBER: {
            char buf[64];
            double num = result->value.number_value;
            if (isnan(num)) {
                return taurus_strdup("NaN");
            } else if (isinf(num)) {
                return taurus_strdup(num > 0 ? "Infinity" : "-Infinity");
            } else {
                snprintf(buf, sizeof(buf), "%g", num);
                return taurus_strdup(buf);
            }
        }
        case XPATH_RESULT_BOOLEAN:
            return taurus_strdup(result->value.boolean_value ? "true" : "false");
        case XPATH_RESULT_NODESET: {
            XPathNodeSet* nodeset = result->value.nodeset_value;
            if (!nodeset || xpath_nodeset_count(nodeset) == 0) {
                return taurus_strdup("");
            }
            return get_node_text(xpath_nodeset_get(nodeset, 0));
        }
        default:
            return taurus_strdup("");
    }
}

/* ============================================================================
 * OPTIMIZED: Direct text access for comparisons (NO ALLOCATION)
 * ============================================================================ */

/* Get direct pointer to node's text content WITHOUT allocation
 * Returns: pointer to internal string, or "" if NULL
 * out_len: output length (0 if empty)
 *
 * PERFORMANCE: Used in predicate comparisons to avoid get_node_text() allocation.
 * For attributes, returns direct pointer to value (most common predicate case).
 * For elements, returns pointer to first text child if available.
 */
const char* get_node_text_direct(void* node, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!node) return "";

    TaurusNodeType node_type = XPATH_NODE_TYPE(node);

    switch (node_type) {
        case TAURUS_NODE_ATTRIBUTE: {
            /* OPTIMIZATION: Direct access to attribute value - NO allocation
             * This is the hot path for predicates like [@id='x'] */
            TaurusAttributeNode* attr_node = (TaurusAttributeNode*)node;
            const char* val = attr_node->value ? attr_node->value : "";
            if (out_len) *out_len = strlen(val);
            return val;
        }

        case TAURUS_NODE_ELEMENT: {
            /* For elements, we need to get text content.
             * Use taurus_text() which returns concatenated text content.
             * This is still faster than allocating via get_node_text(). */
            TaurusElement element = (TaurusElement)node;
            const char* text = taurus_text(element);
            if (text) {
                if (out_len) *out_len = strlen(text);
                return text;
            }
            return "";
        }

        default:
            return "";
    }
}

/* Fast comparison of nodeset's string value with a literal (NO ALLOCATION)
 * PERFORMANCE: O(1) length check + O(n) memcmp for single-node nodesets
 * Returns: 1 if equal, 0 if not equal
 */
int xpath_nodeset_equals_string(XPathNodeSet* nodeset, const char* str, size_t str_len) {
    if (!nodeset || xpath_nodeset_count(nodeset) == 0) {
        return (str == NULL || str_len == 0) ? 1 : 0;
    }

    /* Get first node's text value directly */
    void* first = xpath_nodeset_get(nodeset, 0);
    size_t node_len;
    const char* node_str = get_node_text_direct(first, &node_len);

    /* Quick length check first - this eliminates most comparisons */
    if (node_len != str_len) {
        return 0;
    }

    /* Compare strings */
    if (node_len == 0) {
        return 1;  /* Both empty */
    }

    return (memcmp(node_str, str, str_len) == 0) ? 1 : 0;
}

/* Fast check if nodeset's string value matches a boolean (NO ALLOCATION)
 * PERFORMANCE: Just checks if nodeset is non-empty
 * Returns: 1 if nodeset is non-empty (truthy), 0 if empty
 */
int xpath_nodeset_to_boolean(XPathNodeSet* nodeset) {
    if (!nodeset) return 0;
    return xpath_nodeset_count(nodeset) > 0 ? 1 : 0;
}