/**
 * @file writer_internal.h
 * @brief Internal structures for StAX XML writer
 *
 * This header defines the internal data structures used by the writer
 * implementation. It is not part of the public API.
 */

#ifndef TAURUS_WRITER_INTERNAL_H
#define TAURUS_WRITER_INTERNAL_H

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include "../../include/taurus/writer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Buffer Constants (tuned from Woodstox benchmarks)
 * ============================================================================ */

#define WRITER_BUFFER_SIZE       8192    /* 8KB output buffer */
#define WRITER_SMALL_WRITE_THRESH 256    /* Coalesce writes < 256 bytes */
#define WRITER_ELEMENT_STACK_SIZE 64     /* Initial element stack capacity */

/* ============================================================================
 * Writer State Machine
 * ============================================================================ */

/**
 * Writer states for state machine validation
 *
 * State transitions:
 *   PROLOG -> DOCUMENT_STARTED -> ELEMENT_OPEN -> CONTENT -> ELEMENT_CLOSED -> EPILOG
 *                    |                |              |
 *                    v                v              v
 *               (attributes)   (children)     (end_element)
 */
typedef enum {
    WRITER_STATE_PROLOG,         /* Before document element */
    WRITER_STATE_DOCUMENT_STARTED, /* After start_document, before root element */
    WRITER_STATE_ELEMENT_OPEN,   /* After start_element, before end_element or content */
    WRITER_STATE_CONTENT,        /* Inside element content */
    WRITER_STATE_ELEMENT_CLOSED, /* After end_element (may open new element) */
    WRITER_STATE_EPILOG,         /* After end_document */
    WRITER_STATE_ERROR           /* Error state */
} WriterState;

/* ============================================================================
 * Output Buffer
 * ============================================================================ */

/**
 * Output buffer for efficient write coalescing
 *
 * Small writes (<256 bytes) are buffered to minimize syscalls.
 * Large writes pass through directly after flushing the buffer.
 */
typedef struct {
    char* data;                  /* Output buffer (malloc'd) */
    size_t ptr;                  /* Current write position */
    size_t capacity;             /* Buffer capacity */
    TaurusWriteCallback write;   /* Output callback */
    void* ctx;                   /* Callback context */
    int owns_file;               /* 1 if we own the FILE* (for file-based writers) */
    FILE* file;                  /* FILE* for file-based writers (NULL otherwise) */
    int error;                   /* Last error code (0 = no error) */
} OutputBuffer;

/* ============================================================================
 * Element Stack Entry
 * ============================================================================ */

/**
 * Element stack entry for tracking open elements
 *
 * Used for:
 * 1. Proper nesting validation
 * 2. Writing correct closing tags
 * 3. Namespace scoping
 */
typedef struct {
    char* name;                  /* Element name (allocated in writer's pool) */
    size_t name_len;             /* Name length */
    int has_content;             /* 1 if element has text/child content */
    int has_children;            /* 1 if element has child elements */
} ElementStackEntry;

/* ============================================================================
 * Writer Structure
 * ============================================================================ */

/**
 * Main writer structure
 */
struct TaurusXMLWriter {
    /* Output buffer */
    OutputBuffer buffer;

    /* Writer state machine */
    WriterState state;

    /* Element stack for proper nesting */
    ElementStackEntry* element_stack;
    size_t stack_depth;
    size_t stack_capacity;

    /* Writer options */
    TaurusWriterOptions opts;

    /* Error handling */
    int last_error;
    char error_message[256];

    /* Current element tracking (for attribute validation) */
    int in_start_tag;            /* 1 if we're in a start tag (can write attributes) */

    /* Document state */
    int document_started;        /* 1 if start_document was called */
    int document_ended;          /* 1 if end_document was called */

    /* Memory pool for element names (simple bump allocator) */
    char* name_pool;
    size_t name_pool_ptr;
    size_t name_pool_capacity;
};

/* ============================================================================
 * Buffer Operations (buffer.c)
 * ============================================================================ */

/**
 * Initialize output buffer
 *
 * @param buf Buffer to initialize
 * @param write Output callback
 * @param ctx Callback context
 * @return 0 on success, -1 on error
 */
int buffer_init(OutputBuffer* buf, TaurusWriteCallback write, void* ctx);

/**
 * Initialize output buffer for FILE*
 *
 * @param buf Buffer to initialize
 * @param file FILE* to write to
 * @param owns_file 1 if buffer should close the file
 * @return 0 on success, -1 on error
 */
int buffer_init_file(OutputBuffer* buf, FILE* file, int owns_file);

/**
 * Free output buffer resources
 *
 * @param buf Buffer to free
 */
void buffer_cleanup(OutputBuffer* buf);

/**
 * Ensure buffer has enough space
 *
 * @param buf Output buffer
 * @param needed Bytes needed
 */
void buffer_ensure(OutputBuffer* buf, size_t needed);

/**
 * Write raw bytes to buffer (no escaping)
 *
 * @param buf Output buffer
 * @param data Data to write
 * @param len Length of data
 */
void buffer_write_raw(OutputBuffer* buf, const char* data, size_t len);

