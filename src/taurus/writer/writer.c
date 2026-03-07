/**
 * @file writer.c
 * @brief Main StAX XML writer implementation
 *
 * Implements the streaming XML writer with:
 * - State machine for validation
 * - Element stack for proper nesting
 * - Buffered output with coalescing
 * - Entity escaping with lookup tables
 *
 * Performance techniques:
 * - Small write coalescing (256 byte threshold)
 * - Pre-computed escape lookup tables
 * - SIMD escape detection (ARM NEON / x86 SSE2)
 * - Pool-allocated element name stack
 */

#include "writer_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * Name Pool Operations
 * ============================================================================ */

#define NAME_POOL_INITIAL_SIZE 4096

static char* writer_pool_alloc(TaurusXMLWriter* w, size_t len) {
    if (!w) return NULL;

    /* Ensure capacity */
    size_t needed = w->name_pool_ptr + len + 1;  /* +1 for null terminator */
    if (needed > w->name_pool_capacity) {
        size_t new_capacity = w->name_pool_capacity * 2;
        while (new_capacity < needed) {
            new_capacity *= 2;
        }

        char* new_pool = (char*)realloc(w->name_pool, new_capacity);
        if (!new_pool) return NULL;

        w->name_pool = new_pool;
        w->name_pool_capacity = new_capacity;
    }

    char* result = w->name_pool + w->name_pool_ptr;
    w->name_pool_ptr += len + 1;

    return result;
}

char* writer_copy_to_pool(TaurusXMLWriter* w, const char* str, size_t len) {
    if (!w || !str || len == 0) return NULL;

    char* result = writer_pool_alloc(w, len);
    if (!result) return NULL;

    memcpy(result, str, len);
    result[len] = '\0';

    return result;
}

/* ============================================================================
 * Element Stack Operations
 * ============================================================================ */

int writer_push_element(TaurusXMLWriter* w, const char* name, size_t name_len) {
    if (!w || !name || name_len == 0) return -1;

    /* Grow stack if needed */
    if (w->stack_depth >= w->stack_capacity) {
        size_t new_capacity = w->stack_capacity * 2;
        if (new_capacity == 0) new_capacity = WRITER_ELEMENT_STACK_SIZE;

        ElementStackEntry* new_stack = (ElementStackEntry*)realloc(
            w->element_stack,
            new_capacity * sizeof(ElementStackEntry)
        );
        if (!new_stack) return -1;

        w->element_stack = new_stack;
        w->stack_capacity = new_capacity;
    }

    /* Copy name to pool */
    char* name_copy = writer_copy_to_pool(w, name, name_len);
    if (!name_copy) return -1;

    /* Push to stack */
    ElementStackEntry* entry = &w->element_stack[w->stack_depth++];
    entry->name = name_copy;
    entry->name_len = name_len;
    entry->has_content = 0;
    entry->has_children = 0;

    return 0;
}

int writer_pop_element(TaurusXMLWriter* w, const char** name, size_t* name_len) {
    if (!w || w->stack_depth == 0) return -1;

    ElementStackEntry* entry = &w->element_stack[--w->stack_depth];
    if (name) *name = entry->name;
    if (name_len) *name_len = entry->name_len;

    return 0;
}

ElementStackEntry* writer_current_element(TaurusXMLWriter* w) {
    if (!w || w->stack_depth == 0) return NULL;
    return &w->element_stack[w->stack_depth - 1];
}

/* ============================================================================
 * Error Handling
 * ============================================================================ */

void writer_set_error(TaurusXMLWriter* w, int error, const char* message) {
    if (!w) return;

    w->last_error = error;
    w->state = WRITER_STATE_ERROR;

    if (message) {
        strncpy(w->error_message, message, sizeof(w->error_message) - 1);
        w->error_message[sizeof(w->error_message) - 1] = '\0';
    } else {
        w->error_message[0] = '\0';
    }
}

/* ============================================================================
 * Indentation
 * ============================================================================ */

