/**
 * @file buffer.c
 * @brief Buffered output layer for StAX writer
 *
 * Implements small-write coalescing pattern from Woodstox:
 * - Writes < 256 bytes are buffered
 * - Writes >= 256 bytes flush buffer then pass through directly
 *
 * This minimizes syscalls for small writes while avoiding unnecessary
 * buffer copying for large writes.
 */

#include "writer_internal.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Buffer Initialization
 * ============================================================================ */

/**
 * File-based write callback
 */
static size_t file_write_callback(void* ctx, const char* data, size_t len) {
    FILE* f = (FILE*)ctx;
    if (!f || !data || len == 0) return 0;
    return fwrite(data, 1, len, f);
}

int buffer_init(OutputBuffer* buf, TaurusWriteCallback write, void* ctx) {
    if (!buf || !write) return -1;

    buf->data = (char*)malloc(WRITER_BUFFER_SIZE);
    if (!buf->data) return -1;

    buf->ptr = 0;
    buf->capacity = WRITER_BUFFER_SIZE;
    buf->write = write;
    buf->ctx = ctx;
    buf->owns_file = 0;
    buf->file = NULL;
    buf->error = 0;

    return 0;
}

int buffer_init_file(OutputBuffer* buf, FILE* file, int owns_file) {
    if (!buf || !file) return -1;

    int result = buffer_init(buf, file_write_callback, file);
    if (result != 0) return result;

    buf->owns_file = owns_file;
    buf->file = file;

    return 0;
}

void buffer_cleanup(OutputBuffer* buf) {
    if (!buf) return;

    /* Flush any remaining data */
    if (buf->ptr > 0 && buf->write && buf->error == 0) {
        buffer_flush(buf);
    }

    /* Free buffer */
    if (buf->data) {
        free(buf->data);
        buf->data = NULL;
    }

    /* Close file if we own it */
    if (buf->owns_file && buf->file) {
        fclose(buf->file);
        buf->file = NULL;
    }

    buf->ptr = 0;
    buf->capacity = 0;
}

/* ============================================================================
 * Buffer Management
 * ============================================================================ */

void buffer_ensure(OutputBuffer* buf, size_t needed) {
    if (!buf) return;

    size_t available = buf->capacity - buf->ptr;
    if (available >= needed) return;

    /* Grow buffer by 1.5x (industry standard) */
    size_t new_capacity = buf->capacity;
    while (new_capacity - buf->ptr < needed) {
        new_capacity = new_capacity + new_capacity / 2;
    }

    char* new_data = (char*)realloc(buf->data, new_capacity);
    if (!new_data) {
        buf->error = TAURUS_WRITER_ERROR_MEMORY;
        return;
    }

    buf->data = new_data;
    buf->capacity = new_capacity;
}

/* ============================================================================
 * Write Operations
 * ============================================================================ */

void buffer_write_raw(OutputBuffer* buf, const char* data, size_t len) {
    if (!buf || !data || len == 0 || buf->error) return;

    /* Ensure space */
    buffer_ensure(buf, len);
    if (buf->error) return;

    /* Copy to buffer */
    memcpy(buf->data + buf->ptr, data, len);
    buf->ptr += len;
}

void buffer_write_char(OutputBuffer* buf, char c) {
    if (!buf || buf->error) return;

    /* Ensure space (inline for performance) */
    if (buf->ptr >= buf->capacity) {
        buffer_ensure(buf, 1);
        if (buf->error) return;
    }

    buf->data[buf->ptr++] = c;
}

void buffer_write_smart(OutputBuffer* buf, const char* data, size_t len) {
    if (!buf || !data || len == 0 || buf->error) return;

    if (len < WRITER_SMALL_WRITE_THRESH) {
        /* Small write: buffer it */
        buffer_write_raw(buf, data, len);
    } else {
        /* Large write: flush buffer, then pass through directly */
        buffer_flush(buf);
        if (buf->error) return;

        /* Direct write (bypass buffer) */
        size_t written = buf->write(buf->ctx, data, len);
        if (written != len) {
            buf->error = TAURUS_WRITER_ERROR_IO;
        }
    }
}

int buffer_flush(OutputBuffer* buf) {
    if (!buf) return -1;
    if (buf->ptr == 0) return 0;  /* Nothing to flush */
    if (buf->error) return buf->error;

    /* Write buffered data */
    size_t written = buf->write(buf->ctx, buf->data, buf->ptr);
    if (written != buf->ptr) {
        buf->error = TAURUS_WRITER_ERROR_IO;
        return buf->error;
    }

    /* Reset buffer */
    buf->ptr = 0;

    return 0;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Write null-terminated string to buffer
 */
void buffer_write_string(OutputBuffer* buf, const char* str) {
    if (!buf || !str || buf->error) return;
    buffer_write_raw(buf, str, strlen(str));
}

/**
 * Write repeated character to buffer (for indentation)
 */
void buffer_write_repeat(OutputBuffer* buf, char c, size_t count) {
    if (!buf || count == 0 || buf->error) return;

    /* Ensure space */
    buffer_ensure(buf, count);
    if (buf->error) return;

    /* Fill with character */
    memset(buf->data + buf->ptr, c, count);
    buf->ptr += count;
}
