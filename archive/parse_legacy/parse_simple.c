/* parse_simple.c - Simple XML parser stub for Phase 1
 * Copyright (c) 2024, Ribose Inc.
 *
 * Minimal XML parser for testing the public API.
 * This is a STUB implementation - will be replaced with full parser in Phase 2.
 */

#include "leptris_internal.h"
#include <ctype.h>

/* Parser state for position tracking */
typedef struct {
    const char* input;      /* Original input (for context extraction) */
    const char* pos;        /* Current position in input */
    size_t offset;          /* Byte offset from start */
    int line;               /* Current line (1-based) */
    int column;             /* Current column (1-based) */
} ParserState;

/* Helper: Advance one character and update position */
static void advance_char(ParserState* state) {
    if (!state || !state->pos || !*state->pos) return;

    if (*state->pos == '\n') {
        state->line++;
        state->column = 1;
    } else {
        state->column++;
    }
    state->pos++;
    state->offset++;
}

/* Helper: Skip whitespace and update position */
static void skip_whitespace(ParserState* state) {
    while (state->pos && *state->pos && isspace((unsigned char)*state->pos)) {
        advance_char(state);
    }
}

/* Helper: Parse element name */
static char* parse_name(const char** p) {
    const char* start = *p;
    const char* end = start;

    /* Name: [a-zA-Z_][a-zA-Z0-9_.-]* */
    if (!isalpha((unsigned char)*end) && *end != '_') return NULL;

    end++;
    while (isalnum((unsigned char)*end) || *end == '_' || *end == '-' || *end == '.' || *end == ':') {
        end++;
    }

    size_t len = end - start;
    if (len == 0) return NULL;

    char* name = LEPTRIS_ALLOC_N(char, len + 1);
    if (!name) return NULL;

    memcpy(name, start, len);
    name[len] = '\0';
    *p = end;

    return name;
}

/* Helper: Parse attribute value */
static char* parse_attr_value(const char** p) {
    const char* pos = *p;

    /* Skip whitespace */
    while (*pos && isspace((unsigned char)*pos)) pos++;

    /* Must have = */
    if (*pos != '=') return NULL;
    pos++;

    /* Skip whitespace */
    while (*pos && isspace((unsigned char)*pos)) pos++;

    /* Must have quote */
    char quote = *pos;
    if (quote != '"' && quote != '\'') return NULL;
    pos++;

    /* Find closing quote */
    const char* start = pos;
    while (*pos && *pos != quote) pos++;

    if (*pos != quote) return NULL;

    size_t len = pos - start;
    char* value = LEPTRIS_ALLOC_N(char, len + 1);
    if (!value) return NULL;

    memcpy(value, start, len);
    value[len] = '\0';

    pos++; /* Skip closing quote */
    *p = pos;

    return value;
}

