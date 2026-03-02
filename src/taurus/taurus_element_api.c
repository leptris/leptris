/* taurus_element_api.c - Taurus element API
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C XML parser and XPath evaluator - Element API.
 */

#include "../include/taurus.h"
#include "taurus_internal.h"
#include "dom/element.h"
#include "dom/node.h"
#include "dom/text.h"
#include "dom/comment.h"
#include "dom/cdata.h"
#include "dom/pi.h"
#include "dom/compact_accessor.h"
#include "dom/compact_element.h"  /* V2: 16-byte structures */
#include "memory/zero_check_alloc.h"  /* For ZeroCheckAlloc */
#include "common/entities.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <strings.h>

/* ============================================================================
 * Element Getters - POINTER-ONLY
 * ============================================================================ */

/**
 * Get element name (POINTER-ONLY)
 * Returns a null-terminated string.
 */
TAURUS_API const char* taurus_element_name(TaurusElement elem) {
    if (!elem) return "";

    /* Use wrapper's name directly (always set for pointer-only) */
    if (elem->name) {
        return strdup(elem->name);
    }

    /* Convert name_view if name not yet cached */
    if (!taurus_sv_is_empty(&elem->name_view)) {
        elem->name = taurus_sv_to_cstr(&elem->name_view);
        return elem->name ? strdup(elem->name) : "";
    }

    return "";
}

/**
 * Get element text content (concatenated recursively) - POINTER-ONLY
 */
TAURUS_API const char* taurus_element_text(TaurusElement elem) {
    if (!elem) return "";

    /* Use the wrapper path which handles pointer-based children */
    if (elem->first_child) {
        char* text = taurus_element_get_text_content(elem);
        return text ? text : "";
    }

    /* No children - return empty string */
    char* empty = (char*)malloc(1);
    if (empty) empty[0] = '\0';
    return empty ? empty : "";
}

/**
 * Get child element text value (first text node only, not recursive) - POINTER-ONLY
 */
TAURUS_API const char* taurus_element_child_value(TaurusElement elem) {
    if (!elem) return NULL;

    /* Use wrapper's first child */
    if (elem->first_child) {
        TaurusNode* child = elem->first_child;
        while (child) {
            if (child->type == TAURUS_NODE_TYPE_TEXT) {
                TaurusTextNode* text = (TaurusTextNode*)child;
                if (text->content) {
                    return strdup(text->content);
                }
            }
            child = child->next_sibling;
        }
    }
    return NULL;
}

/* ============================================================================
 * Attribute Getters
 * ============================================================================ */

/**
 * Get attribute value by name (Public API) - COMPACT-ONLY
 *
 * CHECKS WRAPPER ATTRIBUTES FIRST: If the element has had attributes added
 * via taurus_element_set_attribute(), those are checked first. Only if no
 * wrapper attributes exist do we fall back to compact accessor.
 */
TAURUS_API const char* taurus_element_attribute(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;

    /* First check wrapper's attribute list (may have modifications) */
    if (elem->first_attribute) {
        struct taurus_attribute* attr = taurus_element_get_attribute_by_name(elem, name);
        if (attr) {
            /* Lazy convert value_view to C string if needed */
            if (!attr->value && !taurus_sv_is_empty(&attr->value_view)) {
                struct taurus_document* doc = elem->document;
                if (doc && doc->pool) {
                    if (attr->has_entities) {
                        attr->value = taurus_decode_entities_view(&attr->value_view, doc->pool);
                    }
                    if (!attr->value) {
                        attr->value = taurus_sv_to_cstr_pooled(&attr->value_view, doc->pool);
                    }
                } else {
                    attr->value = taurus_sv_to_cstr(&attr->value_view);
                }
            }
            return attr->value;
        }
    }

    /* COMPACT-ONLY: Fall back to compact element access for parsed attributes */
    struct taurus_document* doc = elem->document;
    if (doc) {
        struct compact_element_v2* compact = compact_from_element(elem, doc);
        if (compact) {
            return compact_element_get_attr_value(compact, doc, name);
        }
    }

    /* Fallback for wrapper elements without compact backing */
    return NULL;
}

