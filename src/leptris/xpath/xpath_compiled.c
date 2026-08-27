/* xpath/xpath_compiled.c — compiled XPath expressions
 * (TODO.bindings/03, issue #510 Tier 2).
 *
 * leptris_xpath_eval re-hashes the expression and re-checks the
 * process-wide AST/bytecode cache on every call. A compiled handle
 * pins its cache entry once and skips both steps — the hot-loop win
 * for bindings (issue #509's follow-up).
 *
 * Thread contract (mirrors the README Threading model): the pinned
 * entry is immutable; any number of threads may evaluate the same
 * handle concurrently against different documents. Free the handle
 * only after the last evaluation returns. */
#include "xpath_internal.h"
#include "parser.h"
#include "bytecode.h"
#include "evaluator_internal.h"
#include "xpath_variables.h"
#include "../leptris_internal.h"
#include "../../include/leptris.h"
#include <stdlib.h>
#include <string.h>

struct leptris_xpath_compiled {
    char* expr;
    size_t expr_len;
    XPathASTNode* ast;   /* canonical, pinned in the cache */
};

LEPTRIS_API LeptrisXPathCompiled leptris_xpath_compile(const char* expression) {
    if (!expression || !*expression) return NULL;
    size_t len = strlen(expression);

    struct leptris_xpath_compiled* c =
        (struct leptris_xpath_compiled*)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->expr_len = len;
    c->expr = (char*)malloc(len + 1);
    if (!c->expr) { free(c); return NULL; }
    memcpy(c->expr, expression, len);
    c->expr[len] = '\0';

    XPathParser* parser = xpath_parser_new(expression, len);
    if (!parser) { free(c->expr); free(c); return NULL; }
    XPathASTNode* ast = xpath_parse(parser);
    const char* parse_error = xpath_parser_error(parser);
    if (!ast || parse_error) {
        leptris_set_error(LEPTRIS_ERROR_XPATH_SYNTAX,
                          parse_error && parse_error[0]
                              ? parse_error : "XPath syntax error");
        xpath_parser_free(parser);
        free(c->expr); free(c);
        return NULL;
    }
    xpath_parser_free(parser);

    /* Insert returns the canonical (pinned) AST — a racing twin may
     * have won the slot; never keep using the private parse. */
    c->ast = xpath_ast_cache_insert(c->expr, len, ast);
    if (!c->ast) { free(c->expr); free(c); return NULL; }
    return c;
}

/* Run a compiled expression against a PREPARED context (the XSLT
 * bridge installs its own function registry + variable set before
 * calling this). VM fast path first; AST interpreter fallback. The
 * caller owns the context storage and is responsible for cleanup. */
struct leptris_xpath_result* leptris_xpath_compiled_eval_in(
        LeptrisXPathCompiled compiled, XPathContext* xpath_ctx) {
    if (!compiled || !xpath_ctx || !xpath_ctx->document) return NULL;
    LeptrisXPathBytecode* bc = xpath_ast_cache_get_bc(compiled->expr,
                                                      compiled->expr_len);
    struct leptris_xpath_result* result = NULL;
    if (bc) {
        result = leptris_xpath_vm_run_bc(bc, xpath_ctx);
    } else {
        bc = leptris_xpath_compile_ast(compiled->ast);
        if (bc) {
            result = leptris_xpath_vm_run_bc(bc, xpath_ctx);
            xpath_ast_cache_store_bc(compiled->expr, compiled->expr_len, bc);
        }
    }
    if (!result) result = xpath_evaluate(xpath_ctx, compiled->ast);

    if (!result && xpath_ctx->error_msg[0]) {
        struct leptris_document* d = xpath_ctx->document;
        strncpy(d->last_error_message, xpath_ctx->error_msg,
                sizeof(d->last_error_message) - 1);
        d->last_error_message[sizeof(d->last_error_message) - 1] = '\0';
    }
    return result;
}

LEPTRIS_API LeptrisXPathResult leptris_xpath_compiled_eval(
        LeptrisXPathCompiled compiled, LeptrisDocument doc,
        LeptrisElement context) {
    if (!compiled || !doc) return NULL;

    LeptrisElement context_elem =
        context ? context : leptris_document_root(doc);
    if (!context_elem) return NULL;

    XPathContext ctx_storage;
    XPathContext* xpath_ctx = &ctx_storage;
    xpath_context_init(xpath_ctx, doc, context_elem);
    if (!xpath_ctx->document) return NULL;

    struct leptris_xpath_result* result =
        leptris_xpath_compiled_eval_in(compiled, xpath_ctx);

    xpath_context_cleanup(xpath_ctx);
    return result;
}

/* TODO.engine/02: the context-carrying variants. The ns/vars routes
 * run the direct evaluator (VM fast paths skip prefixed name tests),
 * so these evaluate the pinned AST with ns_set / variable_set
 * installed — the same semantics as leptris_xpath_eval_ns /
 * leptris_xpath_eval_with_vars_context, minus the re-parse. pos is
 * the caller's in-flight node-list position (§12.4 position()); the
 * wrappers pass 1 (the XPath default context position). */
