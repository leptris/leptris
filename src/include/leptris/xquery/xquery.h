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

/* Free a compiled query. */
LEPTRIS_API void leptris_xquery_free(LeptrisXQuery query);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_XQUERY_XQUERY_H */