/**
 * Get attribute value as integer (Public API)
 */
TAURUS_API int taurus_element_attribute_int(TaurusElement elem, const char* name, int default_value) {
    const char* value = taurus_element_attribute(elem, name);
    if (!value || !*value) return default_value;

    while (isspace((unsigned char)*value)) value++;

    char* endptr;
    long result = strtol(value, &endptr, 10);

    while (*endptr && isspace((unsigned char)*endptr)) endptr++;
    if (*endptr != '\0') return default_value;

    return (int)result;
}

/**
 * Get attribute value as unsigned integer (Public API)
 */
TAURUS_API unsigned int taurus_element_attribute_uint(TaurusElement elem, const char* name, unsigned int default_value) {
    const char* value = taurus_element_attribute(elem, name);
    if (!value || !*value) return default_value;

    while (isspace((unsigned char)*value)) value++;

    char* endptr;
    unsigned long result = strtoul(value, &endptr, 10);

    while (*endptr && isspace((unsigned char)*endptr)) endptr++;
    if (*endptr != '\0') return default_value;

    return (unsigned int)result;
}

/**
 * Get attribute value as double (Public API)
 */
TAURUS_API double taurus_element_attribute_double(TaurusElement elem, const char* name, double default_value) {
    const char* value = taurus_element_attribute(elem, name);
    if (!value || !*value) return default_value;

    while (isspace((unsigned char)*value)) value++;

    char* endptr;
    double result = strtod(value, &endptr);

    while (*endptr && isspace((unsigned char)*endptr)) endptr++;
    if (*endptr != '\0') return default_value;

    return result;
}

/**
 * Get attribute value as float (Public API)
 */
TAURUS_API float taurus_element_attribute_float(TaurusElement elem, const char* name, float default_value) {
    const char* value = taurus_element_attribute(elem, name);
    if (!value || !*value) return default_value;

    while (isspace((unsigned char)*value)) value++;

    char* endptr;
    float result = strtof(value, &endptr);

    while (*endptr && isspace((unsigned char)*endptr)) endptr++;
    if (*endptr != '\0') return default_value;

    return result;
}

/**
 * Get attribute value as boolean (Public API)
 */
TAURUS_API int taurus_element_attribute_bool(TaurusElement elem, const char* name, int default_value) {
    const char* value = taurus_element_attribute(elem, name);
    if (!value || !*value) return default_value;

    if (strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "yes") == 0 ||
        strcasecmp(value, "on") == 0 ||
        strcmp(value, "1") == 0) {
        return 1;
    }
    if (strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "no") == 0 ||
        strcasecmp(value, "off") == 0 ||
        strcmp(value, "0") == 0) {
        return 0;
    }

    return default_value;
}

/**
 * Get attribute value as string with default (Public API)
 */
TAURUS_API const char* taurus_element_attribute_string(TaurusElement elem, const char* name, const char* default_value) {
    const char* value = taurus_element_attribute(elem, name);
    return (value != NULL) ? value : default_value;
}

/* ============================================================================
 * Attribute Setter Functions (Type-safe wrappers)
 * ============================================================================ */

/**
 * Set attribute value as double (Public API)
 */
TAURUS_API int taurus_element_set_attribute_double(TaurusElement elem, const char* name, double value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.17g", value);
    return taurus_element_set_attribute(elem, name, buffer);
}

/**
 * Set attribute value as float (Public API)
 */
TAURUS_API int taurus_element_set_attribute_float(TaurusElement elem, const char* name, float value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%g", value);
    return taurus_element_set_attribute(elem, name, buffer);
}

/**
 * Set attribute value as boolean (Public API)
 */
TAURUS_API int taurus_element_set_attribute_bool(TaurusElement elem, const char* name, int value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    const char* bool_str = value ? "true" : "false";
    return taurus_element_set_attribute(elem, name, bool_str);
}

/**
 * Set attribute value as integer (Public API)
 */
TAURUS_API int taurus_element_set_attribute_int(TaurusElement elem, const char* name, int value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return taurus_element_set_attribute(elem, name, buffer);
}

/**
 * Set attribute value as unsigned integer (Public API)
 */
