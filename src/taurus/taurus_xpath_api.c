/* taurus_xpath_api.c - Taurus XPath API
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C XML parser and XPath evaluator - XPath API.
 *
 * PERFORMANCE: Optional fast path for common XPath patterns.
 * Enable with TAURUS_ENABLE_XPATH_FAST_PATH=1 at compile time.
 */

#include "../include/taurus.h"
#include "taurus_internal.h"
#include "xpath/parser.h"
#include "xpath/evaluator.h"
#include "xpath/xpath_variables.h"
#include "xpath/xpath_fast_path.h"
#include "dom/element.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* Fast path can be enabled at compile time for performance testing */
#ifndef TAURUS_ENABLE_XPATH_FAST_PATH
#define TAURUS_ENABLE_XPATH_FAST_PATH 1
#endif

/* ============================================================================
 * XPath Evaluation
 * ============================================================================ */

/**
 * Evaluate XPath expression against document (Public API - 3 parameter version)
 *
 * PERFORMANCE: Optional fast path detection for common patterns.
 * When enabled, provides 10-50x speedup for simple expressions.
 */
TAURUS_API TaurusXPathResult taurus_xpath_eval(
    TaurusDocument doc,
    TaurusElement context,
    const char* expression
) {
    if (!doc || !expression) return NULL;

    TaurusElement context_elem = context ? context : taurus_document_root(doc);
    if (!context_elem) return NULL;

    size_t expr_len = strlen(expression);

#if TAURUS_ENABLE_XPATH_FAST_PATH
    /* PERFORMANCE: Try fast path first for common patterns */
    struct taurus_xpath_result* fast_result = xpath_fast_path_try(
        doc, context_elem, expression, expr_len);
    if (fast_result) {
        return fast_result;
    }
#endif

    XPathParser* parser = xpath_parser_new(expression, expr_len);
    if (!parser) return NULL;

    XPathASTNode* ast = xpath_parse(parser);
    const char* parse_error = xpath_parser_error(parser);

    if (!ast || parse_error) {
        xpath_parser_free(parser);
        return NULL;
    }

    xpath_parser_free(parser);

    XPathContext* xpath_ctx = xpath_context_new(doc, context_elem);
    if (!xpath_ctx) {
        ast_node_free(ast);
        return NULL;
    }

    struct taurus_xpath_result* result = xpath_evaluate(xpath_ctx, ast);

    const char* eval_error = xpath_context_error(xpath_ctx);
    if (eval_error && !result) {
        /* Error already set in context */
    }

    xpath_context_free(xpath_ctx);
    ast_node_free(ast);

    return result;
}

/**
 * Get XPath result type (Public API wrapper)
 */
TAURUS_API TaurusXPathResultType taurus_xpath_result_type(TaurusXPathResult result) {
    if (!result) return TAURUS_XPATH_STRING;

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

/**
 * Get nodeset size (Public API wrapper)
 */
TAURUS_API size_t taurus_xpath_result_count(TaurusXPathResult result) {
    if (!result || result->type != XPATH_RESULT_NODESET) return 0;
    return result->value.nodeset_value ? result->value.nodeset_value->count : 0;
}

/**
 * Get node from nodeset by index (Public API wrapper)
 */
TAURUS_API TaurusElement taurus_xpath_result_get(TaurusXPathResult result, size_t index) {
    if (!result || result->type != XPATH_RESULT_NODESET) return NULL;
    if (!result->value.nodeset_value || index >= result->value.nodeset_value->count) return NULL;

    void* node = result->value.nodeset_value->nodes[index];

    if ((uintptr_t)node < 0x1000) {
        return NULL;
    }

    TaurusNode* typed_node = (TaurusNode*)node;
    if (typed_node->type < TAURUS_NODE_TYPE_ELEMENT ||
        typed_node->type > TAURUS_NODE_TYPE_DOCTYPE) {
        return NULL;
    }

    return (TaurusElement)node;
}

/**
 * Get boolean value (Public API wrapper)
 */
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

/**
 * Get number value (Public API wrapper)
 */
TAURUS_API double taurus_xpath_result_number(TaurusXPathResult result) {
    if (!result) return 0.0 / 0.0;  /* NaN */

    switch (result->type) {
        case XPATH_RESULT_NUMBER:
            return result->value.number_value;
        case XPATH_RESULT_BOOLEAN:
            return result->value.boolean_value ? 1.0 : 0.0;
        case XPATH_RESULT_STRING:
            if (!result->value.string_value || result->value.string_value[0] == '\0') {
                return 0.0 / 0.0;
            }
            {
                char* endptr;
                double val = strtod(result->value.string_value, &endptr);
                return (endptr == result->value.string_value || *endptr != '\0') ? (0.0 / 0.0) : val;
            }
        case XPATH_RESULT_NODESET:
            if (result->value.nodeset_value && result->value.nodeset_value->count > 0) {
                TaurusElement elem = (TaurusElement)result->value.nodeset_value->nodes[0];
                const char* text = taurus_element_text(elem);
                if (text && text[0] != '\0') {
                    char* endptr;
                    double val = strtod(text, &endptr);
                    return (endptr == text || *endptr != '\0') ? (0.0 / 0.0) : val;
                }
            }
            return 0.0 / 0.0;
        default:
            return 0.0 / 0.0;
    }
}

/**
 * Get string value (Public API wrapper)
 */
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
            return taurus_strdup("");
        default:
            return taurus_strdup("");
    }
}

