/* evaluator_types.c - XPath type conversion functions
 * Copyright (c) 2024, Ribose Inc.
 *
 * Type conversions per XPath 1.0 specification Section 4
 */

#include "evaluator_internal.h"
#include "../taurus_internal.h"
#include "../dom/element.h"  /* For TaurusElement structure */
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