TAURUS_API int taurus_element_set_attribute_uint(TaurusElement elem, const char* name, unsigned int value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%u", value);
    return taurus_element_set_attribute(elem, name, buffer);
}

/* ============================================================================
 * Navigation Functions
 * ============================================================================ */

/**
 * Get child element by index (Public API) - COMPACT-ONLY
 */
TAURUS_API TaurusElement taurus_element_child(TaurusElement elem, size_t index) {
    if (!elem) return NULL;

    /* First check wrapper's children array for newly created children */
    if (index < 4 && elem->children[index]) {
        return (TaurusElement)elem->children[index];
    }

    /* CRITICAL FIX: If element has wrapper children (first_child set), walk that linked list
     * This handles the case where we've prepended/appended children to a parsed element */
    if (elem->first_child) {
        TaurusNode* child = (TaurusNode*)elem->first_child;
        size_t i = 0;
        while (child && i <= index) {
            if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
                if (i == index) {
                    return (TaurusElement)child;
                }
                i++;
            }
            child = taurus_node_get_next_sibling(child);
        }
        return NULL;  /* Index out of bounds */
    }

    /* POINTER-ONLY: Walk the linked list */
    TaurusNode* child = elem->first_child;
    size_t i = 0;
    while (child && i < index) {
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            i++;
        }
        if (i < index) {
            child = taurus_node_get_next_sibling(child);
        }
    }
    /* Return the element at index */
    while (child && child->type != TAURUS_NODE_TYPE_ELEMENT) {
        child = taurus_node_get_next_sibling(child);
    }
    return (TaurusElement)child;
}

/**
 * Get parent element (Public API)
 */
TAURUS_API TaurusElement taurus_element_parent(TaurusElement elem) {
    if (!elem) return NULL;

    /* In compact mode, parent is stored via compact offset */
    /* The wrapper element has parent pointer, so we can use legacy path */
    return taurus_element_get_parent(elem);
}

/**
 * Get root element of document (Public API)
 */
TAURUS_API TaurusElement taurus_element_root(TaurusElement elem) {
    if (!elem) return NULL;

    /* In compact mode, traverse up using parent pointers */
    TaurusElement current = elem;
    TaurusElement parent = taurus_element_get_parent(current);
    while (parent) {
        current = parent;
        parent = taurus_element_get_parent(current);
    }

    return current;
}

/**
 * Get first child element regardless of name (Public API) - COMPACT-ONLY
 */
TAURUS_API TaurusElement taurus_element_first_child_any(TaurusElement elem) {
    if (!elem) return NULL;
    return taurus_element_get_first_child(elem);
}

/**
 * Get last child element regardless of name (Public API) - COMPACT-ONLY
 */
TAURUS_API TaurusElement taurus_element_last_child_any(TaurusElement elem) {
    if (!elem) return NULL;
    return taurus_element_get_last_child(elem);
}

/**
 * Get next sibling element regardless of name (Public API) - COMPACT-ONLY
 */
TAURUS_API TaurusElement taurus_element_next_sibling_any(TaurusElement elem) {
    if (!elem) return NULL;
    return taurus_element_get_next_sibling(elem);
}

/**
 * Get previous sibling element regardless of name (Public API)
 */
