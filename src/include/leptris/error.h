/* libleptris - Error Handling
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * This file contains error handling utilities and error code descriptions.
 */

#ifndef LEPTRIS_ERROR_H
#define LEPTRIS_ERROR_H

#include "types.h"

/* Export macro (mirrors dom/document.h). */
#if !defined(LEPTRIS_API)
#  if defined(_WIN32)
#    if defined(LEPTRIS_BUILDING_DLL)
#      define LEPTRIS_API __declspec(dllexport)
#    else
#      define LEPTRIS_API __declspec(dllimport)
#    endif
#  else
#    define LEPTRIS_API __attribute__((visibility("default")))
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Error Message Functions
 * ============================================================================ */

/**
 * Get human-readable error message for a status code
 *
 * DEPRECATED alias of leptris_status_string (leptris.h) — identical
 * output. leptris_status_string is the canonical status->message
 * function; this alias is kept so existing bindings need no change
 * (TODO.concurrency/04).
 *
 * @param status Status code
 * @return Error message string (static, do not free)
 */
LEPTRIS_API const char* leptris_error_message(LeptrisStatus status);

/**
 * Get the THREAD-LOCAL last error message (best-effort, legacy)
 *
 * Returns the most recent error message recorded on the CALLING
 * thread — safe under the one-document-per-thread contract since
 * TODO.concurrency/01. For reliable, document-scoped retrieval use
 * leptris_document_last_error(doc) instead.
 *
 * @return Error message string (thread-local storage, do not free),
 *         or NULL when the thread has no recorded error
 */
LEPTRIS_API const char* leptris_last_error(void);

/**
 * Get the error message from this document's last failed operation
 *
 * Populated when an operation against a LIVE document fails —
 * currently XPath evaluation (leptris_xpath_eval returning NULL).
 * Immune to concurrent operations on other documents/threads.
 *
 * Parse failures return a NULL document (nothing to query): pair
 * the status out-param with the thread-local leptris_last_error()
 * for those messages.
 *
 * @param doc Document handle
 * @return Message (owned by the document, valid until the next
 *         failing operation on it or leptris_document_free), or
 *         NULL when the document has no recorded error or doc is
 *         NULL
 */
LEPTRIS_API const char* leptris_document_last_error(LeptrisDocument doc);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_ERROR_H */