void writer_write_indent(TaurusXMLWriter* w) {
    if (!w || w->opts.indent <= 0) return;

    int spaces = (int)w->stack_depth * w->opts.indent;

    /* Write indent */
    for (int i = 0; i < spaces; i++) {
        buffer_write_char(&w->buffer, ' ');
    }
}

void writer_close_start_tag(TaurusXMLWriter* w) {
    if (!w || !w->in_start_tag) return;

    buffer_write_char(&w->buffer, '>');
    w->in_start_tag = 0;
}

/* ============================================================================
 * XML Name Validation
 * ============================================================================ */

static int is_name_start_char(int c) {
    return c == ':' || c == '_' ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= 0xC0 && c <= 0xD6) ||
           (c >= 0xD8 && c <= 0xF6) ||
           (c >= 0xF8);
}

static int is_name_char(int c) {
    return is_name_start_char(c) ||
           c == '-' || c == '.' ||
           (c >= '0' && c <= '9') ||
           c == 0xB7;
}

int is_valid_xml_name(const char* name, size_t len) {
    if (!name || len == 0) return 0;

    /* First character must be name start char */
    if (!is_name_start_char((unsigned char)name[0])) return 0;

    /* Rest must be name chars */
    for (size_t i = 1; i < len; i++) {
        if (!is_name_char((unsigned char)name[i])) return 0;
    }

    return 1;
}

int is_valid_xml_ncname(const char* name, size_t len) {
    if (!is_valid_xml_name(name, len)) return 0;

    /* NCName cannot contain colon */
    for (size_t i = 0; i < len; i++) {
        if (name[i] == ':') return 0;
    }

    return 1;
}

/* ============================================================================
 * Writer Creation
 * ============================================================================ */

static TaurusXMLWriter* writer_create_internal(TaurusWriterOptions* opts) {
    TaurusXMLWriter* w = (TaurusXMLWriter*)calloc(1, sizeof(TaurusXMLWriter));
    if (!w) return NULL;

    /* Initialize options */
    if (opts) {
        w->opts = *opts;
    } else {
        w->opts = (TaurusWriterOptions)TAURUS_WRITER_OPTIONS_DEFAULT;
    }

    /* Initialize state */
    w->state = WRITER_STATE_PROLOG;

    /* Initialize element stack */
    w->element_stack = (ElementStackEntry*)malloc(
        WRITER_ELEMENT_STACK_SIZE * sizeof(ElementStackEntry)
    );
    if (!w->element_stack) {
        free(w);
        return NULL;
    }
    w->stack_capacity = WRITER_ELEMENT_STACK_SIZE;

    /* Initialize name pool */
    w->name_pool = (char*)malloc(NAME_POOL_INITIAL_SIZE);
    if (!w->name_pool) {
        free(w->element_stack);
        free(w);
        return NULL;
    }
    w->name_pool_capacity = NAME_POOL_INITIAL_SIZE;

    return w;
}

TaurusXMLWriter* taurus_writer_create_file(const char* filepath, const char* encoding) {
    return taurus_writer_create_file_ex(filepath, &(TaurusWriterOptions){
        .indent = 0,
        .pretty_print = 0,
        .xml_declaration = 1,
        .encoding = encoding
    });
}

TaurusXMLWriter* taurus_writer_create_stream(FILE* stream, const char* encoding) {
    return taurus_writer_create_stream_ex(stream, &(TaurusWriterOptions){
        .indent = 0,
        .pretty_print = 0,
        .xml_declaration = 1,
        .encoding = encoding
    });
}

TaurusXMLWriter* taurus_writer_create_callback(TaurusWriteCallback cb, void* ctx, const char* encoding) {
    return taurus_writer_create_callback_ex(cb, ctx, &(TaurusWriterOptions){
        .indent = 0,
        .pretty_print = 0,
        .xml_declaration = 1,
        .encoding = encoding
    });
}

