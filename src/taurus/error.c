/* error.c - Error context management implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Provides thread-local error storage and error handler callbacks.
 */

#include "../include/taurus/error.h"
#include "taurus_internal.h"
#include <string.h>
#include <stdio.h>
#include <pthread.h>

/* ============================================================================
 * Thread-Local Error Storage
 * ============================================================================ */

/* Thread-local storage key for error context */
static pthread_key_t g_error_key;
static pthread_once_t g_error_key_once = PTHREAD_ONCE_INIT;

/* Global error handler (shared across all threads) */
static TaurusErrorHandler g_error_handler = NULL;
static void* g_error_handler_userdata = NULL;

/* ============================================================================
 * Thread-Local Storage Initialization
 * ============================================================================ */

/**
 * Free thread-local error context when thread exits
 */
static void error_context_free(void* ptr) {
    if (ptr) {
        TaurusError* error = (TaurusError*)ptr;
        /* Clear sensitive data */
        memset(error, 0, sizeof(TaurusError));
        free(ptr);
    }
}

/**
 * Initialize thread-local storage key (called once)
 */
static void error_key_init(void) {
    pthread_key_create(&g_error_key, error_context_free);
}

/**
 * Get or create thread-local error context
 */
static TaurusError* get_error_context(void) {
    pthread_once(&g_error_key_once, error_key_init);

    TaurusError* error = (TaurusError*)pthread_getspecific(g_error_key);
    if (!error) {
        error = (TaurusError*)calloc(1, sizeof(TaurusError));
        if (error) {
            error->code = TAURUS_OK;
            pthread_setspecific(g_error_key, error);
        }
    }
    return error;
}

/* ============================================================================
 * Error Message Lookup
 * ============================================================================ */

/**
 * Get human-readable error message for status code
 */
const char* taurus_error_message(TaurusStatus status) {
    switch (status) {
        case TAURUS_OK:
            return "Success";
        case TAURUS_ERROR_MEMORY:
            return "Memory allocation failed";
        case TAURUS_ERROR_PARSE:
            return "XML parsing error";
        case TAURUS_ERROR_XPATH:
            return "XPath evaluation error";
        case TAURUS_ERROR_NULL_ARG:
            return "NULL argument passed";
        case TAURUS_ERROR_INVALID_ARG:
            return "Invalid argument";
        case TAURUS_ERROR_NOT_FOUND:
            return "Resource not found";
        case TAURUS_ERROR_IO:
            return "I/O error";
        default:
            return "Unknown error";
    }
}

/* ============================================================================
 * Public Error API Implementation
 * ============================================================================ */

/**
 * Get last error context for current thread
 */
const TaurusError* taurus_get_last_error(void) {
    TaurusError* error = get_error_context();
    if (error && error->code != TAURUS_OK) {
        return error;
    }
    return NULL;
}

/**
 * Get last error message from the library
 */
const char* taurus_last_error(void) {
    TaurusError* error = get_error_context();
    if (error && error->code != TAURUS_OK) {
        return error->message;
    }
    return "No error";
}

/**
 * Clear last error for current thread
 */
void taurus_clear_error(void) {
    TaurusError* error = get_error_context();
    if (error) {
        error->code = TAURUS_OK;
        error->line = 0;
        error->column = 0;
        error->message[0] = '\0';
        error->context[0] = '\0';
    }
}

/**
 * Set custom error handler
 */
void taurus_set_error_handler(TaurusErrorHandler handler, void* userdata) {
    g_error_handler = handler;
    g_error_handler_userdata = userdata;
}

/**
 * Check if an error is currently set
 */
int taurus_has_error(void) {
    TaurusError* error = get_error_context();
    return (error && error->code != TAURUS_OK) ? 1 : 0;
}

/* ============================================================================
 * Internal Error Setting Functions
 * ============================================================================ */

/**
 * Set error with va_list
 */
void taurus_set_error_v(TaurusStatus code, int line, int column,
                        const char* context, const char* fmt, va_list args) {
    TaurusError* error = get_error_context();
    if (!error) {
        return; /* Can't set error without context */
    }

    /* Set error code and location */
    error->code = code;
    error->line = line;
    error->column = column;

    /* Format message */
    if (fmt) {
        vsnprintf(error->message, sizeof(error->message), fmt, args);
    } else {
        snprintf(error->message, sizeof(error->message), "%s", taurus_error_message(code));
    }

    /* Copy context if provided */
    if (context) {
        strncpy(error->context, context, sizeof(error->context) - 1);
        error->context[sizeof(error->context) - 1] = '\0';
    } else {
        error->context[0] = '\0';
    }

    /* Call error handler if set */
    if (g_error_handler) {
        g_error_handler(error, g_error_handler_userdata);
    }
}

/**
 * Set error with formatted message (extended version)
 */
void taurus_set_error_ex(TaurusStatus code, int line, int column,
                         const char* context, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    taurus_set_error_v(code, line, column, context, fmt, args);
    va_end(args);
}

/**
 * Set simple error without location
 * Note: This is the public API version
 */
void taurus_set_error(TaurusStatus code, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    taurus_set_error_v(code, 0, 0, NULL, fmt, args);
    va_end(args);
}

/**
 * Set error from existing error context
 */
void taurus_set_error_from(const TaurusError* source) {
    if (!source) {
        return;
    }

    TaurusError* error = get_error_context();
    if (!error) {
        return;
    }

    memcpy(error, source, sizeof(TaurusError));
}

/* ============================================================================
 * Legacy Compatibility Functions (Internal Use)
 * ============================================================================ */

/**
 * Set error with line/column position (legacy)
 */
void taurus_set_parse_error_position(int line, int column) {
    taurus_set_error_ex(TAURUS_ERROR_PARSE, line, column, NULL,
                        "Parse error at line %d, column %d", line, column);
}

/**
 * Set error with full context (legacy)
 */
void taurus_set_error_with_context(
    TaurusStatus code,
    const char* message,
    const char* input,
    size_t byte_offset,
    int line,
    int column
) {
    char context[64] = "";
    if (input) {
        taurus_extract_context_snippet(input, byte_offset, line, context, sizeof(context));
    }
    taurus_set_error_ex(code, line, column, context, "%s", message ? message : taurus_error_message(code));
}

/**
 * Extract context snippet from input around error position
 */
void taurus_extract_context_snippet(
    const char* input,
    size_t offset,
    int error_line,
    char* out_buffer,
    size_t buffer_size
) {
    if (!out_buffer || buffer_size == 0) {
        return;
    }

    out_buffer[0] = '\0';

    if (!input) {
        return;
    }

    /* Find the start of the line containing the error */
    size_t line_start = offset;
    while (line_start > 0 && input[line_start - 1] != '\n' && input[line_start - 1] != '\r') {
        line_start--;
    }

    /* Find the end of the line */
    size_t line_end = offset;
    while (input[line_end] != '\0' && input[line_end] != '\n' && input[line_end] != '\r') {
        line_end++;
    }

    /* Extract the line (up to buffer_size - 1 chars) */
    size_t line_len = line_end - line_start;
    size_t copy_len = (line_len < buffer_size - 1) ? line_len : buffer_size - 1;
    strncpy(out_buffer, input + line_start, copy_len);
    out_buffer[copy_len] = '\0';

    (void)error_line; /* Not used in basic implementation */
}
