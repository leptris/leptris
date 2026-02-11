/* error.c - Minimal error handling stubs
 * Copyright (c) 2024, Ribose Inc.
 */

#include "taurus_internal.h"
#include <stdio.h>
#include <string.h>

/* Global error state (thread-local would be better but keeping simple) */
static char error_message[512] = "";
static taurus_error_code last_error = TAURUS_ERROR_NONE;

/**
 * Set error with message
 */
void taurus_set_error(taurus_error_code code, const char* message) {
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
void taurus_set_error_with_context(
    taurus_error_code code,
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

    taurus_set_error(code, message);
}

/**
 * Set parse error with position
 */
void taurus_set_parse_error_position(int line, int column) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Parse error at line %d, column %d", line, column);
    taurus_set_error(TAURUS_ERROR_PARSE_FAILED, msg);
}

/**
 * Extract context snippet (stub)
 */
void taurus_extract_context_snippet(
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