TaurusXMLWriter* taurus_writer_create_callback_ex(TaurusWriteCallback cb, void* ctx, TaurusWriterOptions* opts) {
    if (!cb) return NULL;

    TaurusXMLWriter* w = writer_create_internal(opts);
    if (!w) return NULL;

    if (buffer_init(&w->buffer, cb, ctx) != 0) {
        free(w->name_pool);
        free(w->element_stack);
        free(w);
        return NULL;
    }

    return w;
}

TaurusXMLWriter* taurus_writer_create_file_ex(const char* filepath, TaurusWriterOptions* opts) {
    if (!filepath) return NULL;

    FILE* file = fopen(filepath, "wb");
    if (!file) return NULL;

    TaurusXMLWriter* w = writer_create_internal(opts);
    if (!w) {
        fclose(file);
        return NULL;
    }

    if (buffer_init_file(&w->buffer, file, 1) != 0) {
        free(w->name_pool);
        free(w->element_stack);
        free(w);
        fclose(file);
        return NULL;
    }

    return w;
}

TaurusXMLWriter* taurus_writer_create_stream_ex(FILE* stream, TaurusWriterOptions* opts) {
    if (!stream) return NULL;

    TaurusXMLWriter* w = writer_create_internal(opts);
    if (!w) return NULL;

    if (buffer_init_file(&w->buffer, stream, 0) != 0) {
        free(w->name_pool);
        free(w->element_stack);
        free(w);
        return NULL;
    }

    return w;
}

void taurus_writer_free(TaurusXMLWriter* w) {
    if (!w) return;

    /* Flush remaining data */
    buffer_flush(&w->buffer);

    /* Free buffer */
    buffer_cleanup(&w->buffer);

    /* Free element stack */
    if (w->element_stack) {
        free(w->element_stack);
    }

    /* Free name pool */
    if (w->name_pool) {
        free(w->name_pool);
    }

    free(w);
}

/* ============================================================================
 * Document Structure
 * ============================================================================ */

int taurus_writer_start_document(TaurusXMLWriter* w, const char* version,
                                  const char* encoding, int standalone) {
    if (!w) return TAURUS_WRITER_ERROR_INVALID_ARG;

    if (w->state != WRITER_STATE_PROLOG) {
        writer_set_error(w, TAURUS_WRITER_ERROR_INVALID_STATE,
                         "start_document called in invalid state");
        return w->last_error;
    }

    if (!w->opts.xml_declaration) {
        w->state = WRITER_STATE_DOCUMENT_STARTED;
        w->document_started = 1;
        return TAURUS_WRITER_OK;
    }

    /* Write XML declaration */
    const char* ver = version ? version : "1.0";
    const char* enc = encoding ? encoding : (w->opts.encoding ? w->opts.encoding : "UTF-8");

    buffer_write_string(&w->buffer, "<?xml version=\"");
    buffer_write_string(&w->buffer, ver);
    buffer_write_string(&w->buffer, "\" encoding=\"");
    buffer_write_string(&w->buffer, enc);
    buffer_write_char(&w->buffer, '"');

    if (standalone >= 0) {
        buffer_write_string(&w->buffer, " standalone=\"");
        buffer_write_string(&w->buffer, standalone ? "yes" : "no");
        buffer_write_char(&w->buffer, '"');
    }

    buffer_write_string(&w->buffer, "?>");

    if (w->opts.pretty_print) {
        buffer_write_char(&w->buffer, '\n');
    }

    w->state = WRITER_STATE_DOCUMENT_STARTED;
    w->document_started = 1;

    return w->buffer.error ? w->buffer.error : TAURUS_WRITER_OK;
}

int taurus_writer_end_document(TaurusXMLWriter* w) {
    if (!w) return TAURUS_WRITER_ERROR_INVALID_ARG;

    if (w->stack_depth > 0) {
        writer_set_error(w, TAURUS_WRITER_ERROR_NESTING,
                         "Unclosed elements remain");
        return w->last_error;
    }

    /* Flush buffer */
    int result = buffer_flush(&w->buffer);
    if (result != 0) {
        return result;
    }

    w->state = WRITER_STATE_EPILOG;
    w->document_ended = 1;

    return TAURUS_WRITER_OK;
}