/* Helper: Parse attributes and process namespace declarations */
static void parse_attributes(const char** p, struct leptris_element* elem) {
    const char* pos = *p;

    while (*pos && *pos != '>' && *pos != '/') {
        /* Skip whitespace */
        while (*pos && isspace((unsigned char)*pos)) pos++;

        if (*pos == '>' || *pos == '/') break;

        /* Parse attribute name */
        char* name = parse_name(&pos);
        if (!name) break;

        /* Parse attribute value */
        char* value = parse_attr_value(&pos);
        if (!value) {
            LEPTRIS_FREE(name);
            break;
        }

        /* Check if this is a namespace declaration */
        if (strcmp(name, "xmlns") == 0) {
            /* Default namespace: xmlns="uri" */
            struct leptris_namespace* ns = LEPTRIS_ALLOC(struct leptris_namespace);
            if (ns) {
                ns->prefix = NULL;
                ns->uri = leptris_strdup(value);
                ns->next = elem->namespaces;
                elem->namespaces = ns;
                elem->namespaces_count++;
            }
            LEPTRIS_FREE(name);
            LEPTRIS_FREE(value);
            continue;
        } else if (strncmp(name, "xmlns:", 6) == 0) {
            /* Prefixed namespace: xmlns:prefix="uri" */
            const char* prefix = name + 6;
            struct leptris_namespace* ns = LEPTRIS_ALLOC(struct leptris_namespace);
            if (ns) {
                ns->prefix = leptris_strdup(prefix);
                ns->uri = leptris_strdup(value);
                ns->next = elem->namespaces;
                elem->namespaces = ns;
                elem->namespaces_count++;
            }
            LEPTRIS_FREE(name);
            LEPTRIS_FREE(value);
            continue;
        }

        /* Regular attribute */
        struct leptris_attribute* attr = LEPTRIS_ALLOC(struct leptris_attribute);
        if (!attr) {
            LEPTRIS_FREE(name);
            LEPTRIS_FREE(value);
            break;
        }

        attr->name = name;
        attr->value = value;
        attr->prefix = NULL;
        attr->namespace_uri = NULL;

        /* Add to element's attributes array */
        if (elem->attributes_count >= elem->attributes_capacity) {
            size_t new_cap = elem->attributes_capacity == 0 ? 4 : elem->attributes_capacity * 2;
            struct leptris_attribute** new_attrs = LEPTRIS_REALLOC_N(
                elem->attributes, struct leptris_attribute*, new_cap);
            if (!new_attrs) {
                LEPTRIS_FREE(attr->name);
                LEPTRIS_FREE(attr->value);
                LEPTRIS_FREE(attr);
                break;
            }
            elem->attributes = new_attrs;
            elem->attributes_capacity = new_cap;
        }

        elem->attributes[elem->attributes_count++] = attr;
    }

    *p = pos;
}

/* Helper: Resolve namespace URI for element */
static void resolve_element_namespace(struct leptris_element* elem) {
    if (!elem) return;

    /* Extract prefix from element name if present */
    const char* colon = strchr(elem->name, ':');
    char* prefix = NULL;

    if (colon) {
        size_t prefix_len = colon - elem->name;
        prefix = LEPTRIS_ALLOC_N(char, prefix_len + 1);
        if (prefix) {
            memcpy(prefix, elem->name, prefix_len);
            prefix[prefix_len] = '\0';
            elem->prefix = prefix;
        }
    }

    /* Search for matching namespace declaration */
    struct leptris_element* current = elem;
    while (current) {
        struct leptris_namespace* ns = current->namespaces;
        while (ns) {
            /* Check if this namespace matches our prefix */
            if (!prefix && !ns->prefix) {
                /* Default namespace match */
                elem->namespace_uri = leptris_strdup(ns->uri);
                return;
            }
            if (prefix && ns->prefix && strcmp(prefix, ns->prefix) == 0) {
                /* Prefixed namespace match */
                elem->namespace_uri = leptris_strdup(ns->uri);
                return;
            }
            ns = ns->next;
        }
        current = current->parent;
    }
}

/* Helper: Resolve namespaces recursively for element and all descendants */
static void resolve_namespaces_recursive(struct leptris_element* elem) {
    if (!elem) return;

    /* Resolve this element's namespace */
    resolve_element_namespace(elem);

    /* Recursively resolve children */
    for (size_t i = 0; i < elem->children_count; i++) {
        resolve_namespaces_recursive(elem->children[i]);
    }
}

/* Helper: Parse text content */
static char* parse_text(const char** p) {
    size_t capacity = 64;
    size_t len = 0;
    char* text = LEPTRIS_ALLOC_N(char, capacity);
    if (!text) return NULL;

    while (**p && **p != '<') {
        if (len + 1 >= capacity) {
            capacity *= 2;
            char* new_text = LEPTRIS_REALLOC_N(text, char, capacity);
            if (!new_text) {
                LEPTRIS_FREE(text);
                return NULL;
            }
            text = new_text;
        }
        text[len++] = **p;
        (*p)++;
    }

    text[len] = '\0';

    /* Trim whitespace */
    char* trimmed_start = text;
    while (*trimmed_start && isspace((unsigned char)*trimmed_start)) {
        trimmed_start++;
    }

    if (*trimmed_start == '\0') {
        LEPTRIS_FREE(text);
        return NULL;
    }

    char* result = leptris_strdup(trimmed_start);
    LEPTRIS_FREE(text);
    return result;
}

