/* parse_helpers.h - Helper functions and structures for XML parsing
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * Consolidates parsing utilities extracted from ext/taurus helper modules:
 * - Character classification and scanning (from parse_inline.h)
 * - Attribute stack (from attr_stack.h, converted to pure C)
 * - String interning (from symbol_cache.h, converted to pure C)
 * - Parse structures (from parse_structures.h, converted to pure C)
 */

#ifndef TAURUS_PARSE_HELPERS_H
#define TAURUS_PARSE_HELPERS_H

#include "taurus_internal.h"
#include "taurus_memory.h"
#include "chartype.h"
#include "simd_helpers.h"

/* ==================================================================
 * CHARACTER CLASSIFICATION AND SCANNING
 * =================================================================
 * Inline hot-path functions for parser (from parse_inline.h)
 */

/* Check if character is whitespace - uses fast table lookup */
static inline int taurus_is_whitespace(char c) {
    return is_whitespace_fast(c);
}

/* Skip whitespace using SIMD vectorized scanning */
static inline void taurus_skip_whitespace(const char **pos, const char *end) {
    *pos = simd_skip_whitespace(*pos, end);
}

/* Check if character is valid for XML names - uses fast table lookup */
static inline int taurus_is_name_char(char c) {
    return is_name_char_fast(c);
}

/* Peek at current character without advancing */
static inline char taurus_peek_char(const char *pos, const char *end) {
    if (pos < end) {
        return *pos;
    }
    return '\0';
}

/* Get current character and advance position */
static inline char taurus_next_char(const char **pos, const char *end) {
    if (*pos < end) {
        return *(*pos)++;
    }
    return '\0';
}

/* ==================================================================
 * ATTRIBUTE STACK
 * =================================================================
 * Stack-based attribute storage with inline base array
 * Converted from ext/taurus/attr_stack.h (Ruby memory → pure C)
 */

#define TAURUS_ATTR_STACK_BASE_SIZE 8

/* Attribute structure for parsing (distinct from final taurus_attribute) */
typedef struct parse_attribute {
    const char *name;   /* Attribute name (pointer into parse buffer) */
    const char *value;  /* Attribute value (pointer into parse buffer) */
} ParseAttribute;

/* Attribute stack with inline base array to avoid heap allocation for small elements */
typedef struct taurus_attr_stack {
    ParseAttribute base[TAURUS_ATTR_STACK_BASE_SIZE];  /* 8 inline slots */
    ParseAttribute *head;     /* Start of allocated space */
    ParseAttribute *end;      /* End of allocated space */
    ParseAttribute *tail;     /* Current position (one past last element) */
} TaurusAttrStack;

/* Initialize attribute stack */
static inline void taurus_attr_stack_init(TaurusAttrStack *stack) {
    stack->head = stack->base;
    stack->end = stack->base + TAURUS_ATTR_STACK_BASE_SIZE;
    stack->tail = stack->head;
}

/* Cleanup attribute stack (only needed if heap was allocated) */
static inline void taurus_attr_stack_cleanup(TaurusAttrStack *stack) {
    if (stack->base != stack->head) {
        /* Heap memory was allocated, free it */
        taurus_free(stack->head);
        stack->head = stack->base;
    }
}

/* Push attribute onto stack, growing if necessary */
static inline void taurus_attr_stack_push(TaurusAttrStack *stack,
                                           const char *name,
                                           const char *value) {
    /* Check if we need to grow */
    if (stack->end <= stack->tail + 1) {
        size_t len = stack->end - stack->head;
        size_t toff = stack->tail - stack->head;
        
        if (stack->base == stack->head) {
            /* First time exceeding base array - allocate on heap */
            stack->head = (ParseAttribute*)taurus_malloc(
                sizeof(ParseAttribute) * (len + TAURUS_ATTR_STACK_BASE_SIZE)
            );
            memcpy(stack->head, stack->base, sizeof(ParseAttribute) * len);
        } else {
            /* Already on heap - reallocate */
            stack->head = (ParseAttribute*)taurus_realloc(
                stack->head,
                sizeof(ParseAttribute) * (len + TAURUS_ATTR_STACK_BASE_SIZE)
            );
        }
        stack->tail = stack->head + toff;
        stack->end = stack->head + len + TAURUS_ATTR_STACK_BASE_SIZE;
    }
    
    /* Add attribute */
    stack->tail->name = name;
    stack->tail->value = value;
    stack->tail++;
}

/* Get number of attributes in stack */
static inline size_t taurus_attr_stack_size(const TaurusAttrStack *stack) {
    return stack->tail - stack->head;
}