TAURUS_API TaurusElement taurus_element_previous_sibling_any(TaurusElement elem) {
    if (!elem) return NULL;

    TaurusElement parent = taurus_element_get_parent(elem);
    if (!parent) return NULL;

    TaurusNode* child = (TaurusNode*)taurus_element_get_first_child(parent);
    TaurusElement prev = NULL;

    while (child && child != (TaurusNode*)elem) {
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            prev = (TaurusElement)child;
        }

        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            child = taurus_node_get_next_sibling(child);
        } else if (child->type == TAURUS_NODE_TYPE_TEXT) {
            child = (TaurusNode*)(((TaurusTextNode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
            child = (TaurusNode*)(((TaurusCDATANode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_COMMENT) {
            child = (TaurusNode*)(((TaurusCommentNode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_PI) {
            child = (TaurusNode*)(((TaurusPINode*)child)->next_sibling);
        } else {
            child = NULL;
        }
    }

    return prev;
}

/**
 * Get first child element with specific name (Public API)
 */
TAURUS_API TaurusElement taurus_element_first_child(TaurusElement elem, const char* name) {
    if (!elem) return NULL;

    TaurusElement child = taurus_element_get_first_child(elem);
    if (!name) return child;

    while (child) {
        const char* child_name = taurus_element_name(child);
        if (child_name && strcmp(child_name, name) == 0) {
            return child;
        }
        child = taurus_element_get_next_sibling(child);
    }

    return NULL;
}

/**
 * Get last child element with specific name (Public API)
 */
TAURUS_API TaurusElement taurus_element_last_child(TaurusElement elem, const char* name) {
    if (!elem) return NULL;

    TaurusElement last = taurus_element_get_last_child(elem);
    if (!name || !last) return last;

    TaurusElement found = NULL;
    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        const char* child_name = taurus_element_name(child);
        if (child_name && strcmp(child_name, name) == 0) {
            found = child;
        }
        child = taurus_element_get_next_sibling(child);
    }

    return found;
}

/**
 * Get next sibling element with specific name (Public API)
 */
TAURUS_API TaurusElement taurus_element_next_sibling(TaurusElement elem, const char* name) {
    if (!elem) return NULL;

    TaurusElement sibling = taurus_element_get_next_sibling(elem);
    if (!name) return sibling;

    while (sibling) {
        const char* sibling_name = taurus_element_name(sibling);
        if (sibling_name && strcmp(sibling_name, name) == 0) {
            return sibling;
        }
        sibling = taurus_element_get_next_sibling(sibling);
    }

    return NULL;
}

/**
 * Get previous sibling element with specific name (Public API)
 */
TAURUS_API TaurusElement taurus_element_previous_sibling(TaurusElement elem, const char* name) {
    if (!elem) return NULL;

    TaurusElement parent = taurus_element_get_parent(elem);
    if (!parent) return NULL;

    TaurusElement child = taurus_element_get_first_child(parent);
    TaurusElement prev = NULL;
    TaurusElement found = NULL;

    while (child && child != elem) {
        if (!name) {
            prev = child;
        } else {
            const char* child_name = taurus_element_name(child);
            if (child_name && strcmp(child_name, name) == 0) {
                found = child;
            }
        }
        child = taurus_element_get_next_sibling(child);
    }

    return name ? found : prev;
}

/* ============================================================================
 * Find Functions
 * ============================================================================ */

/**
 * Find child element by name (Public API)
 */
TAURUS_API TaurusElement taurus_element_find_child(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;

    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        const char* child_name = taurus_element_name(child);
        if (child_name && strcmp(child_name, name) == 0) {
            return child;
        }
        child = taurus_element_get_next_sibling(child);
    }

    return NULL;
}

/**
 * Find child element by name and attribute value (Public API)
 */
TAURUS_API TaurusElement taurus_element_find_child_by_attr(TaurusElement elem,
                                                           const char* child_name,
                                                           const char* attr_name,
                                                           const char* attr_value) {
    if (!elem || !attr_name || !attr_value) return NULL;

    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        int name_matches = (child_name == NULL);
        if (!name_matches) {
            const char* child_elem_name = taurus_element_name(child);
            name_matches = (child_elem_name && strcmp(child_elem_name, child_name) == 0);
        }

        if (name_matches) {
            const char* attr_val = taurus_element_attribute(child, attr_name);
            if (attr_val && strcmp(attr_val, attr_value) == 0) {
                return child;
            }
        }
        child = taurus_element_get_next_sibling(child);
    }

    return NULL;
}

/* ============================================================================
 * Text Content Conversion Functions
 * ============================================================================ */

/**
 * Get element text content as integer (Public API)
 */
TAURUS_API int taurus_element_text_int(TaurusElement elem, int default_value) {
    const char* text = taurus_element_text(elem);
    if (!text || !text[0]) return default_value;

    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
        text++;
    }

    int is_hex = 0;
    int is_negative = 0;

    if (*text == '-') {
        is_negative = 1;
        text++;
        if (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
            return default_value;
        }
    }

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        is_hex = 1;
        text += 2;
    }

    char* endptr;
    long value;
    if (is_hex) {
        value = strtol(text, &endptr, 16);
    } else {
        value = strtol(text, &endptr, 10);
    }

    if (endptr == text || *endptr != '\0') {
        return default_value;
    }

    return is_negative ? -(int)value : (int)value;
}

/**
 * Get element text content as unsigned integer (Public API)
 */
TAURUS_API unsigned int taurus_element_text_uint(TaurusElement elem, unsigned int default_value) {
    const char* text = taurus_element_text(elem);
    if (!text || !text[0]) return default_value;

    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
        text++;
    }

    int is_hex = 0;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        is_hex = 1;
        text += 2;
    }

    char* endptr;
    unsigned long value;
    if (is_hex) {
        value = strtoul(text, &endptr, 16);
    } else {
        value = strtoul(text, &endptr, 10);
    }

    if (endptr == text || *endptr != '\0') {
        return default_value;
    }

    return (unsigned int)value;
}