/* Forward declaration */
static struct leptris_element* parse_element(const char** p);

/* Helper: Add child to element */
static void add_child(struct leptris_element* parent, struct leptris_element* child) {
    if (!parent || !child) return;

    if (parent->children_count >= parent->children_capacity) {
        size_t new_cap = parent->children_capacity == 0 ? 4 : parent->children_capacity * 2;
        struct leptris_element** new_children = LEPTRIS_REALLOC_N(
            parent->children, struct leptris_element*, new_cap);
        if (!new_children) return;
        parent->children = new_children;
        parent->children_capacity = new_cap;
    }

    parent->children[parent->children_count++] = child;
    child->parent = parent;
}

/* Parse element: <name>content</name> */
static struct leptris_element* parse_element(const char** p) {
    /* Skip whitespace manually */
    while (**p && isspace((unsigned char)**p)) (*p)++;

    const char* pos = *p;

    /* Must start with < */
    if (*pos != '<') return NULL;
    pos++;

    /* Parse tag name */
    char* name = parse_name(&pos);
    if (!name) return NULL;

    /* Create element */
    struct leptris_element* elem = LEPTRIS_ALLOC(struct leptris_element);
    if (!elem) {
        LEPTRIS_FREE(name);
        return NULL;
    }
    memset(elem, 0, sizeof(struct leptris_element));
    elem->node_type = LEPTRIS_NODE_ELEMENT;  /* Initialize node type */
    elem->name = name;
    elem->doc_order = -1;

    /* Parse attributes (includes namespace declarations) */
    parse_attributes(&pos, elem);

    /* Self-closing tag? */
    if (*pos == '/') {
        pos++;
        if (*pos == '>') pos++;
        *p = pos;
        return elem;
    }
    if (*pos == '>') pos++;

    /* Parse content */
    while (*pos) {
        /* Skip whitespace manually */
        while (*pos && isspace((unsigned char)*pos)) pos++;

        if (*pos == '<') {
            if (*(pos + 1) == '/') {
                /* Closing tag */
                pos += 2;
                char* close_name = parse_name(&pos);
                if (close_name) {
                    LEPTRIS_FREE(close_name);
                }
                /* Skip whitespace manually */
                while (*pos && isspace((unsigned char)*pos)) pos++;
                if (*pos == '>') pos++;
                *p = pos;
                return elem;
            } else {
                /* Child element */
                struct leptris_element* child = parse_element(&pos);
                if (child) {
                    add_child(elem, child);
                    /* Note: Namespace resolution now done recursively after full tree built */
                }
            }
        } else {
            /* Text content */
            char* text = parse_text(&pos);
            if (text) {
                elem->text_content = text;
            }
        }
    }

    *p = pos;
    return elem;
}

/* Parse XML document with position tracking */
struct leptris_document* parse_xml_simple(const char* xml, size_t len) {
    /* Validate input */
    if (!xml) {
        leptris_set_error(LEPTRIS_ERROR_NULL_INPUT, "NULL input provided");
        return NULL;
    }

    if (len == 0) {
        leptris_set_error(LEPTRIS_ERROR_EMPTY_INPUT, "Empty input provided");
        return NULL;
    }

    /* Initialize parser state */
    ParserState state;
    state.input = xml;
    state.pos = xml;
    state.offset = 0;
    state.line = 1;
    state.column = 1;

    /* Create document */
    struct leptris_document* doc = LEPTRIS_ALLOC(struct leptris_document);
    if (!doc) {
        leptris_set_error(LEPTRIS_ERROR_OUT_OF_MEMORY, "Failed to allocate document");
        return NULL;
    }

    memset(doc, 0, sizeof(struct leptris_document));
    doc->ref_count = 1;

    /* Skip XML declaration and processing instructions */
    skip_whitespace(&state);