/* ============================================================================
 * Elements
 * ============================================================================ */

int taurus_writer_start_element(TaurusXMLWriter* w, const char* name) {
    if (!w || !name) return TAURUS_WRITER_ERROR_INVALID_ARG;

    size_t name_len = strlen(name);
    if (name_len == 0) {
        writer_set_error(w, TAURUS_WRITER_ERROR_INVALID_NAME, "Empty element name");
        return w->last_error;
    }

    /* Validate name if requested */
    if (w->opts.validate_names && !is_valid_xml_name(name, name_len)) {
        writer_set_error(w, TAURUS_WRITER_ERROR_INVALID_NAME, "Invalid element name");
        return w->last_error;
    }

    /* Check state */
    if (w->state == WRITER_STATE_ELEMENT_OPEN) {
        /* Close previous start tag */
        writer_close_start_tag(w);

        /* Mark parent as having children */
        ElementStackEntry* parent = writer_current_element(w);
        if (parent) parent->has_children = 1;
    } else if (w->state == WRITER_STATE_PROLOG || w->state == WRITER_STATE_DOCUMENT_STARTED) {
        /* Starting root element */
    } else if (w->state != WRITER_STATE_CONTENT && w->state != WRITER_STATE_ELEMENT_CLOSED) {
        writer_set_error(w, TAURUS_WRITER_ERROR_INVALID_STATE,
                         "start_element called in invalid state");
        return w->last_error;
    }

    /* Write indentation if pretty-printing */
    if (w->opts.pretty_print && w->stack_depth > 0) {
        buffer_write_char(&w->buffer, '\n');
        writer_write_indent(w);
    }

    /* Write start tag */
    buffer_write_char(&w->buffer, '<');
    buffer_write_raw(&w->buffer, name, name_len);

    /* Push to stack */
    if (writer_push_element(w, name, name_len) != 0) {
        writer_set_error(w, TAURUS_WRITER_ERROR_MEMORY, "Failed to push element");
        return w->last_error;
    }

    w->state = WRITER_STATE_ELEMENT_OPEN;
    w->in_start_tag = 1;

    return w->buffer.error ? w->buffer.error : TAURUS_WRITER_OK;
}

int taurus_writer_start_element_ns(TaurusXMLWriter* w, const char* prefix,
                                    const char* uri, const char* localname) {
    if (!w || !localname) return TAURUS_WRITER_ERROR_INVALID_ARG;

    /* Build qname */
    char qname[256];
    size_t qname_len;

    if (prefix && *prefix) {
        qname_len = snprintf(qname, sizeof(qname), "%s:%s", prefix, localname);
    } else {
        qname_len = snprintf(qname, sizeof(qname), "%s", localname);
    }

    if (qname_len >= sizeof(qname)) {
        writer_set_error(w, TAURUS_WRITER_ERROR_INVALID_NAME, "Element name too long");
        return w->last_error;
    }

    /* Write element */
    int result = taurus_writer_start_element(w, qname);
    if (result != TAURUS_WRITER_OK) return result;

    /* Write namespace declaration if URI provided */
    if (uri) {
        result = taurus_writer_namespace(w, prefix, uri);
    }

    return result;
}