/**
 * Get element text content as double (Public API)
 */
TAURUS_API double taurus_element_text_double(TaurusElement elem, double default_value) {
    const char* text = taurus_element_text(elem);
    if (!text || !text[0]) return default_value;

    char* endptr;
    double value = strtod(text, &endptr);

    if (endptr == text || *endptr != '\0') {
        return default_value;
    }

    return value;
}

/**
 * Get element text content as float (Public API)
 */
TAURUS_API float taurus_element_text_float(TaurusElement elem, float default_value) {
    const char* text = taurus_element_text(elem);
    if (!text || !text[0]) return default_value;

    char* endptr;
    float value = strtof(text, &endptr);

    if (endptr == text || *endptr != '\0') {
        return default_value;
    }

    return value;
}

/**
 * Get element text content as boolean (Public API)
 */
TAURUS_API int taurus_element_text_bool(TaurusElement elem, int default_value) {
    const char* text = taurus_element_text(elem);
    if (!text || !text[0]) return 0;

    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
        text++;
    }

    size_t len = strlen(text);
    while (len > 0 && (text[len-1] == ' ' || text[len-1] == '\t' || text[len-1] == '\n' || text[len-1] == '\r')) {
        len--;
    }

    if ((len == 5 && strncasecmp(text, "false", 5) == 0) ||
        (len == 2 && strncasecmp(text, "no", 2) == 0) ||
        (len == 3 && strncasecmp(text, "off", 3) == 0) ||
        (len == 1 && text[0] == '0')) {
        return 0;
    }

    if ((len == 4 && strncasecmp(text, "true", 4) == 0) ||
        (len == 3 && strncasecmp(text, "yes", 3) == 0) ||
        (len == 2 && strncasecmp(text, "on", 2) == 0) ||
        (len == 1 && text[0] == '1')) {
        return 1;
    }

    return len > 0 ? 1 : 0;
}

/* ============================================================================
 * Namespace Operations
 * ============================================================================ */

/**
 * Get element's active namespace URI
 */
TAURUS_API TaurusNamespace taurus_element_namespace(TaurusElement elem) {
    if (!elem) return NULL;
    return taurus_element_get_namespace_uri(elem);
}

/**
 * Get namespace URI from namespace handle
 */
TAURUS_API const char* taurus_namespace_uri(TaurusNamespace ns) {
    return ns;
}

/**
 * Get namespace prefix from namespace handle
 */
TAURUS_API const char* taurus_namespace_prefix(TaurusNamespace ns) {
    (void)ns;
    return NULL;
}

/**
 * Resolve namespace prefix with inheritance
 */
TAURUS_API const char* taurus_element_namespace_for_prefix(TaurusElement elem, const char* prefix) {
    if (!elem) return NULL;

    const char* elem_prefix = taurus_element_get_prefix(elem);
    if (elem_prefix && prefix && strcmp(elem_prefix, prefix) == 0) {
        return taurus_element_get_namespace_uri(elem);
    }

    TaurusElement parent = taurus_element_get_parent(elem);
    if (parent) {
        return taurus_element_namespace_for_prefix(parent, prefix);
    }

    return NULL;
}
