/* evaluator_types.c - XPath type conversion functions
 * Copyright (c) 2024, Ribose Inc.
 *
 * Type conversions per XPath 1.0 specification Section 4
 */

#include "evaluator_internal.h"
#include "../leptris_internal.h"
#include "../dom/element.h"
#include "../dom/document_node.h"  /* For LeptrisElement structure */
#include "../dom/text.h"
#include "../dom/comment.h"
#include "../dom/cdata.h"
#include "../dom/pi.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * Type Conversions (XPath 1.0 Spec Section 4)
 * ============================================================================ */

/* Get the XPath string-value of a node (all node kinds). Returns a
 * malloc'd string the caller owns. */
char* get_node_text(void* node) {
    if (!node) return leptris_strdup("");

    /* Unified tag space (issue #477): real DOM nodes carry public
     * LeptrisNodeKind values, synthetic nodes carry 6/7/8. */
    int node_type = (int)XPATH_NODE_TYPE(node);

    switch (node_type) {
        case LEPTRIS_NODE_ELEMENT: {
            LeptrisElement element = (LeptrisElement)node;

            /* Use the New DOM text extraction function */
            return leptris_element_get_text_content(element);
        }

        case LEPTRIS_NODE_ATTRIBUTE: {
            LeptrisAttributeNode* attr_node = (LeptrisAttributeNode*)node;
            return leptris_strdup(attr_node->value ? attr_node->value : "");
        }

        case LEPTRIS_NODE_NAMESPACE: {
            /* The string-value of a namespace node is its URI. */
            LeptrisNamespaceNode* ns = (LeptrisNamespaceNode*)node;
            return leptris_strdup(ns->uri ? ns->uri : "");
        }

        case LEPTRIS_NODE_TYPE_DOCUMENT: {
            struct leptris_document* d =
                ((LeptrisDocumentNode*)node)->doc;
            LeptrisElement root = (LeptrisElement)d->new_dom_root;
            if (!root) root = d->root;
            if (!root) return leptris_strdup("");
            return leptris_element_get_text_content(root);
        }

        case LEPTRIS_NODE_TYPE_TEXT: {
            const char* s =
                leptris_text_get_content((LeptrisTextNode*)node);
            return leptris_strdup(s ? s : "");
        }

        case LEPTRIS_NODE_TYPE_CDATA: {
            const char* s =
                leptris_cdata_get_content((LeptrisCDATANode*)node);
            return leptris_strdup(s ? s : "");
        }

        case LEPTRIS_NODE_TYPE_COMMENT: {
            const char* s =
                leptris_comment_get_content((LeptrisCommentNode*)node);
            return leptris_strdup(s ? s : "");
        }

        case LEPTRIS_NODE_TYPE_PI: {
            const char* s = leptris_pi_get_data((LeptrisPINode*)node);
            return leptris_strdup(s ? s : "");
        }

        case LEPTRIS_NODE_TEXT: {
            XPathTextNode* text = (XPathTextNode*)node;
            return leptris_strdup(text->content ? text->content : "");
        }

        default:
            return leptris_strdup("");
    }
}

int xpath_to_boolean(struct leptris_xpath_result* result) {
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

double xpath_to_number(struct leptris_xpath_result* result) {
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

            /* Per W3C XPath 1.0 Section 4.4: empty string converts to 0 */
            if (*str == '\0') return 0.0;

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
                LEPTRIS_FREE(str);
                return NAN;
            }

            char* endptr;
            double value = strtod(p, &endptr);
            while (isspace((unsigned char)*endptr)) endptr++;

            int valid = (*endptr == '\0');
            LEPTRIS_FREE(str);
            return valid ? value : NAN;
        }
        default:
            return NAN;
    }
}

char* xpath_to_string(struct leptris_xpath_result* result) {
    if (!result) return leptris_strdup("");

    switch (result->type) {
        case XPATH_RESULT_STRING:
            return leptris_strdup(result->value.string_value ?
                               result->value.string_value : "");
        case XPATH_RESULT_NUMBER: {
            char buf[64];
            double num = result->value.number_value;
            if (isnan(num)) {
                return leptris_strdup("NaN");
            } else if (isinf(num)) {
                return leptris_strdup(num > 0 ? "Infinity" : "-Infinity");
            } else {
                snprintf(buf, sizeof(buf), "%g", num);
                return leptris_strdup(buf);
            }
        }
        case XPATH_RESULT_BOOLEAN:
            return leptris_strdup(result->value.boolean_value ? "true" : "false");
        case XPATH_RESULT_NODESET: {
            XPathNodeSet* nodeset = result->value.nodeset_value;
            if (!nodeset || xpath_nodeset_count(nodeset) == 0) {
                return leptris_strdup("");
            }
            return get_node_text(xpath_nodeset_get(nodeset, 0));
        }
        default:
            return leptris_strdup("");
    }
}