int taurus_writer_end_element(TaurusXMLWriter* w) {
    if (!w) return TAURUS_WRITER_ERROR_INVALID_ARG;

    if (w->stack_depth == 0) {
        writer_set_error(w, TAURUS_WRITER_ERROR_NESTING, "No element to close");
        return w->last_error;
    }

    /* Get current element */
    ElementStackEntry* entry = writer_current_element(w);
    if (!entry) {
        writer_set_error(w, TAURUS_WRITER_ERROR_NESTING, "Element stack error");
        return w->last_error;
    }

    /* Close start tag if still open */
    if (w->in_start_tag) {
        /* Self-closing element */
        buffer_write_string(&w->buffer, "/>");

        if (w->opts.pretty_print && w->stack_depth > 1) {
            /* Will add newline after */
        }
    } else {
        /* Write indentation if pretty-printing and has element children */
        if (w->opts.pretty_print && entry->has_children) {
            buffer_write_char(&w->buffer, '\n');
            writer_write_indent(w);
        }

        /* Write end tag */
        buffer_write_string(&w->buffer, "</");
        buffer_write_raw(&w->buffer, entry->name, entry->name_len);
        buffer_write_char(&w->buffer, '>');
    }

    /* Pop from stack */
    writer_pop_element(w, NULL, NULL);

    /* Update state */
    w->in_start_tag = 0;
    if (w->stack_depth == 0) {
        w->state = WRITER_STATE_ELEMENT_CLOSED;
    } else {
        w->state = WRITER_STATE_CONTENT;
    }

    return w->buffer.error ? w->buffer.error : TAURUS_WRITER_OK;
}

int taurus_writer_empty_element(TaurusXMLWriter* w, const char* name) {
    if (!w || !name) return TAURUS_WRITER_ERROR_INVALID_ARG;

    int result = taurus_writer_start_element(w, name);
    if (result != TAURUS_WRITER_OK) return result;

    return taurus_writer_end_element(w);
}

int taurus_writer_empty_element_ns(TaurusXMLWriter* w, const char* prefix,
                                    const char* uri, const char* localname) {
    if (!w || !localname) return TAURUS_WRITER_ERROR_INVALID_ARG;

    int result = taurus_writer_start_element_ns(w, prefix, uri, localname);
    if (result != TAURUS_WRITER_OK) return result;

    return taurus_writer_end_element(w);
}

/* ============================================================================
 * Attributes
 * ============================================================================ */

int taurus_writer_attribute(TaurusXMLWriter* w, const char* name, const char* value) {
    if (!w || !name) return TAURUS_WRITER_ERROR_INVALID_ARG;
    return taurus_writer_attribute_len(w, name, value, value ? strlen(value) : 0);
}

int taurus_writer_attribute_len(TaurusXMLWriter* w, const char* name,
                                 const char* value, size_t value_len) {
    if (!w || !name) return TAURUS_WRITER_ERROR_INVALID_ARG;

    if (!w->in_start_tag) {
        writer_set_error(w, TAURUS_WRITER_ERROR_INVALID_STATE,
                         "Attribute must be written after start_element");
        return w->last_error;
    }

    size_t name_len = strlen(name);
    if (name_len == 0) {
        writer_set_error(w, TAURUS_WRITER_ERROR_INVALID_NAME, "Empty attribute name");
        return w->last_error;
    }

    /* Validate name if requested */
    if (w->opts.validate_names && !is_valid_xml_name(name, name_len)) {
        writer_set_error(w, TAURUS_WRITER_ERROR_INVALID_NAME, "Invalid attribute name");
        return w->last_error;
    }

    /* Write attribute */
    buffer_write_char(&w->buffer, ' ');
    buffer_write_raw(&w->buffer, name, name_len);
    buffer_write_string(&w->buffer, "=\"");

    /* Write escaped value */
    if (value && value_len > 0) {
        buffer_write_escaped(&w->buffer, value, value_len, 1);
    }

    buffer_write_char(&w->buffer, '"');

    return w->buffer.error ? w->buffer.error : TAURUS_WRITER_OK;
}

int taurus_writer_attribute_ns(TaurusXMLWriter* w, const char* prefix,
                                const char* uri, const char* localname,
                                const char* value) {
    if (!w || !localname) return TAURUS_WRITER_ERROR_INVALID_ARG;

    /* Build qname */
    char qname[256];

    if (prefix && *prefix) {
        snprintf(qname, sizeof(qname), "%s:%s", prefix, localname);
    } else {
        snprintf(qname, sizeof(qname), "%s", localname);
    }

    /* Write attribute */
    int result = taurus_writer_attribute(w, qname, value);
    if (result != TAURUS_WRITER_OK) return result;

    /* Write namespace if needed (not yet declared) */
    /* For now, always write namespace declaration on first use */
    (void)uri;  /* TODO: Track declared namespaces */

    return TAURUS_WRITER_OK;
}