/**
 * Free XPath result
 */
TAURUS_API void taurus_xpath_result_free(struct taurus_xpath_result* result) {
    xpath_result_free(result);
}

/* ============================================================================
 * XPath Variables
 * ============================================================================ */

/**
 * Create a new variable set
 */
TAURUS_API TaurusXPathVariableSet taurus_xpath_variable_set_new(void) {
    return (TaurusXPathVariableSet)xpath_variable_set_new();
}

/**
 * Free a variable set
 */
TAURUS_API void taurus_xpath_variable_set_free(TaurusXPathVariableSet set) {
    xpath_variable_set_free((XPathVariableSet*)set);
}

/**
 * Add a boolean variable to the set
 */
TAURUS_API TaurusStatus taurus_xpath_variable_set_boolean(TaurusXPathVariableSet set, const char* name, int value) {
    if (!set || !name) return TAURUS_ERROR_NULL_ARG;

    XPathVariable* var = xpath_variable_set_add((XPathVariableSet*)set, name, XPATH_VAR_TYPE_BOOLEAN);
    if (!var) return TAURUS_ERROR_MEMORY;

    if (!xpath_variable_set_boolean(var, value)) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    return TAURUS_OK;
}

/**
 * Add a number variable to the set
 */
TAURUS_API TaurusStatus taurus_xpath_variable_set_number(TaurusXPathVariableSet set, const char* name, double value) {
    if (!set || !name) return TAURUS_ERROR_NULL_ARG;

    XPathVariable* var = xpath_variable_set_add((XPathVariableSet*)set, name, XPATH_VAR_TYPE_NUMBER);
    if (!var) return TAURUS_ERROR_MEMORY;

    if (!xpath_variable_set_number(var, value)) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    return TAURUS_OK;
}

/**
 * Add a string variable to the set
 */
TAURUS_API TaurusStatus taurus_xpath_variable_set_string(TaurusXPathVariableSet set, const char* name, const char* value) {
    if (!set || !name) return TAURUS_ERROR_NULL_ARG;

    XPathVariable* var = xpath_variable_set_add((XPathVariableSet*)set, name, XPATH_VAR_TYPE_STRING);
    if (!var) return TAURUS_ERROR_MEMORY;

    if (!xpath_variable_set_string(var, value)) {
        return TAURUS_ERROR_MEMORY;
    }

    return TAURUS_OK;
}

/**
 * Evaluate XPath expression with variables
 */
TAURUS_API TaurusXPathResult taurus_xpath_eval_with_vars(
    TaurusDocument doc,
    const char* expression,
    TaurusXPathVariableSet variables)
{
    if (!doc || !expression) {
        return NULL;
    }

    XPathParser* parser = xpath_parser_new(expression, strlen(expression));
    if (!parser) return NULL;

    XPathASTNode* ast = xpath_parse(parser);
    const char* parse_error = xpath_parser_error(parser);

    if (!ast || parse_error) {
        xpath_parser_free(parser);
        return NULL;
    }

    xpath_parser_free(parser);

    XPathContext* xpath_ctx = xpath_context_new(doc, taurus_document_root(doc));
    if (!xpath_ctx) {
        ast_node_free(ast);
        return NULL;
    }

    xpath_ctx->variable_set = variables;

    struct taurus_xpath_result* result = xpath_evaluate(xpath_ctx, ast);

    const char* eval_error = xpath_context_error(xpath_ctx);
    if (eval_error && !result) {
        /* Error already set in context */
    }

    xpath_context_free(xpath_ctx);
    ast_node_free(ast);

    return result;
}

/* ============================================================================
 * XPath Pre-Compilation API - Faster repeated evaluation
 * ============================================================================ */

/**
 * Compile XPath expression for faster repeated evaluation
 *
 * PERFORMANCE: Pre-compile once, evaluate many times.
 * Provides 10-50x speedup for repeated evaluations of the same expression.
 */
