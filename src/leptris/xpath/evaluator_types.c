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
#include <limits.h>

/* ============================================================================
 * Type Conversions (XPath 1.0 Spec Section 4)
 * ============================================================================ */

/* Number→string, libxml2 xmlXPathFormatNumber parity (the libxslt
 * suite's ground truth): int32-integral values print bare; other
 * values with magnitude in [1e-5, 1e9] print decimal with 15
 * significant digits, trailing zeros trimmed; everything else
 * prints scientific (%.14e, mantissa zeros trimmed, exponent kept).
 * Returns a malloc'd string. */
char* xpath_number_to_string(double number) {
    if (isnan(number)) return leptris_strdup("NaN");
    if (isinf(number))
        return leptris_strdup(number > 0 ? "Infinity" : "-Infinity");
    if (number == 0.0) return leptris_strdup("0");
    if (number > (double)INT_MIN && number < (double)INT_MAX &&
        number == (double)(int)number) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", (int)number);
        return leptris_strdup(buf);
    }

    char work[64];
    int size;
    double absolute_value = fabs(number);
    if (absolute_value > 1e9 || absolute_value < 1e-5) {
        /* Scientific: %21.14e (the width only pads; spaces stripped
         * below), then size stops AT the 'e' so the trim keeps it. */
        size = (int)snprintf(work, sizeof(work), "%21.14e", number);
        while (size > 0 && work[size] != 'e') size--;
    } else {
        /* Decimal: 15 significant digits — the fraction precision
         * spends the budget left after the integer digits. */
        int integer_place, fraction_place;
        if (absolute_value > 0.0) {
            integer_place = (int)log10(absolute_value);
            fraction_place = integer_place > 0 ? 15 - integer_place - 1
                                               : 15 - integer_place;
        } else {
            fraction_place = 1;
        }
        size = (int)snprintf(work, sizeof(work), "%0.*f",
                             fraction_place, number);
    }

    char* start = work;
    while (*start == ' ') start++;
    /* Trim trailing fraction zeros: for the decimal form the tail is
     * just the NUL; for the scientific form it is the exponent. */
    char* after = work + size;
    char* ptr = after;
    while (ptr > start && *(--ptr) == '0') { }
    if (*ptr != '.') ptr++;
    memmove(ptr, after, strlen(after) + 1);
    return leptris_strdup(start);
}

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
            /* XPath string-value of a document node: ALL text
             * descendants concatenated — the doc-children chain
             * (comments/PIs/text before-after the root) walks first
             * so pure-text RTF fragments (issue #56) string to their
             * text; the root element's subtree follows. */
            size_t cap = 64, len = 0;
            char* acc = (char*)malloc(cap);
            if (!acc) return leptris_strdup("");
            acc[0] = '\0';
            int any_text = 0;
            for (LeptrisNodeRef c = (LeptrisNodeRef)d->doc_children_head;
                 c; c = leptris_node_get_next_sibling(c)) {
                int ty = leptris_node_get_type(c);
                if (ty == LEPTRIS_NODE_TYPE_TEXT ||
                    ty == LEPTRIS_NODE_TYPE_CDATA) {
                    const char* t =
                        leptris_text_get_content((LeptrisTextNode*)c);
                    if (t && *t) {
                        any_text = 1;
                        size_t tl = strlen(t);
                        while (len + tl + 1 > cap) cap *= 2;
                        char* grown = (char*)realloc(acc, cap);
                        if (!grown) { free(acc); return leptris_strdup(""); }
                        acc = grown;
                        memcpy(acc + len, t, tl + 1);
                        len += tl;
                    }
                }
            }
            if (any_text) return acc;
            free(acc);
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
            const char* c = text->content ? text->content : "";
            /* Numeric sequence members carry a "\x03N" marker for
             * per-member type checks (instance of). \x03 is invalid
             * in XML 1.0 text, so it can never collide with real
             * content — strip it for every string consumer. */
            if (c[0] == '\x03' && c[1] == 'N') c += 2;
            return leptris_strdup(c);
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

            /* XPath 1.0 §4.4 number(string): a string that is not
             * optional-whitespace + a valid Number converts to NaN —
             * the empty string included (bug-61: number('') is NaN;
             * the old 0.0 misread the spec, masked by the
             * interpreter-only path). */
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
            return xpath_number_to_string(result->value.number_value);
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