/**
 * Write single character to buffer
 *
 * @param buf Output buffer
 * @param c Character to write
 */
void buffer_write_char(OutputBuffer* buf, char c);

/**
 * Write with small-write coalescing (Woodstox pattern)
 *
 * Small writes (<256 bytes) are buffered.
 * Large writes flush buffer then pass through directly.
 *
 * @param buf Output buffer
 * @param data Data to write
 * @param len Length of data
 */
void buffer_write_smart(OutputBuffer* buf, const char* data, size_t len);

/**
 * Flush buffer to output
 *
 * @param buf Output buffer
 * @return 0 on success, -1 on error
 */
int buffer_flush(OutputBuffer* buf);

/**
 * Write null-terminated string to buffer
 *
 * @param buf Output buffer
 * @param str String to write
 */
void buffer_write_string(OutputBuffer* buf, const char* str);

/**
 * Write repeated character to buffer (for indentation)
 *
 * @param buf Output buffer
 * @param c Character to repeat
 * @param count Number of times to repeat
 */
void buffer_write_repeat(OutputBuffer* buf, char c, size_t count);

/* ============================================================================
 * Escape Operations (escape.c)
 * ============================================================================ */

/**
 * Escape lookup table entry
 */
typedef struct {
    const char* entity;          /* Entity string (e.g., "&lt;") or NULL if no escape */
    uint8_t len;                 /* Entity length */
} EscapeEntry;

/**
 * Pre-computed escape lookup table for text content
 *
 * Index by character value (0-255). If entity is non-NULL,
 * the character should be replaced with the entity string.
 */
extern const EscapeEntry escape_table_text[256];

/**
 * Pre-computed escape lookup table for attribute values
 *
 * Includes additional escapes for quotes.
 */
extern const EscapeEntry escape_table_attr[256];

/**
 * Initialize escape lookup tables
 *
 * Called once at library initialization.
 */
void escape_tables_init(void);

/**
 * Calculate escaped length of text
 *
 * @param text Input text
 * @param len Length of text
 * @param is_attr 1 for attribute escaping, 0 for text escaping
 * @return Length after escaping
 */
size_t calc_escaped_length(const char* text, size_t len, int is_attr);

/**
 * Write escaped text to buffer
 *
 * Uses lookup table for branch-free escaping.
 *
 * @param buf Output buffer
 * @param text Text to escape
 * @param len Length of text
 * @param is_attr 1 for attribute escaping, 0 for text escaping
 */
void buffer_write_escaped(OutputBuffer* buf, const char* text, size_t len, int is_attr);

/**
 * Find position of first character needing escape (SIMD-optimized)
 *
 * @param text Text to scan
 * @param len Length of text
 * @param is_attr 1 for attribute escaping, 0 for text escaping
 * @return Position of first escape char, or len if none found
 */
size_t find_escape_pos(const char* text, size_t len, int is_attr);

/* ============================================================================
 * Writer Internal Helpers
 * ============================================================================ */

/**
 * Push element onto stack
 *
 * @param w Writer handle
 * @param name Element name (copied to name pool)
 * @param name_len Length of name
 * @return 0 on success, -1 on error
 */
int writer_push_element(TaurusXMLWriter* w, const char* name, size_t name_len);

/**
 * Pop element from stack
 *
 * @param w Writer handle
 * @param name Output: popped element name
 * @param name_len Output: popped element name length
 * @return 0 on success, -1 on error (empty stack)
 */
int writer_pop_element(TaurusXMLWriter* w, const char** name, size_t* name_len);

/**
 * Get current element from stack
 *
 * @param w Writer handle
 * @return Current element or NULL if stack is empty
 */
ElementStackEntry* writer_current_element(TaurusXMLWriter* w);

/**
 * Write indentation for pretty-printing
 *
 * @param w Writer handle
 */
void writer_write_indent(TaurusXMLWriter* w);

/**
 * Close current start tag (if open)
 *
 * Called before writing content or ending element.
 *
 * @param w Writer handle
 */
void writer_close_start_tag(TaurusXMLWriter* w);

/**
 * Set writer error
 *
 * @param w Writer handle
 * @param error Error code
 * @param message Error message
 */
void writer_set_error(TaurusXMLWriter* w, int error, const char* message);

/**
 * Copy string to writer's name pool
 *
 * @param w Writer handle
 * @param str String to copy
 * @param len Length of string
 * @return Pointer to copied string in pool
 */
char* writer_copy_to_pool(TaurusXMLWriter* w, const char* str, size_t len);

/* ============================================================================
 * XML Name Validation
 * ============================================================================ */

/**
 * Check if string is a valid XML name
 *
 * @param name Name to check
 * @param len Length of name
 * @return 1 if valid, 0 if invalid
 */
int is_valid_xml_name(const char* name, size_t len);

/**
 * Check if string is a valid XML NCName (no-colon name)
 *
 * @param name Name to check
 * @param len Length of name
 * @return 1 if valid, 0 if invalid
 */
int is_valid_xml_ncname(const char* name, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_WRITER_INTERNAL_H */