    /* Skip <?xml...?> declaration and other PIs */
    while (*state.pos == '<' && *(state.pos + 1) == '?') {
        /* Find closing ?> */
        advance_char(&state);  /* < */
        advance_char(&state);  /* ? */
        while (*state.pos && !(*state.pos == '?' && *(state.pos + 1) == '>')) {
            advance_char(&state);
        }
        if (*state.pos == '?' && *(state.pos + 1) == '>') {
            advance_char(&state);  /* ? */
            advance_char(&state);  /* > */
        }
        skip_whitespace(&state);
    }

    /* Skip comments */
    while (*state.pos == '<' && *(state.pos + 1) == '!' &&
           *(state.pos + 2) == '-' && *(state.pos + 3) == '-') {
        /* Find closing --> */
        advance_char(&state);  /* < */
        advance_char(&state);  /* ! */
        advance_char(&state);  /* - */
        advance_char(&state);  /* - */
        while (*state.pos && !(*state.pos == '-' && *(state.pos + 1) == '-' && *(state.pos + 2) == '>')) {
            advance_char(&state);
        }
        if (*state.pos == '-' && *(state.pos + 1) == '-' && *(state.pos + 2) == '>') {
            advance_char(&state);  /* - */
            advance_char(&state);  /* - */
            advance_char(&state);  /* > */
        }
        skip_whitespace(&state);
    }

    /* Check for root element */
    if (*state.pos != '<') {
        leptris_set_error_with_context(
            LEPTRIS_ERROR_INVALID_XML,
            "Expected root element",
            state.input,
            state.offset,
            state.line,
            state.column
        );
        LEPTRIS_FREE(doc);
        return NULL;
    }

    /* Parse root element (use old interface for now) */
    const char* pos = state.pos;
    doc->root = parse_element(&pos);

    if (!doc->root) {
        /* Get current position after failed parse */
        size_t failed_offset = pos - xml;
        int failed_line = state.line;
        int failed_col = state.column;

        /* Calculate actual line/column if parse advanced */
        const char* p = state.pos;
        while (p < pos) {
            if (*p == '\n') {
                failed_line++;
                failed_col = 1;
            } else {
                failed_col++;
            }
            p++;
        }

        leptris_set_error_with_context(
            LEPTRIS_ERROR_PARSE_FAILED,
            "Failed to parse root element",
            state.input,
            failed_offset,
            failed_line,
            failed_col
        );
        LEPTRIS_FREE(doc);
        return NULL;
    }

    /* Resolve namespaces for entire tree after parsing complete
     * This ensures all elements have correct namespace_uri */
    resolve_namespaces_recursive(doc->root);

    return doc;
}

/* Free element recursively */
void free_element(struct leptris_element* elem) {
    if (!elem) return;

    /* Free children */
    for (size_t i = 0; i < elem->children_count; i++) {
        free_element(elem->children[i]);
    }

    /* Free attributes and their content */
    for (size_t i = 0; i < elem->attributes_count; i++) {
        if (elem->attributes[i]) {
            if (elem->attributes[i]->name) LEPTRIS_FREE(elem->attributes[i]->name);
            if (elem->attributes[i]->value) LEPTRIS_FREE(elem->attributes[i]->value);
            if (elem->attributes[i]->prefix) LEPTRIS_FREE(elem->attributes[i]->prefix);
            if (elem->attributes[i]->namespace_uri) LEPTRIS_FREE(elem->attributes[i]->namespace_uri);
            LEPTRIS_FREE(elem->attributes[i]);
        }
    }

    /* Free arrays */
    if (elem->children) LEPTRIS_FREE(elem->children);
    if (elem->attributes) LEPTRIS_FREE(elem->attributes);

    /* Free strings */
    if (elem->name) LEPTRIS_FREE(elem->name);
    if (elem->prefix) LEPTRIS_FREE(elem->prefix);
    if (elem->namespace_uri) LEPTRIS_FREE(elem->namespace_uri);
    if (elem->text_content) LEPTRIS_FREE(elem->text_content);

    /* Free element */
    LEPTRIS_FREE(elem);
}