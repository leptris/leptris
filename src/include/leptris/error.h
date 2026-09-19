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
     /* Mirrors leptris.h (GCC LTO externalization, #1204). */
#    if defined(__GNUC__) && !defined(__clang__)
#      define LEPTRIS_API __attribute__((visibility("default"), used, \
                                            externally_visible))
#    else
#      define LEPTRIS_API __attribute__((visibility("default"), used))
#    endif
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
 * Get the position of this thread's last recorded parse error
 *
 * Populated by DOM parse failures (leptris_parse_string returning
 * NULL) alongside leptris_last_error — line/column parity with
 * lxml's XMLSyntaxError and Nokogiri (issue #510).
 *
 * @param line Out: 1-based line (may be NULL)
 * @param column Out: 1-based byte column (may be NULL)
 *
 * Memory: Writes through the out-pointers only.
 */
LEPTRIS_API void leptris_last_error_position(int* line, int* column);

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

/* ============================================================================
 * Parse diagnostics (#1200): recover-class events recorded on the
 * DOM parse (duplicate attributes today) — the SAX lane's recover
 * error list, unified engine-side so both lanes report the same
 * surface. Documents stay usable; the diagnostics are advisory.
 * ============================================================================ */

#ifndef LEPTRIS_API
#  ifdef _WIN32
#    ifdef LEPTRIS_BUILD_SHARED
#      define LEPTRIS_API __declspec(dllexport)
#    elif defined(LEPTRIS_BUILDING_DLL)
#      define LEPTRIS_API __declspec(dllexport)
#    elif defined(LEPTRIS_USE_SHARED)
#      define LEPTRIS_API __declspec(dllimport)
#    else
#      define LEPTRIS_API
#    endif
#  else
#    if defined(__GNUC__) && !defined(__clang__)
#      define LEPTRIS_API __attribute__((visibility("default"), used, \
                                            externally_visible))
#    else
#      define LEPTRIS_API __attribute__((visibility("default"), used))
#    endif
#  endif
#endif

/**
 * Number of recover-class diagnostics recorded during the
 * document's parse.
 *
 * @param doc Document handle
 * @return Diagnostic count (0 for NULL doc or a clean parse)
 */
LEPTRIS_API size_t leptris_document_parse_diag_count(LeptrisDocument doc);

/**
 * Read one parse diagnostic by index.
 *
 * @param doc Document handle
 * @param index Diagnostic index (< leptris_document_parse_diag_count)
 * @param kind Out: diagnostic kind (may be NULL)
 * @param message Out: NUL-terminated message buffer (may be NULL)
 * @param message_cap Capacity of message (ignored when message NULL)
 * @return 1 on success; 0 for NULL doc or out-of-range index
 *
 * Memory: Message is copied into the caller's buffer; diagnostics
 *         are owned by the document until leptris_document_free.
 */
LEPTRIS_API int leptris_document_parse_diag(LeptrisDocument doc,
                                            size_t index,
                                            LeptrisDiagKind* kind,
                                            char* message,
                                            size_t message_cap);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_ERROR_H */