TAURUS_API TaurusXPathCompiled taurus_xpath_compile(const char* expression) {
    if (!expression) return NULL;

    size_t expr_len = strlen(expression);

    /* Allocate compiled expression structure */
    struct taurus_xpath_compiled* compiled = TAURUS_ALLOC(struct taurus_xpath_compiled);
    if (!compiled) return NULL;

    compiled->ast = NULL;
    compiled->expression = NULL;
    compiled->error_msg[0] = '\0';
    compiled->is_literal = 0;
    compiled->cached_result = NULL;

    /* Store expression string for debugging */
    compiled->expression = taurus_strdup(expression);
    if (!compiled->expression) {
        TAURUS_FREE(compiled);
        return NULL;
    }

    /* Parse expression into AST */
    XPathParser* parser = xpath_parser_new(expression, expr_len);
    if (!parser) {
        TAURUS_FREE(compiled->expression);
        TAURUS_FREE(compiled);
        return NULL;
    }

    compiled->ast = xpath_parse(parser);
    const char* parse_error = xpath_parser_error(parser);

    if (!compiled->ast || parse_error) {
        if (parse_error) {
            strncpy(compiled->error_msg, parse_error, sizeof(compiled->error_msg) - 1);
        }
        xpath_parser_free(parser);
        TAURUS_FREE(compiled->expression);
        TAURUS_FREE(compiled);
        return NULL;
    }

    xpath_parser_free(parser);

    /* PERFORMANCE: Detect literal expressions (constant-folded) and pre-compute result */
    if (compiled->ast->type == XPATH_AST_STRING) {
        compiled->is_literal = 1;
        compiled->cached_result = TAURUS_ALLOC(struct taurus_xpath_result);
        if (compiled->cached_result) {
            compiled->cached_result->type = XPATH_RESULT_STRING;
            compiled->cached_result->value.string_value = taurus_strdup(compiled->ast->value);
            if (!compiled->cached_result->value.string_value) {
                TAURUS_FREE(compiled->cached_result);
                compiled->is_literal = 0;
            }
        }
    } else if (compiled->ast->type == XPATH_AST_NUMBER) {
        compiled->is_literal = 1;
        compiled->cached_result = TAURUS_ALLOC(struct taurus_xpath_result);
        if (compiled->cached_result) {
            compiled->cached_result->type = XPATH_RESULT_NUMBER;
            compiled->cached_result->value.number_value = compiled->ast->number_value;
        }
    }

    return compiled;
}

/**
 * Evaluate compiled XPath expression
 */
TAURUS_API TaurusXPathResult taurus_xpath_eval_compiled(
    TaurusDocument doc,
    TaurusElement context,
    TaurusXPathCompiled compiled
) {
    if (!doc || !compiled || !compiled->ast) return NULL;

    /* PERFORMANCE: Return cached result for literal expressions */
    if (compiled->is_literal && compiled->cached_result) {
        /* Return a copy of the cached result (caller will free it) */
        struct taurus_xpath_result* result = TAURUS_ALLOC(struct taurus_xpath_result);
        if (!result) return NULL;

        *result = *compiled->cached_result;
        if (result->type == XPATH_RESULT_STRING && compiled->cached_result->value.string_value) {
            result->value.string_value = taurus_strdup(compiled->cached_result->value.string_value);
        }
        return result;
    }

    TaurusElement context_elem = context ? context : taurus_document_root(doc);
    if (!context_elem) return NULL;

    /* Create context and evaluate */
    XPathContext* xpath_ctx = xpath_context_new(doc, context_elem);
    if (!xpath_ctx) return NULL;

    struct taurus_xpath_result* result = xpath_evaluate(xpath_ctx, compiled->ast);

    xpath_context_free(xpath_ctx);
    return result;
}

/**
 * Evaluate compiled XPath expression with variables
 */
TAURUS_API TaurusXPathResult taurus_xpath_eval_compiled_with_vars(
    TaurusDocument doc,
    TaurusXPathCompiled compiled,
    TaurusXPathVariableSet variables
) {
    if (!doc || !compiled || !compiled->ast) return NULL;

    /* Create context and evaluate */
    XPathContext* xpath_ctx = xpath_context_new(doc, taurus_document_root(doc));
    if (!xpath_ctx) return NULL;

    xpath_ctx->variable_set = variables;

    struct taurus_xpath_result* result = xpath_evaluate(xpath_ctx, compiled->ast);

    xpath_context_free(xpath_ctx);
    return result;
}

/**
 * Free compiled XPath expression
 */
TAURUS_API void taurus_xpath_compiled_free(TaurusXPathCompiled compiled) {
    if (!compiled) return;

    if (compiled->ast) {
        ast_node_free(compiled->ast);
    }
    if (compiled->expression) {
        TAURUS_FREE(compiled->expression);
    }
    if (compiled->cached_result) {
        if (compiled->cached_result->type == XPATH_RESULT_STRING &&
            compiled->cached_result->value.string_value) {
            TAURUS_FREE(compiled->cached_result->value.string_value);
        }
        TAURUS_FREE(compiled->cached_result);
    }
    TAURUS_FREE(compiled);
}