/* ============================================================================
 * Text Content
 * ============================================================================ */

int taurus_writer_characters(TaurusXMLWriter* w, const char* text) {
    if (!w) return TAURUS_WRITER_ERROR_INVALID_ARG;
    if (!text) return TAURUS_WRITER_OK;  /* NULL is no-op */

    return taurus_writer_characters_len(w, text, strlen(text));
}

int taurus_writer_characters_len(TaurusXMLWriter* w, const char* text, size_t len) {
    if (!w) return TAURUS_WRITER_ERROR_INVALID_ARG;
    if (!text || len == 0) return TAURUS_WRITER_OK;  /* Empty is no-op */

    /* Close start tag if open */
    if (w->in_start_tag) {
        writer_close_start_tag(w);
    }

    /* Check state */
    if (w->state != WRITER_STATE_ELEMENT_OPEN && w->state != WRITER_STATE_CONTENT) {
        writer_set_error(w, TAURUS_WRITER_ERROR_INVALID_STATE,
                         "characters called in invalid state");
        return w->last_error;
    }

    /* Mark element as having content */
    ElementStackEntry* entry = writer_current_element(w);
    if (entry) entry->has_content = 1;

    /* Write escaped text */
    buffer_write_escaped(&w->buffer, text, len, 0);

    w->state = WRITER_STATE_CONTENT;

    return w->buffer.error ? w->buffer.error : TAURUS_WRITER_OK;
}

int taurus_writer_cdata(TaurusXMLWriter* w, const char* data) {
    if (!w) return TAURUS_WRITER_ERROR_INVALID_ARG;
    if (!data) return TAURUS_WRITER_OK;

    return taurus_writer_cdata_len(w, data, strlen(data));
}

int taurus_writer_cdata_len(TaurusXMLWriter* w, const char* data, size_t len) {
    if (!w) return TAURUS_WRITER_ERROR_INVALID_ARG;
    if (!data || len == 0) return TAURUS_WRITER_OK;

    /* Check for ]]> in data (not allowed in CDATA) */
    for (size_t i = 0; i + 2 < len; i++) {
        if (data[i] == ']' && data[i+1] == ']' && data[i+2] == '>') {
            writer_set_error(w, TAURUS_WRITER_ERROR_CDATA,
                             "CDATA section cannot contain ]]>");
            return w->last_error;
        }
    }

    /* Close start tag if open */
    if (w->in_start_tag) {
        writer_close_start_tag(w);
    }

    /* Check state */
    if (w->state != WRITER_STATE_ELEMENT_OPEN && w->state != WRITER_STATE_CONTENT) {
        writer_set_error(w, TAURUS_WRITER_ERROR_INVALID_STATE,
                         "cdata called in invalid state");
        return w->last_error;
    }

    /* Mark element as having content */
    ElementStackEntry* entry = writer_current_element(w);
    if (entry) entry->has_content = 1;

    /* Write CDATA section */
    buffer_write_string(&w->buffer, "<![CDATA[");
    buffer_write_raw(&w->buffer, data, len);
    buffer_write_string(&w->buffer, "]]>");

    w->state = WRITER_STATE_CONTENT;

    return w->buffer.error ? w->buffer.error : TAURUS_WRITER_OK;
}

/* ============================================================================
 * Other Node Types
 * ============================================================================ */