/* Get pointer to attribute array */
static inline ParseAttribute *taurus_attr_stack_to_array(TaurusAttrStack *stack) {
    return stack->head;
}

/* Get attribute at index */
static inline ParseAttribute *taurus_attr_stack_at(TaurusAttrStack *stack, size_t index) {
    if (index < taurus_attr_stack_size(stack)) {
        return &stack->head[index];
    }
    return NULL;
}

/* ==================================================================
 * STRING INTERNING
 * =================================================================
 * String interning table for attribute names
 * Replaces symbol_cache.h (Ruby symbols → interned strings)
 * 
 * PURPOSE: Eliminate duplicate string allocations. Multiple attributes
 * with the same name (e.g., "id") share a single interned string.
 * Compare by pointer equality instead of strcmp().
 */

#define STRING_INTERN_TABLE_SIZE 128  /* Power of 2 for fast modulo */

typedef struct string_intern_entry {
    char *key;          /* Interned string (owned copy) */
    size_t hash;        /* Cached hash value */
} StringInternEntry;

typedef struct string_intern_table {
    StringInternEntry entries[STRING_INTERN_TABLE_SIZE];
    int initialized;
} StringInternTable;

/* Pre-interned strings for top 10 most common attributes (HTML/XML)
 * Avoids hash table lookup for 80% of attribute accesses */
extern const char *g_intern_id;
extern const char *g_intern_class;
extern const char *g_intern_type;
extern const char *g_intern_name;
extern const char *g_intern_href;
extern const char *g_intern_src;
extern const char *g_intern_xmlns;
extern const char *g_intern_style;
extern const char *g_intern_value;
extern const char *g_intern_lang;

/* Initialize string interning table */
void string_intern_table_init(StringInternTable *table);

/* Get or create interned string
 * Returns pointer to interned string (owned by table)
 * Same string always returns same pointer (pointer equality) */
const char* string_intern_get(StringInternTable *table, const char *str, size_t len);

/* Fast-path for common attributes (bypasses hash table)
 * Returns interned string directly for top 10 attributes, otherwise NULL */
const char* string_intern_get_fast(const char *name, size_t len);

/* Clear the interning table (for testing/benchmarking) */
void string_intern_table_clear(StringInternTable *table);

/* Free all interned strings */
void string_intern_table_free(StringInternTable *table);

/* ==================================================================
 * PARSE STRUCTURES
 * =================================================================
 * Temporary structures during parsing, converted from parse_structures.h
 * (VALUE → struct taurus_element*)
 */

/* Parsed element structure - pure C, no Ruby objects
 * This structure holds parsed element data temporarily
 * before taurus_element structures are created */
typedef struct parsed_element {
    char *name;              /* Element name (pointer into buffer) */
    char *prefix;            /* Namespace prefix (pointer into buffer) */
    TaurusAttrStack *attrs;  /* Attributes (stack-based) */
    int has_children;        /* Flag for whether element has child nodes */
    int is_self_closing;     /* Flag for self-closing elements */
} ParsedElement;

/* Initialize a ParsedElement structure */
static inline void parsed_element_init(ParsedElement *elem, TaurusAttrStack *attr_stack) {
    elem->name = NULL;
    elem->prefix = NULL;
    elem->attrs = attr_stack;
    elem->has_children = 0;
    elem->is_self_closing = 0;
}

/* Parser callbacks - builds C DOM structures
 * These callbacks are invoked with parsed C data during parsing.
 * The callback implementation creates taurus_element structures from the C data. */
typedef struct parse_callbacks {
    /* Called when element start tag is parsed
     * context: Parent element (or NULL for document root)
     * elem: Parsed element data (C structure)
     * Returns: Newly created element (for parent tracking) */
    struct taurus_element* (*start_element)(struct taurus_element *context, ParsedElement *elem);

    /* Called when element end tag is parsed
     * context: Parent element
     * name: Element name being closed */
    void (*end_element)(struct taurus_element *context, const char *name);

    /* Called when text content is parsed
     * context: Current element
     * text: Text content (pointer into buffer) */
    void (*add_text)(struct taurus_element *context, const char *text);

    /* Called when comment is parsed
     * context: Current element
     * comment: Comment text (pointer into buffer) */
    void (*add_comment)(struct taurus_element *context, const char *comment);

    /* Called when CDATA is parsed
     * context: Current element
     * cdata: CDATA content (pointer into buffer)
     * len: Length of CDATA */
    void (*add_cdata)(struct taurus_element *context, const char *cdata, size_t len);
} ParseCallbacks;

#endif /* TAURUS_PARSE_HELPERS_H */