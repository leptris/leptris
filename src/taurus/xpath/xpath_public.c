/* xpath/xpath_public.c — Public XPath API wrappers.
 *
 * Extracted from taurus.c (TODO 42 phase 2). These are the public-facing
 * functions: taurus_xpath_eval, taurus_xpath_result_*, and the
 * taurus_xpath_variable_set_* helpers.
 */

#include "../include/taurus.h"
#include "../taurus_internal.h"
#include "parser.h"
#include "bytecode.h"  /* TODO 120: TaurusXPathBytecode + compile */
#include "evaluator.h"
#include "evaluator_internal.h"  /* TODO 120: taurus_xpath_vm_eval */
#include "functions.h"  /* TODO 148 Phase 5 */
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

    /* FlatDoc fast path removed — direct_parse builds the
     * TaurusElement tree eagerly. XPath always evaluates against
     * the compact-pointer DOM via the bytecode VM. */

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

    /* TODO 120 Phase F: bytecode cache. Compile once per expression,
     * reuse on every subsequent eval. Inline dispatch (AXIS_STEP,
     * BINARY_OP, FUNC_CALL) skips the AST-type switch in
     * evaluate_expr on the hot path. */
    TaurusXPathBytecode* bc = xpath_ast_cache_get_bc(expression, expr_len);
    struct taurus_xpath_result* result = NULL;
    if (bc) {
        result = taurus_xpath_vm_run_bc(bc, xpath_ctx);
    } else {
        /* Compile + run + cache. taurus_xpath_vm_eval compiles and
         * runs but does not cache; we cache explicitly here so the
         * next caller hits the fast path above. */
        bc = taurus_xpath_compile_ast(ast);
        if (bc) {
            result = taurus_xpath_vm_run_bc(bc, xpath_ctx);
            xpath_ast_cache_store_bc(expression, expr_len, bc);
            /* cache now owns bc; do not free here. */
        }
    }
    /* If the VM failed for any reason (e.g., unsupported edge case),
     * fall back to direct AST evaluation. */
    if (!result) {
        result = xpath_evaluate(xpath_ctx, ast);
    }

    /* Cleanup. ast and bc are owned by the cache — never free here. */
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

TAURUS_API size_t taurus_xpath_result_get_nodes(
    TaurusXPathResult result, TaurusElement* out_nodes, size_t max_count) {
    if (!result || !out_nodes || max_count == 0) return 0;
    if (result->type != XPATH_RESULT_NODESET) return 0;
    if (!result->value.nodeset_value) return 0;

    size_t count = result->value.nodeset_value->count;
    if (count > max_count) count = max_count;

    for (size_t i = 0; i < count; i++) {
        out_nodes[i] = (TaurusElement)result->value.nodeset_value->nodes[i];
    }
    return count;
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
    return taurus_xpath_eval_with_vars_context(doc, NULL, expression, variables);
}

TAURUS_API TaurusXPathResult taurus_xpath_eval_with_vars_context(
    TaurusDocument doc,
    TaurusElement context,
    const char* expression,
    TaurusXPathVariableSet variables)
{
    if (!doc || !expression) {
        return NULL;
    }

    /* Resolve context: explicit context if provided, else root. */
    TaurusElement context_elem = context ? context : taurus_document_root(doc);
    if (!context_elem) return NULL;

    /* Parse XPath expression. The variable-bound path is not yet on
     * the AST cache + bytecode fast path (TODO 120 Phase F follow-up);
     * we parse + eval directly. */
    XPathParser* parser = xpath_parser_new(expression, strlen(expression));
    if (!parser) return NULL;

    XPathASTNode* ast = xpath_parse(parser);
    const char* parse_error = xpath_parser_error(parser);

    if (!ast || parse_error) {
        xpath_parser_free(parser);
        return NULL;
    }

    xpath_parser_free(parser);

    /* Create evaluation context with the resolved context element. */
    XPathContext* xpath_ctx = xpath_context_new(doc, context_elem);
    if (!xpath_ctx) {
        ast_node_free(ast);
        return NULL;
    }

    /* Set variable set in context */
    xpath_ctx->variable_set = variables;

    /* Evaluate expression */
    struct taurus_xpath_result* result = xpath_evaluate(xpath_ctx, ast);

    /* Cleanup */
    xpath_context_free(xpath_ctx);
    ast_node_free(ast);

    return result;
}

/* ---- Custom XPath function registration (TODO 148 Phase 5) ----
 *
 * Registered functions live on the document. The evaluator merges
 * them with the standard XPath 1.0 library when building the
 * per-context function registry. Standard functions win name
 * collisions.
 *
 * State is captured via XPathFunctionDef.user_data (set on
 * registration). The evaluator saves/restores that user_data on
 * ctx->current_fn_user_data around each handler invocation, so
 * custom-fn recursion is safe.
 */

/* Per-doc custom function entry. */
struct taurus_custom_xpath_fn {
    char* name;
    TaurusXPathFn fn;
    void* user_data;
    struct taurus_custom_xpath_fn* next;
};