int taurus_writer_comment(TaurusXMLWriter* w, const char* text) {
    if (!w) return TAURUS_WRITER_ERROR_INVALID_ARG;
    if (!text) return TAURUS_WRITER_OK;

    size_t len = strlen(text);

    /* Check for -- in comment (not allowed) */
    for (size_t i = 0; i + 1 < len; i++) {
        if (text[i] == '-' && text[i+1] == '-') {
            writer_set_error(w, TAURUS_WRITER_ERROR_COMMENT,
                             "Comment cannot contain --");
            return w->last_error;
        }
    }

    /* Check for - at end (not allowed) */
    if (len > 0 && text[len-1] == '-') {
        writer_set_error(w, TAURUS_WRITER_ERROR_COMMENT,
                         "Comment cannot end with -");
        return w->last_error;
    }

    /* Close start tag if open */
    if (w->in_start_tag) {
        writer_close_start_tag(w);
    }

    /* Write indentation if pretty-printing */
    if (w->opts.pretty_print && w->stack_depth > 0) {
        buffer_write_char(&w->buffer, '\n');
        writer_write_indent(w);
    }

    /* Write comment */
    buffer_write_string(&w->buffer, "<!--");
    buffer_write_raw(&w->buffer, text, len);
    buffer_write_string(&w->buffer, "-->");

    /* Mark element as having content if inside element */
    if (w->stack_depth > 0) {
        ElementStackEntry* entry = writer_current_element(w);
        if (entry) entry->has_content = 1;
    }

    return w->buffer.error ? w->buffer.error : TAURUS_WRITER_OK;
}

int taurus_writer_processing_instruction(TaurusXMLWriter* w, const char* target,
                                          const char* data) {
    if (!w || !target) return TAURUS_WRITER_ERROR_INVALID_ARG;

    size_t target_len = strlen(target);
    if (target_len == 0) {
        writer_set_error(w, TAURUS_WRITER_ERROR_INVALID_NAME, "Empty PI target");
        return w->last_error;
    }

    /* Validate target name */
    if (w->opts.validate_names && !is_valid_xml_name(target, target_len)) {
        writer_set_error(w, TAURUS_WRITER_ERROR_INVALID_NAME, "Invalid PI target");
        return w->last_error;
    }

    /* Close start tag if open */
    if (w->in_start_tag) {
        writer_close_start_tag(w);
    }

    /* Write indentation if pretty-printing */
    if (w->opts.pretty_print && w->stack_depth > 0) {
        buffer_write_char(&w->buffer, '\n');
        writer_write_indent(w);
    }

    /* Write PI */
    buffer_write_string(&w->buffer, "<?");
    buffer_write_raw(&w->buffer, target, target_len);

    if (data && *data) {
        buffer_write_char(&w->buffer, ' ');
        buffer_write_string(&w->buffer, data);
    }

    buffer_write_string(&w->buffer, "?>");

    /* Mark element as having content if inside element */
    if (w->stack_depth > 0) {
        ElementStackEntry* entry = writer_current_element(w);
        if (entry) entry->has_content = 1;
    }

    return w->buffer.error ? w->buffer.error : TAURUS_WRITER_OK;
}

/* ============================================================================
 * Namespace Declarations
 * ============================================================================ */

int taurus_writer_namespace(TaurusXMLWriter* w, const char* prefix, const char* uri) {
    if (!w || !uri) return TAURUS_WRITER_ERROR_INVALID_ARG;

    if (!w->in_start_tag) {
        writer_set_error(w, TAURUS_WRITER_ERROR_INVALID_STATE,
                         "Namespace must be declared in start tag");
        return w->last_error;
    }

    /* Build attribute name */
    char attr_name[256];

    if (prefix && *prefix) {
        snprintf(attr_name, sizeof(attr_name), "xmlns:%s", prefix);
    } else {
        snprintf(attr_name, sizeof(attr_name), "xmlns");
    }

    /* Write as attribute */
    return taurus_writer_attribute(w, attr_name, uri);
}

/* ============================================================================
 * Flush and Error Handling
 * ============================================================================ */

int taurus_writer_flush(TaurusXMLWriter* w) {
    if (!w) return TAURUS_WRITER_ERROR_INVALID_ARG;

    return buffer_flush(&w->buffer);
}

int taurus_writer_get_error(TaurusXMLWriter* w) {
    if (!w) return TAURUS_WRITER_ERROR_INVALID_ARG;
    return w->last_error;
}

const char* taurus_writer_get_error_message(TaurusXMLWriter* w) {
    if (!w) return NULL;
    return w->error_message[0] ? w->error_message : NULL;
}
