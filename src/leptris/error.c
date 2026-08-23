/* error.c - Minimal error handling stubs
 * Copyright (c) 2024, Ribose Inc.
 */

#include "leptris_internal.h"
#include "../include/leptris.h"
#include "common/port.h"  /* LEPTRIS_THREAD_LOCAL (TODO.concurrency/01) */
#include <stdio.h>
#include <string.h>

/* Error state — THREAD-LOCAL since TODO.concurrency/01: concurrent
 * parses (one document per thread) no longer race the channel. The
 * bare leptris_last_error reads this; per-document snapshots go
 * through leptris_document_last_error. */
static LEPTRIS_THREAD_LOCAL char error_message[512] = "";
static LEPTRIS_THREAD_LOCAL leptris_error_code last_error = LEPTRIS_ERROR_NONE;

/* Per-document snapshot: failing operations against a LIVE document
 * (currently XPath evaluation) copy the thread-local message here. */
LEPTRIS_API const char* leptris_document_last_error(LeptrisDocument doc) {
    if (!doc) return NULL;
    struct leptris_document* d = (struct leptris_document*)doc;
    return d->last_error_message[0] ? d->last_error_message : NULL;
}

void leptris_doc_snapshot_error(struct leptris_document* doc) {
    if (!doc) return;
    strncpy(doc->last_error_message, error_message,
            sizeof(doc->last_error_message) - 1);
    doc->last_error_message[sizeof(doc->last_error_message) - 1] = '\0';
}

/**
 * Set error with message
 */
/* TODO.concurrency/01 + issue #510: 1-based line/column of the most
 * recent parse error on this thread. 0/0 = unknown. */
static LEPTRIS_THREAD_LOCAL int last_error_line = 0;
static LEPTRIS_THREAD_LOCAL int last_error_column = 0;

LEPTRIS_API void leptris_last_error_position(int* line, int* column) {
    if (line) *line = last_error_line;
    if (column) *column = last_error_column;
}

void leptris_set_error_position(int line, int column) {
    last_error_line = line;
    last_error_column = column;
}

void leptris_set_error(leptris_error_code code, const char* message) {
    last_error = code;
    if (message) {
        strncpy(error_message, message, sizeof(error_message) - 1);
        error_message[sizeof(error_message) - 1] = '\0';
    } else {
        error_message[0] = '\0';
    }
}

/**
 * Set error with context (full version)
 */
void leptris_set_error_with_context(
    leptris_error_code code,
    const char* message,
    const char* input,
    size_t byte_offset,
    int line,
    int column
) {
    (void)input;
    (void)byte_offset;
    (void)line;
    (void)column;

    leptris_set_error(code, message);
}

/**
 * Set parse error with position
 */
void leptris_set_parse_error_position(int line, int column) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Parse error at line %d, column %d", line, column);
    leptris_set_error(LEPTRIS_ERROR_PARSE_FAILED, msg);
}

/**
 * Extract context snippet (stub)
 */
void leptris_extract_context_snippet(
    const char* input,
    size_t offset,
    int error_line,
    char* out_buffer,
    size_t buffer_size
) {
    (void)input;
    (void)offset;
    (void)error_line;

    if (out_buffer && buffer_size > 0) {
        out_buffer[0] = '\0';
    }
}

/* The public header has declared these since the initial release
 * but they were never defined — phantom symbols that linked nowhere
 * (found when the Rust binding referenced leptris_error_message and
 * MSVC failed to resolve it). */
LEPTRIS_API const char* leptris_error_message(LeptrisStatus status) {
    return leptris_status_string(status);
}

LEPTRIS_API const char* leptris_last_error(void) {
    return error_message[0] ? error_message : NULL;
}

/* (Doc contract in error.h: NULL when the thread has no error.) */

LEPTRIS_API const char* leptris_status_string(LeptrisStatus status) {
    switch (status) {
        case LEPTRIS_OK:              return "OK";
        case LEPTRIS_ERROR_MEMORY:    return "Memory allocation failed";
        case LEPTRIS_ERROR_PARSE:     return "XML parse error";
        case LEPTRIS_ERROR_XPATH:     return "XPath evaluation error";
        case LEPTRIS_ERROR_NULL_ARG:  return "NULL argument";
        case LEPTRIS_ERROR_INVALID_ARG: return "Invalid argument";
        case LEPTRIS_ERROR_NOT_FOUND: return "Not found";
        case LEPTRIS_ERROR_IO:        return "I/O error";
        default:                     return "Unknown error";
    }
}