static struct leptris_xpath_result* compiled_eval_context(
        LeptrisXPathCompiled compiled, LeptrisDocument doc,
        LeptrisElement context, struct leptris_xpath_ns_map* ns,
        XPathVariableSet* vars, size_t pos) {
    if (!compiled || !doc) return NULL;

    LeptrisElement context_elem =
        context ? context : leptris_document_root(doc);
    if (!context_elem) return NULL;

    XPathContext ctx_storage;
    XPathContext* xpath_ctx = &ctx_storage;
    xpath_context_init(xpath_ctx, doc, context_elem);
    if (!xpath_ctx->document) return NULL;

    xpath_ctx->ns_set = ns;
    xpath_ctx->variable_set = vars;
    xpath_ctx->context_position = pos;

    /* VM fast path first (issue #564): the VM's name matcher is
     * namespace-aware and the absolute folds lower prefixed tests
     * through the local-name bucket + URI verification, so ns-bound
     * expressions ride the same index-backed paths as plain ones.
     * The bytecode comes from the shared per-expression cache.
     *
     * Variable-bound expressions stay on the interpreter for now
     * (#565 follow-up): the VM's operator/variable interop still
     * diverges on union-of-variable-nodesets (bug-76). */
    struct leptris_xpath_result* result = NULL;
    if (!vars) {
        /* Pinned borrow (the raw get_bc returns an unpinned pointer —
         * a re-entrant store during the run can evict and free it;
         * ASAN caught exactly that in libxslt bug-147, PR #600). */
        XPathCacheEntry ce;
        int hit = xpath_ast_cache_get(compiled->expr, compiled->expr_len,
                                      &ce);
        LeptrisXPathBytecode* bc = hit ? ce.bc : NULL;
        if (hit && !bc) {
            bc = leptris_xpath_compile_ast(compiled->ast);
            if (bc) xpath_ast_cache_store_bc(compiled->expr,
                                             compiled->expr_len, bc);
        }
        if (bc) {
            result = leptris_xpath_vm_run_bc(bc, xpath_ctx);
            if (!result) xpath_ctx->error_msg[0] = '\0';
        }
        if (hit) xpath_ast_cache_release(ce.ast);
    }
    if (!result)
        result = xpath_evaluate(xpath_ctx, compiled->ast);

    if (!result && xpath_ctx->error_msg[0]) {
        strncpy(doc->last_error_message, xpath_ctx->error_msg,
                sizeof(doc->last_error_message) - 1);
        doc->last_error_message[sizeof(doc->last_error_message) - 1] = '\0';
    }

    xpath_context_cleanup(xpath_ctx);
    return result;
}

LEPTRIS_API LeptrisXPathResult leptris_xpath_compiled_eval_ns(
        LeptrisXPathCompiled compiled, LeptrisDocument doc,
        LeptrisElement context, LeptrisXPathNsSet ns) {
    return compiled_eval_context(compiled, doc, context,
                                 (struct leptris_xpath_ns_map*)ns, NULL, 1);
}

/* Combined ns + vars entry — the XSLT engine's §4 prefixed tests
 * inside variable-carrying transforms. */
struct leptris_xpath_result* leptris_xpath_compiled_eval_ns_vars(
        LeptrisXPathCompiled compiled, LeptrisDocument doc,
        LeptrisElement context, struct leptris_xpath_ns_map* ns,
        XPathVariableSet* vars) {
    return compiled_eval_context(compiled, doc, context, ns, vars, 1);
}

/* Full-context eval for the XSLT engine: the VM fast path when
 * neither ns nor vars are bound (identical semantics to
 * leptris_xpath_compiled_eval), the AST interpreter otherwise, and
 * the context position carries the in-flight node-list position so
 * position() (§12.4) reflects the for-each / apply-templates
 * iteration instead of the XPath default 1. */
struct leptris_xpath_result* leptris_xpath_compiled_eval_ctx(
        LeptrisXPathCompiled compiled, LeptrisDocument doc,
        LeptrisElement context, struct leptris_xpath_ns_map* ns,
        XPathVariableSet* vars, size_t pos) {
    if (!compiled || !doc) return NULL;
    if (ns || vars)
        return compiled_eval_context(compiled, doc, context, ns, vars, pos);

    LeptrisElement context_elem =
        context ? context : leptris_document_root(doc);
    if (!context_elem) return NULL;

    XPathContext ctx_storage;
    XPathContext* xpath_ctx = &ctx_storage;
    xpath_context_init(xpath_ctx, doc, context_elem);
    if (!xpath_ctx->document) return NULL;
    xpath_ctx->context_position = pos;

    struct leptris_xpath_result* result =
        leptris_xpath_compiled_eval_in(compiled, xpath_ctx);

    xpath_context_cleanup(xpath_ctx);
    return result;
}

LEPTRIS_API LeptrisXPathResult leptris_xpath_compiled_eval_vars(
        LeptrisXPathCompiled compiled, LeptrisDocument doc,
        LeptrisElement context, LeptrisXPathVariableSet variables) {
    return compiled_eval_context(compiled, doc, context, NULL,
                                 (XPathVariableSet*)variables, 1);
}

LEPTRIS_API const char* leptris_xpath_compiled_text(LeptrisXPathCompiled compiled) {
    return compiled ? compiled->expr : NULL;
}

LEPTRIS_API void leptris_xpath_compiled_free(LeptrisXPathCompiled compiled) {
    if (!compiled) return;
    xpath_ast_cache_release(compiled->ast);
    free(compiled->expr);
    free(compiled);
}
