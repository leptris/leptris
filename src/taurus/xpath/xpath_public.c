/* xpath/xpath_public.c — Public XPath API wrappers.
 *
 * Extracted from taurus.c (TODO 42 phase 2). These are the public-facing
 * functions: taurus_xpath_eval, taurus_xpath_result_*, and the
 * taurus_xpath_variable_set_* helpers.
 */

#include "../include/taurus.h"
#include "../taurus_internal.h"
#include "parser.h"
#include "evaluator.h"
#include "xpath_variables.h"
#include "../dom/element.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

TAURUS_API TaurusXPathResult taurus_xpath_eval(
    TaurusDocument doc,
    TaurusElement context,
    const char* expression
) {
    if (!doc || !expression) return NULL;

    /* Use context element if provided, otherwise use root */
    TaurusElement context_elem = context ? context : taurus_document_root(doc);
    if (!context_elem) return NULL;

    /* Call internal implementation with string length */
    size_t expr_len = strlen(expression);

    /* AST cache (TODO 113 perf): repeated evaluations of the same
     * expression skip the parse phase. 16-entry LRU keyed by FNV-1a
     * hash of the expression string. For single-threaded use; the
     * race on concurrent first-insert is benign (worst case is a
     * duplicate parse, then last-writer-wins on the cache slot).
     *
     * Ownership: the cache owns the AST after first parse. Subsequent
     * calls borrow it for the duration of one evaluate; never free. */
    XPathASTNode* ast = xpath_ast_cache_lookup(expression, expr_len);

    if (!ast) {
        XPathParser* parser = xpath_parser_new(expression, expr_len);
        if (!parser) return NULL;

        ast = xpath_parse(parser);
        const char* parse_error = xpath_parser_error(parser);

        if (!ast || parse_error) {
            xpath_parser_free(parser);
            return NULL;
        }

        xpath_parser_free(parser);

        /* Hand ownership to the cache. The cache returns the same
         * pointer on subsequent lookups. */
        xpath_ast_cache_insert(expression, expr_len, ast);
    }

    /* Create evaluation context with TaurusElement directly - NO CONVERSION! */
    XPathContext* xpath_ctx = xpath_context_new(doc, context_elem);
    if (!xpath_ctx) {
        /* Don't free ast — owned by the cache. */
        return NULL;
    }

    /* Evaluate expression */
    struct taurus_xpath_result* result = xpath_evaluate(xpath_ctx, ast);

    /* Check for evaluation errors */
    const char* eval_error = xpath_context_error(xpath_ctx);
    if (eval_error && !result) {
        /* Error already set in context */
    }

    /* Cleanup. ast is owned by the cache — never free here. */
    xpath_context_free(xpath_ctx);

    return result;
}

TAURUS_API TaurusXPathResultType taurus_xpath_result_type(TaurusXPathResult result) {
    if (!result) return TAURUS_XPATH_STRING;  /* Default to string for NULL */

    /* Map internal XPathResultType to public TaurusXPathResultType */
    switch (result->type) {
        case XPATH_RESULT_NODESET:
            return TAURUS_XPATH_NODESET;
        case XPATH_RESULT_BOOLEAN:
            return TAURUS_XPATH_BOOLEAN;
        case XPATH_RESULT_NUMBER:
            return TAURUS_XPATH_NUMBER;
        case XPATH_RESULT_STRING:
            return TAURUS_XPATH_STRING;
        default:
            return TAURUS_XPATH_STRING;
    }
}

TAURUS_API size_t taurus_xpath_result_count(TaurusXPathResult result) {
    if (!result || result->type != XPATH_RESULT_NODESET) return 0;
    return result->value.nodeset_value ? result->value.nodeset_value->count : 0;
}

TAURUS_API TaurusElement taurus_xpath_result_get(TaurusXPathResult result, size_t index) {
    if (!result || result->type != XPATH_RESULT_NODESET) return NULL;
    if (!result->value.nodeset_value || index >= result->value.nodeset_value->count) return NULL;

    void* node = result->value.nodeset_value->nodes[index];

    /* SAFETY: Validate node pointer before returning
     * Stale pointers from previous operations can cause crashes */
    if ((uintptr_t)node < 0x1000) {
        return NULL;  /* Clearly invalid pointer */
    }

    /* Additional safety: check if node type field is valid */
    TaurusNode* typed_node = (TaurusNode*)node;
    if (typed_node->type < TAURUS_NODE_TYPE_ELEMENT ||
        typed_node->type > TAURUS_NODE_TYPE_DOCTYPE) {
        return NULL;  /* Invalid type field - likely stale pointer */
    }

    return (TaurusElement)node;
}

TAURUS_API int taurus_xpath_result_boolean(TaurusXPathResult result) {
    if (!result) return 0;

    switch (result->type) {
        case XPATH_RESULT_BOOLEAN:
            return result->value.boolean_value ? 1 : 0;
        case XPATH_RESULT_NUMBER:
            return (result->value.number_value != 0.0 && !isnan(result->value.number_value)) ? 1 : 0;
        case XPATH_RESULT_STRING:
            return (result->value.string_value && result->value.string_value[0] != '\0') ? 1 : 0;
        case XPATH_RESULT_NODESET:
            return (result->value.nodeset_value && result->value.nodeset_value->count > 0) ? 1 : 0;
        default:
            return 0;
    }
}