/* Thunk bridging the internal XPathFunctionHandler signature to
 * the public TaurusXPathFn. Each arg AST is evaluated, converted
 * to a string, and passed to the user callback. The callback's
 * returned string is wrapped as a string-typed result.
 *
 * The per-call user_data slot (ctx->current_fn_user_data) carries
 * the (TaurusXPathFn, void* user_data) pair, packed into a small
 * heap struct on registration. */
struct taurus_custom_fn_state {
    TaurusXPathFn fn;
    void* user_data;
};

static struct taurus_xpath_result* custom_xpath_thunk(
    XPathContext* context,
    XPathASTNode** args,
    size_t arg_count) {
    if (!context) return NULL;
    struct taurus_custom_fn_state* state =
        (struct taurus_custom_fn_state*)context->current_fn_user_data;
    if (!state || !state->fn) return NULL;

    extern struct taurus_xpath_result* evaluate_expr(XPathContext*, XPathASTNode*);

    const char** str_args = NULL;
    char** owned = NULL;
    size_t owned_count = 0;
    if (arg_count > 0) {
        str_args = (const char**)calloc(arg_count, sizeof(char*));
        owned = (char**)calloc(arg_count, sizeof(char*));
        if (!str_args || !owned) {
            free(str_args); free(owned);
            return NULL;
        }
    }

    struct taurus_xpath_result* result = NULL;
    for (size_t i = 0; i < arg_count; i++) {
        struct taurus_xpath_result* r = evaluate_expr(context, args[i]);
        if (!r) goto cleanup;
        char* s = xpath_to_string(r);
        xpath_result_free(r);
        if (!s) s = strdup("");
        owned[owned_count++] = s;
        str_args[i] = s;
    }

    char* result_str = state->fn(str_args, (int)arg_count, state->user_data);
    if (result_str) {
        result = xpath_result_new(XPATH_RESULT_STRING);
        if (result) {
            result->value.string_value = result_str;
        } else {
            free(result_str);
        }
    }

cleanup:
    for (size_t i = 0; i < owned_count; i++) free(owned[i]);
    free(owned);
    free(str_args);
    return result;
}

TAURUS_API TaurusStatus taurus_xpath_register_function(
    TaurusDocument doc,
    const char* name,
    TaurusXPathFn fn,
    void* user_data) {
    if (!doc || !name || !fn) return TAURUS_ERROR_NULL_ARG;

    /* Each registration owns a heap-allocated (fn, user_data) pair
     * that the evaluator packs onto the XPathFunctionDef.user_data
     * slot. The thunk unpacks it via ctx->current_fn_user_data. */
    struct taurus_custom_fn_state* state =
        (struct taurus_custom_fn_state*)calloc(1, sizeof(*state));
    if (!state) return TAURUS_ERROR_MEMORY;

    struct taurus_custom_xpath_fn* entry =
        (struct taurus_custom_xpath_fn*)calloc(1, sizeof(*entry));
    if (!entry) { free(state); return TAURUS_ERROR_MEMORY; }

    entry->name = strdup(name);
    if (!entry->name) {
        free(entry); free(state);
        return TAURUS_ERROR_MEMORY;
    }
    entry->fn = fn;
    entry->user_data = user_data;
    /* Stash the (fn, user_data) state on the entry so the
     * registry builder can pass it through to the thunk via the
     * XPathFunctionDef.user_data slot. Use the `user_data` field
     * of the entry itself — the public API `user_data` arg is
     * what the user wants back, and we pack both into `state`. */
    state->fn = fn;
    state->user_data = user_data;
    /* Override the entry's user_data with the packed state so the
     * registry builder picks it up. */
    entry->user_data = state;
    entry->next = doc->custom_xpath_fns;
    doc->custom_xpath_fns = entry;
    return TAURUS_OK;
}

/* Build a per-context function registry that merges standard
 * XPath 1.0 functions with the document's custom fns. Returns
 * NULL if the doc has no custom fns (the context then uses the
 * shared standard singleton). Caller frees via the registry's
 * normal lifecycle. */
XPathFunctionRegistry* taurus_xpath_build_custom_registry(struct taurus_document* doc) {
    if (!doc || !doc->custom_xpath_fns) return NULL;

    XPathFunctionRegistry* reg = xpath_function_registry_new();
    if (!reg) return NULL;
    xpath_function_registry_init_standard(reg);

    for (struct taurus_custom_xpath_fn* e = doc->custom_xpath_fns;
         e; e = e->next) {
        /* min=0 max=32: the user callback is responsible for
         * validating its own arity. */
        xpath_function_registry_register(reg, e->name, custom_xpath_thunk, 0, 32);
        /* Patch the just-added entry's user_data. The standard
         * register helper doesn't take user_data; walk to the
         * last entry and set it. */
        if (reg->count > 0) {
            reg->functions[reg->count - 1].user_data = e->user_data;
        }
    }
    return reg;
}

/* Release the doc's custom-fn list. Called from
 * taurus_document_free. */
void taurus_xpath_free_custom_fns(struct taurus_document* doc) {
    if (!doc) return;
    struct taurus_custom_xpath_fn* e = doc->custom_xpath_fns;
    while (e) {
        struct taurus_custom_xpath_fn* next = e->next;
        free(e->name);
        free(e->user_data);  /* the packed state */
        free(e);
        e = next;
    }
    doc->custom_xpath_fns = NULL;
}
