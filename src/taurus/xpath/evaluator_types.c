/* evaluator_types.c - XPath type conversion functions
 * Copyright (c) 2024, Ribose Inc.
 *
 * Type conversions per XPath 1.0 specification Section 4
 */

#include "evaluator_internal.h"
#include "../taurus_internal.h"
#include "../dom/element.h"  /* For TaurusElement structure */
#include "../dom/ptr_element.h"  /* For direct struct access in hash function */
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

/* ============================================================================
 * HASH-BASED OPTIMIZATION (libxml2 strategy)
 * ============================================================================ */

/* Fast hash of string - uses first two characters only (like libxml2)
 * PERFORMANCE: O(1) - just two character accesses
 * Returns: Hash value, or 0 if string is NULL or empty
 *
 * This matches libxml2's xmlXPathStringHash() approach:
 * hash = string[0] + (string[1] << 8)
 */
static inline unsigned int xpath_fast_hash(const char* str) {
    if (!str || str[0] == '\0') return 0;
    if (str[1] == '\0') return (unsigned char)str[0];
    return (unsigned char)str[0] + ((unsigned char)str[1] << 8);
}

/* Fast hash of node's text content - NO ALLOCATION
 * PERFORMANCE: O(1) - direct access to first chars of text content
 * Returns: Hash value based on first two characters of node's string value
 *
 * This matches libxml2's xmlXPathNodeValHash() approach.
 * For attributes: uses attribute value directly
 * For elements: uses first text child's content (no concatenation!)
 */
unsigned int xpath_node_val_hash(void* node) {
    if (!node) return 0;

    TaurusNodeType node_type = XPATH_NODE_TYPE(node);

    switch (node_type) {
        case TAURUS_NODE_ATTRIBUTE: {
            /* Attribute - hash the value directly */
            TaurusAttributeNode* attr_node = (TaurusAttributeNode*)node;
            return xpath_fast_hash(attr_node->value);
        }

        case TAURUS_NODE_ELEMENT: {
            /* Element - hash the FIRST text child's content only
             * PERFORMANCE: Don't use taurus_text() - it concatenates!
             * Just look at first text node for quick hash */
            TaurusElement element = (TaurusElement)node;
            struct ptr_element* elem = (struct ptr_element*)element;

            /* Check if element has children */
            if (!elem->first_child) return 0;

            /* Look for first text node among children */
            /* Note: In our structure, text nodes come before element children */
            struct ptr_node* child = (struct ptr_node*)elem->first_child;
            while (child) {
                if (child->type == PTR_NODE_TYPE_TEXT) {
                    struct ptr_text* text_node = (struct ptr_text*)child;
                    return xpath_fast_hash(text_node->text);
                }
                /* Move to next sibling - but only check first level */
                child = (struct ptr_node*)child->u.elem.next_sibling;
            }
            return 0;
        }

        default:
            return 0;
    }
}

/* Hash-based nodeset-string comparison with early exit (libxml2 strategy)
 * PERFORMANCE: O(1) hash comparison before O(n) string comparison
 *
 * Algorithm:
 * 1. Compute hash of target string once
 * 2. For each node in nodeset:
 *    a. Compute node's hash (O(1))
 *    b. If hashes match, do full comparison
 *    c. If hashes don't match, skip (early exit!)
 *
 * Returns: 1 if any node matches, 0 if none match
 */
int xpath_nodeset_equals_string_hash(XPathNodeSet* nodeset, const char* str, int neq) {
    if (!nodeset || xpath_nodeset_count(nodeset) == 0) {
        return (str == NULL || str[0] == '\0') ? (neq ? 0 : 1) : (neq ? 1 : 0);
    }

    /* Compute hash of target string ONCE */
    unsigned int target_hash = xpath_fast_hash(str);
    size_t str_len = str ? strlen(str) : 0;

    /* Check each node using hash-based early exit */
    size_t count = xpath_nodeset_count(nodeset);
    for (size_t i = 0; i < count; i++) {
        void* node = xpath_nodeset_get(nodeset, i);

        /* Fast hash comparison - eliminates most non-matches */
        unsigned int node_hash = xpath_node_val_hash(node);

        if (node_hash != target_hash) {
            /* Hashes don't match - quick rejection */
            if (neq) {
                return 1;  /* Not equal for != operator */
            }
            continue;  /* Try next node for = operator */
        }

        /* Hashes match - do full comparison */
        size_t node_len;
        const char* node_str = get_node_text_direct(node, &node_len);

        if (node_len != str_len) {
            if (neq) {
                return 1;  /* Lengths differ, so not equal */
            }
            continue;
        }

        if (node_len == 0 || memcmp(node_str, str, str_len) == 0) {
            /* Strings match */
            return neq ? 0 : 1;
        } else if (neq) {
            return 1;  /* Strings differ, so != is true */
        }
    }

    /* No match found */
    return neq ? 1 : 0;
}