TAURUS_API double taurus_xpath_result_number(TaurusXPathResult result) {
    if (!result) return NAN;

    switch (result->type) {
        case XPATH_RESULT_NUMBER:
            return result->value.number_value;
        case XPATH_RESULT_BOOLEAN:
            return result->value.boolean_value ? 1.0 : 0.0;
        case XPATH_RESULT_STRING:
            if (!result->value.string_value || result->value.string_value[0] == '\0') {
                return NAN;
            }
            {
                char* endptr;
                double val = strtod(result->value.string_value, &endptr);
                return (endptr == result->value.string_value || *endptr != '\0') ? (NAN) : val;
            }
        case XPATH_RESULT_NODESET:
            if (result->value.nodeset_value && result->value.nodeset_value->count > 0) {
                TaurusElement elem = (TaurusElement)result->value.nodeset_value->nodes[0];
                const char* text = taurus_element_text(elem);
                if (text && text[0] != '\0') {
                    char* endptr;
                    double val = strtod(text, &endptr);
                    return (endptr == text || *endptr != '\0') ? (NAN) : val;
                }
            }
            return NAN;
        default:
            return NAN;
    }
}

TAURUS_API char* taurus_xpath_result_string(TaurusXPathResult result) {
    if (!result) return taurus_strdup("");

    switch (result->type) {
        case XPATH_RESULT_STRING:
            return result->value.string_value ? taurus_strdup(result->value.string_value) : taurus_strdup("");
        case XPATH_RESULT_BOOLEAN:
            return taurus_strdup(result->value.boolean_value ? "true" : "false");
        case XPATH_RESULT_NUMBER:
            {
                char buffer[64];
                double num = result->value.number_value;

                if (isnan(num)) {
                    return taurus_strdup("NaN");
                } else if (isinf(num)) {
                    return taurus_strdup(num > 0 ? "Infinity" : "-Infinity");
                } else if (num == (long)num) {
                    snprintf(buffer, sizeof(buffer), "%ld", (long)num);
                } else {
                    snprintf(buffer, sizeof(buffer), "%g", num);
                }
                return taurus_strdup(buffer);
            }
        case XPATH_RESULT_NODESET:
            if (result->value.nodeset_value && result->value.nodeset_value->count > 0) {
                const char* text = taurus_element_text((TaurusElement)result->value.nodeset_value->nodes[0]);
                return text ? taurus_strdup(text) : taurus_strdup("");
            }
            /* fallthrough */
        default:
            return taurus_strdup("");
    }
}

TAURUS_API void taurus_xpath_result_free(struct taurus_xpath_result* result) {
    if (!result) return;
    xpath_result_free(result);
}

TAURUS_API TaurusXPathVariableSet taurus_xpath_variable_set_new(void) {
    return (TaurusXPathVariableSet)xpath_variable_set_new();
}

TAURUS_API void taurus_xpath_variable_set_free(TaurusXPathVariableSet set) {
    xpath_variable_set_free((XPathVariableSet*)set);
}

TAURUS_API TaurusStatus taurus_xpath_variable_set_boolean(TaurusXPathVariableSet set, const char* name, int value) {
    if (!set || !name) return TAURUS_ERROR_NULL_ARG;

    XPathVariable* var = xpath_variable_set_add((XPathVariableSet*)set, name, XPATH_VAR_TYPE_BOOLEAN);
    if (!var) return TAURUS_ERROR_MEMORY;

    if (!xpath_variable_set_boolean(var, value)) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    return TAURUS_OK;
}

TAURUS_API TaurusStatus taurus_xpath_variable_set_number(TaurusXPathVariableSet set, const char* name, double value) {
    if (!set || !name) return TAURUS_ERROR_NULL_ARG;

    XPathVariable* var = xpath_variable_set_add((XPathVariableSet*)set, name, XPATH_VAR_TYPE_NUMBER);
    if (!var) return TAURUS_ERROR_MEMORY;

    if (!xpath_variable_set_number(var, value)) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    return TAURUS_OK;
}

TAURUS_API TaurusStatus taurus_xpath_variable_set_string(TaurusXPathVariableSet set, const char* name, const char* value) {
    if (!set || !name) return TAURUS_ERROR_NULL_ARG;

    XPathVariable* var = xpath_variable_set_add((XPathVariableSet*)set, name, XPATH_VAR_TYPE_STRING);
    if (!var) return TAURUS_ERROR_MEMORY;

    if (!xpath_variable_set_string(var, value)) {
        return TAURUS_ERROR_MEMORY;
    }

    return TAURUS_OK;
}

TAURUS_API TaurusXPathResult taurus_xpath_eval_with_vars(
    TaurusDocument doc,
    const char* expression,
    TaurusXPathVariableSet variables)
{
    if (!doc || !expression) {
        return NULL;
    }

    /* Parse XPath expression */
    XPathParser* parser = xpath_parser_new(expression, strlen(expression));
    if (!parser) return NULL;

    XPathASTNode* ast = xpath_parse(parser);
    const char* parse_error = xpath_parser_error(parser);

    if (!ast || parse_error) {
        xpath_parser_free(parser);
        return NULL;
    }

    xpath_parser_free(parser);

    /* Create evaluation context with TaurusElement directly - NO CONVERSION! */
    XPathContext* xpath_ctx = xpath_context_new(doc, taurus_document_root(doc));
    if (!xpath_ctx) {
        ast_node_free(ast);
        return NULL;
    }

    /* Set variable set in context */
    xpath_ctx->variable_set = variables;

    /* Evaluate expression */
    struct taurus_xpath_result* result = xpath_evaluate(xpath_ctx, ast);

    /* Check for evaluation errors */
    const char* eval_error = xpath_context_error(xpath_ctx);
    if (eval_error && !result) {
        /* Error already set in context */
    }

    /* Cleanup */
    xpath_context_free(xpath_ctx);
    ast_node_free(ast);

    return result;
}
