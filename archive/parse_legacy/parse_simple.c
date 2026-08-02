/* parse_simple.c - Simple XML parser stub for Phase 1
 * Copyright (c) 2024, Ribose Inc.
 *
 * Minimal XML parser for testing the public API.
 * This is a STUB implementation - will be replaced with full parser in Phase 2.
 */

#include "taurus_internal.h"
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

    char* name = TAURUS_ALLOC_N(char, len + 1);
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
    char* value = TAURUS_ALLOC_N(char, len + 1);
    if (!value) return NULL;

    memcpy(value, start, len);
    value[len] = '\0';

    pos++; /* Skip closing quote */
    *p = pos;

    return value;
}

/* Helper: Parse attributes and process namespace declarations */
static void parse_attributes(const char** p, struct taurus_element* elem) {
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
            TAURUS_FREE(name);
            break;
        }

        /* Check if this is a namespace declaration */
        if (strcmp(name, "xmlns") == 0) {
            /* Default namespace: xmlns="uri" */
            struct taurus_namespace* ns = TAURUS_ALLOC(struct taurus_namespace);
            if (ns) {
                ns->prefix = NULL;
                ns->uri = taurus_strdup(value);
                ns->next = elem->namespaces;
                elem->namespaces = ns;
                elem->namespaces_count++;
            }
            TAURUS_FREE(name);
            TAURUS_FREE(value);
            continue;
        } else if (strncmp(name, "xmlns:", 6) == 0) {
            /* Prefixed namespace: xmlns:prefix="uri" */
            const char* prefix = name + 6;
            struct taurus_namespace* ns = TAURUS_ALLOC(struct taurus_namespace);
            if (ns) {
                ns->prefix = taurus_strdup(prefix);
                ns->uri = taurus_strdup(value);
                ns->next = elem->namespaces;
                elem->namespaces = ns;
                elem->namespaces_count++;
            }
            TAURUS_FREE(name);
            TAURUS_FREE(value);
            continue;
        }

        /* Regular attribute */
        struct taurus_attribute* attr = TAURUS_ALLOC(struct taurus_attribute);
        if (!attr) {
            TAURUS_FREE(name);
            TAURUS_FREE(value);
            break;
        }

        attr->name = name;
        attr->value = value;
        attr->prefix = NULL;
        attr->namespace_uri = NULL;

        /* Add to element's attributes array */
        if (elem->attributes_count >= elem->attributes_capacity) {
            size_t new_cap = elem->attributes_capacity == 0 ? 4 : elem->attributes_capacity * 2;
            struct taurus_attribute** new_attrs = TAURUS_REALLOC_N(
                elem->attributes, struct taurus_attribute*, new_cap);
            if (!new_attrs) {
                TAURUS_FREE(attr->name);
                TAURUS_FREE(attr->value);
                TAURUS_FREE(attr);
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
static void resolve_element_namespace(struct taurus_element* elem) {
    if (!elem) return;

    /* Extract prefix from element name if present */
    const char* colon = strchr(elem->name, ':');
    char* prefix = NULL;

    if (colon) {
        size_t prefix_len = colon - elem->name;
        prefix = TAURUS_ALLOC_N(char, prefix_len + 1);
        if (prefix) {
            memcpy(prefix, elem->name, prefix_len);
            prefix[prefix_len] = '\0';
            elem->prefix = prefix;
        }
    }

    /* Search for matching namespace declaration */
    struct taurus_element* current = elem;
    while (current) {
        struct taurus_namespace* ns = current->namespaces;
        while (ns) {
            /* Check if this namespace matches our prefix */
            if (!prefix && !ns->prefix) {
                /* Default namespace match */
                elem->namespace_uri = taurus_strdup(ns->uri);
                return;
            }
            if (prefix && ns->prefix && strcmp(prefix, ns->prefix) == 0) {
                /* Prefixed namespace match */
                elem->namespace_uri = taurus_strdup(ns->uri);
                return;
            }
            ns = ns->next;
        }
        current = current->parent;
    }
}

/* Helper: Resolve namespaces recursively for element and all descendants */
static void resolve_namespaces_recursive(struct taurus_element* elem) {
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
    char* text = TAURUS_ALLOC_N(char, capacity);
    if (!text) return NULL;

    while (**p && **p != '<') {
        if (len + 1 >= capacity) {
            capacity *= 2;
            char* new_text = TAURUS_REALLOC_N(text, char, capacity);
            if (!new_text) {
                TAURUS_FREE(text);
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
        TAURUS_FREE(text);
        return NULL;
    }

    char* result = taurus_strdup(trimmed_start);
    TAURUS_FREE(text);
    return result;
}

/* Forward declaration */
static struct taurus_element* parse_element(const char** p);

/* Helper: Add child to element */
static void add_child(struct taurus_element* parent, struct taurus_element* child) {
    if (!parent || !child) return;

    if (parent->children_count >= parent->children_capacity) {
        size_t new_cap = parent->children_capacity == 0 ? 4 : parent->children_capacity * 2;
        struct taurus_element** new_children = TAURUS_REALLOC_N(
            parent->children, struct taurus_element*, new_cap);
        if (!new_children) return;
        parent->children = new_children;
        parent->children_capacity = new_cap;
    }

    parent->children[parent->children_count++] = child;
    child->parent = parent;
}

/* Parse element: <name>content</name> */
static struct taurus_element* parse_element(const char** p) {
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
    struct taurus_element* elem = TAURUS_ALLOC(struct taurus_element);
    if (!elem) {
        TAURUS_FREE(name);
        return NULL;
    }
    memset(elem, 0, sizeof(struct taurus_element));
    elem->node_type = TAURUS_NODE_ELEMENT;  /* Initialize node type */
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
                    TAURUS_FREE(close_name);
                }
                /* Skip whitespace manually */
                while (*pos && isspace((unsigned char)*pos)) pos++;
                if (*pos == '>') pos++;
                *p = pos;
                return elem;
            } else {
                /* Child element */
                struct taurus_element* child = parse_element(&pos);
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
struct taurus_document* parse_xml_simple(const char* xml, size_t len) {
    /* Validate input */
    if (!xml) {
        taurus_set_error(TAURUS_ERROR_NULL_INPUT, "NULL input provided");
        return NULL;
    }

    if (len == 0) {
        taurus_set_error(TAURUS_ERROR_EMPTY_INPUT, "Empty input provided");
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
    struct taurus_document* doc = TAURUS_ALLOC(struct taurus_document);
    if (!doc) {
        taurus_set_error(TAURUS_ERROR_OUT_OF_MEMORY, "Failed to allocate document");
        return NULL;
    }

    memset(doc, 0, sizeof(struct taurus_document));
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
        taurus_set_error_with_context(
            TAURUS_ERROR_INVALID_XML,
            "Expected root element",
            state.input,
            state.offset,
            state.line,
            state.column
        );
        TAURUS_FREE(doc);
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

        taurus_set_error_with_context(
            TAURUS_ERROR_PARSE_FAILED,
            "Failed to parse root element",
            state.input,
            failed_offset,
            failed_line,
            failed_col
        );
        TAURUS_FREE(doc);
        return NULL;
    }

    /* Resolve namespaces for entire tree after parsing complete
     * This ensures all elements have correct namespace_uri */
    resolve_namespaces_recursive(doc->root);

    return doc;
}

/* Free element recursively */
void free_element(struct taurus_element* elem) {
    if (!elem) return;

    /* Free children */
    for (size_t i = 0; i < elem->children_count; i++) {
        free_element(elem->children[i]);
    }

    /* Free attributes and their content */
    for (size_t i = 0; i < elem->attributes_count; i++) {
        if (elem->attributes[i]) {
            if (elem->attributes[i]->name) TAURUS_FREE(elem->attributes[i]->name);
            if (elem->attributes[i]->value) TAURUS_FREE(elem->attributes[i]->value);
            if (elem->attributes[i]->prefix) TAURUS_FREE(elem->attributes[i]->prefix);
            if (elem->attributes[i]->namespace_uri) TAURUS_FREE(elem->attributes[i]->namespace_uri);
            TAURUS_FREE(elem->attributes[i]);
        }
    }

    /* Free arrays */
    if (elem->children) TAURUS_FREE(elem->children);
    if (elem->attributes) TAURUS_FREE(elem->attributes);

    /* Free strings */
    if (elem->name) TAURUS_FREE(elem->name);
    if (elem->prefix) TAURUS_FREE(elem->prefix);
    if (elem->namespace_uri) TAURUS_FREE(elem->namespace_uri);
    if (elem->text_content) TAURUS_FREE(elem->text_content);

    /* Free element */
    TAURUS_FREE(elem);
}