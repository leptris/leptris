/* libleptris - XQuery Operations
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * XQuery 1.0 core (TODO.xslt-full/11): a thin orchestration layer
 * over the XPath engine — prolog declarations bind into the
 * evaluation context, FLWOR clauses drive the same FOR/LET
 * discipline the XPath 2.0+ forms use. Results reuse the XPath
 * result type (SSOT).
 */

#ifndef LEPTRIS_XQUERY_XQUERY_H
#define LEPTRIS_XQUERY_XQUERY_H

#include "../types.h"
#include "../xpath/xpath.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Export macro for Windows DLL support */
#ifndef LEPTRIS_API
#  ifdef LEPTRIS_FOR_BINDGEN
#    define LEPTRIS_API
#  elif defined(_WIN32)
#    ifdef LEPTRIS_BUILD_SHARED
#      define LEPTRIS_API __declspec(dllexport)
#    elif defined(LEPTRIS_BUILDING_DLL)
#      define LEPTRIS_API __declspec(dllexport)
#    else
#      define LEPTRIS_API __declspec(dllimport)
#    endif
#  else
#    define LEPTRIS_API
#  endif
#endif

/* Opaque compiled XQuery. */
typedef struct LeptrisXQueryInternal* LeptrisXQuery;

/* Compile a query (prolog + body). Supported prolog: declare
 * variable, declare namespace, declare function local:*. The body
 * is a FLWOR expression or a plain XPath expression.
 *
 * Returns NULL on syntax errors (the thread-local error channel
 * carries the diagnostic; see leptris_error_message).
 */
LEPTRIS_API LeptrisXQuery leptris_xquery_parse(const char* query,
                                               size_t len);

/* Evaluate a compiled query against a document. context_node may
 * be NULL (document root). Returns an XPath result handle (free
 * with leptris_xpath_result_free): FLWOR results are the
 * synthetic-text sequence; plain expressions keep their type.
 */
LEPTRIS_API LeptrisXPathResult leptris_xquery_eval(LeptrisXQuery query,
                                                   LeptrisDocument doc,
                                                   LeptrisElement context_node);

/* Evaluate a compiled query with external-variable bindings.
 * names[i]/selects[i] bind `declare variable $names[i] external`
 * declarations: each select is an XPath expression evaluated in
 * an empty context (QT3 <param select> semantics) and takes
 * precedence over an `external := default` initializer. An
 * external variable with neither a binding nor a default fails
 * evaluation. Other behavior matches leptris_xquery_eval.
 */
LEPTRIS_API LeptrisXPathResult leptris_xquery_eval_params(
    LeptrisXQuery query, LeptrisDocument doc, LeptrisElement context_node,
    const char* const* names, const char* const* selects, size_t count);

/* Evaluation-context flags for leptris_xquery_eval_params_ex. */
typedef enum {
    /* XSD 1.1 §3.13.4.1/§3.16.4 assertion data model (#2154):
     * comments and processing instructions are not part of the
     * tree the query sees. */
    LEPTRIS_XQUERY_ASSERT_DATA_MODEL = 1,
    /* XSD 1.1 §3.16.4 absent focus (#2183): simple-type assertion
     * facets evaluate with NO context item — position()/last()/
     * relative paths raise XPDY0002. */
    LEPTRIS_XQUERY_ASSERT_ABSENT_FOCUS = 2,
    /* XSD 1.1 §3.13.4.1 cl. 1.3 subtree scoping (#2183): in a
     * complexType assert test the data model contains ONLY the
     * validated element and its subtree — '/' and '//' reach for
     * a document root the model does not contain and select
     * NOTHING (d4_3_15ii14/ii31 pin both directions). */
    LEPTRIS_XQUERY_ASSERT_ROOTED = 4
} LeptrisXQueryEvalFlag;

/* leptris_xquery_eval_params with evaluation-context flags (XSD
 * 1.1 assertion evaluation, #2154). */
LEPTRIS_API LeptrisXPathResult leptris_xquery_eval_params_ex(
    LeptrisXQuery query, LeptrisDocument doc, LeptrisElement context_node,
    const char* const* names, const char* const* selects, size_t count,
    unsigned flags);

/* leptris_xquery_eval_params_ex plus the error-code channel
 * (#2183): when evaluation fails, a spec-defined dynamic error
 * (FOAR0001, XPDY0002, FODC0002, ...) is copied into
 * out_error_code (NUL-terminated, capacity-bounded) so the XSD
 * assertion lane can distinguish "the assertion evaluated FALSE"
 * from "the engine could not assess the expression". Pass NULL to
 * ignore. */
LEPTRIS_API LeptrisXPathResult leptris_xquery_eval_params_ec(
    LeptrisXQuery query, LeptrisDocument doc, LeptrisElement context_node,
    const char* const* names, const char* const* selects, size_t count,
    unsigned flags, char* out_error_code, size_t error_code_cap);

/* Free a compiled query. */
LEPTRIS_API void leptris_xquery_free(LeptrisXQuery query);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_XQUERY_XQUERY_H */
