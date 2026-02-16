/* libtaurus - Error Handling
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * This file contains error handling utilities and error context management.
 * Provides rich error information including line, column, and descriptive messages.
 */

#ifndef TAURUS_ERROR_H
#define TAURUS_ERROR_H

#include "types.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Error Context Structure
 * ============================================================================ */

/**
 * Rich error context structure
 *
 * Provides detailed information about the last error that occurred,
 * including location (line/column for parsing errors) and a descriptive message.
 */
typedef struct {
    TaurusStatus code;          /**< Error code (TAURUS_OK means no error) */
    int line;                   /**< Line number (1-based), 0 if not applicable */
    int column;                 /**< Column number (1-based), 0 if not applicable */
    char message[256];          /**< Human-readable error message */
    char context[64];           /**< Surrounding context (for parsing errors) */
} TaurusError;

/* ============================================================================
 * Error Handler Callback
 * ============================================================================ */

/**
 * Error handler callback type
 *
 * Called when an error occurs, allowing custom error handling, logging,
 * or error reporting. The callback receives a pointer to the error context
 * and optional user-provided data.
 *
 * @param error Pointer to the error context (valid only during callback)
 * @param userdata User-provided data passed to taurus_set_error_handler()
 */
typedef void (*TaurusErrorHandler)(const TaurusError* error, void* userdata);

/* ============================================================================
 * Public Error API
 * ============================================================================ */

/**
 * Get human-readable error message for status code
 *
 * @param status Status code
 * @return Error message string (static, do not free)
 */
const char* taurus_error_message(TaurusStatus status);

/**
 * Get last error context for current thread
 *
 * Returns a pointer to thread-local error storage containing details
 * about the most recent error. The pointer is valid until the next
 * call to any taurus function that may generate an error.
 *
 * @return Pointer to TaurusError structure (NULL if no error)
 */
const TaurusError* taurus_get_last_error(void);

/**
 * Get last error message from the library
 *
 * Convenience function that returns just the message from the last error.
 * Equivalent to taurus_get_last_error()->message, but returns a generic
 * message if no error context is available.
 *
 * @return Error message string (static, do not free)
 */
const char* taurus_last_error(void);

/**
 * Clear last error for current thread
 *
 * Resets the error context to TAURUS_OK with no message.
 * Call this after handling an error to prevent stale error state.
 */
void taurus_clear_error(void);

/**
 * Set custom error handler
 *
 * Install a callback function to be called when errors occur.
 * Pass NULL to disable custom error handling.
 *
 * @param handler Error handler callback (NULL to disable)
 * @param userdata User data passed to handler on each error
 */
void taurus_set_error_handler(TaurusErrorHandler handler, void* userdata);

/**
 * Check if an error is currently set
 *
 * @return 1 if an error is set, 0 otherwise
 */
int taurus_has_error(void);

/* ============================================================================
 * Internal Error Setting Functions
 * ============================================================================ */

/**
 * Set error with formatted message (internal use)
 *
 * Sets the thread-local error context with the given code, location,
 * and formatted message. Also invokes the error handler if one is set.
 *
 * @param code Error status code
 * @param line Line number (0 if not applicable)
 * @param column Column number (0 if not applicable)
 * @param context Context string (may be NULL)
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void taurus_set_error_ex(TaurusStatus code, int line, int column,
                         const char* context, const char* fmt, ...);

/**
 * Set error with va_list (internal use)
 *
 * Variant of taurus_set_error_ex that accepts a va_list.
 *
 * @param code Error status code
 * @param line Line number (0 if not applicable)
 * @param column Column number (0 if not applicable)
 * @param context Context string (may be NULL)
 * @param fmt Printf-style format string
 * @param args va_list of format arguments
 */
void taurus_set_error_v(TaurusStatus code, int line, int column,
                        const char* context, const char* fmt, va_list args);

/**
 * Set simple error without location (internal use)
 *
 * Convenience function for errors that don't have line/column context.
 *
 * @param code Error status code
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void taurus_set_error(TaurusStatus code, const char* fmt, ...);

/**
 * Set error from existing error context (internal use)
 *
 * Copies an existing error context to the thread-local storage.
 *
 * @param error Source error context to copy
 */
void taurus_set_error_from(const TaurusError* error);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_ERROR_H */
