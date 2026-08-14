/* dom/element_query.c — Public element/attribute/text/namespace query API.
 *
 * Extracted from taurus.c (TODO 42 phase 3). These are the read-only
 * inspection functions: taurus_element_*, taurus_element_attribute_*,
 * taurus_element_text_*, taurus_element_namespace_*.
 *
 * Mutation (set_attribute, add_namespace) lives here too — they're
 * typed wrappers around the underlying accessors.
 */

#include "../include/taurus.h"
#include "../taurus_internal.h"
#include "element.h"
#include "node.h"
#include "text.h"

/* Forward decls from taurus_memory.c — avoid pulling taurus_memory.h
 * directly because it conflicts with pi.h's taurus_pi_free. */
int taurus_element_add_namespace(struct taurus_element* elem,
                                  struct taurus_namespace* ns);
struct taurus_namespace* taurus_namespace_new_pooled(const char* prefix,
                                                      const char* uri,
                                                      TaurusMemoryPool* pool);
#include "cdata.h"
#include "comment.h"
#include "pi.h"
#include "../common/string_view.h"
#include "../common/entities.h"
#include "../common/port.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

/* ============================================================================
 * Element Functions
 * ============================================================================ */

/**
 * Get element name
 */
TAURUS_API const char* taurus_element_name(TaurusElement elem) {
    if (!elem) return "";

    /* Use compact accessor to get element name */
    const char* name = taurus_element_get_name(elem);
    return name ? name : "";
}

/* Locate the owning document by walking up the parent chain.
 * Only the parse-time root gets `document` set on every node, but detached
 * subtrees still reach an attached ancestor through `parent`. */
static struct taurus_document* element_owning_document(TaurusElement elem) {
    for (; elem; elem = taurus_elem_parent(elem)) {
        if (taurus_element_get_document(elem)) return taurus_element_get_document(elem);
    }
    return NULL;
}

/**
 * Get element text content (concatenated recursively)
 *
 * The public contract is a `const char*` owned by the element, so the result
 * must live as long as the document — it can never be a malloc'd buffer the
 * caller has no way to release.  Two paths satisfy that:
 *
 *   1. A lone text/CDATA child already holds the entire content, NUL-terminated,
 *      in document-owned storage: return it directly (zero allocation).
 *   2. Mixed content needs concatenation, so the joined string is interned in
 *      the document pool and released with taurus_document_free().
 */
TAURUS_API const char* taurus_element_text(TaurusElement elem) {
    if (!elem) return "";

    TaurusNode* child = taurus_node_first_child_internal((TaurusNode*)elem);
    if (!child) return "";

    /* Path 1: single character-data child — hand back its own storage. */
    if (!taurus_node_get_next_sibling(child)) {
        if (child->type == TAURUS_NODE_TYPE_TEXT) {
            const char* content = taurus_text_get_content((TaurusTextNode*)child);
            return content ? content : "";
        }
        if (child->type == TAURUS_NODE_TYPE_CDATA) {
            const char* content = ((TaurusCDATANode*)child)->content;
            return content ? content : "";
        }
    }

    /* Path 2: concatenate, then intern in the document pool. */
    struct taurus_document* doc = element_owning_document(elem);
    if (!doc || !doc->pool) return "";

    char* text = taurus_element_get_text_content(elem);
    if (!text) return "";
    const char* owned = taurus_pool_strdup(doc->pool, text);
    taurus_free(text);
    return owned ? owned : "";
}

/**
 * Get child element text value (first text node only, not recursive)
 * This is different from taurus_element_text which concatenates all text recursively
 *
 * Returns nullptr if no text/CDATA child exists, not empty string ""
 *
 * Behavior: Looks at the first child node. If it's text/CDATA, returns its content.
 * If it's an element, recursively gets the child value from that element (one level deep).
 */
TAURUS_API const char* taurus_element_child_value(TaurusElement elem) {
    if (!elem) return NULL;

    /* Get first child node - use node API to get ALL children (not just elements) */
    TaurusNode* child = taurus_node_first_child((TaurusNode*)elem);
    if (!child) return NULL;

    /* If first child is a text node, return its content */
    if (child->type == TAURUS_NODE_TYPE_TEXT) {
        TaurusTextNode* text = (TaurusTextNode*)child;
        return taurus_text_get_content(text);
    }

    /* If first child is a CDATA node, return its content */
    if (child->type == TAURUS_NODE_TYPE_CDATA) {
        TaurusCDATANode* cdata = (TaurusCDATANode*)child;
        return cdata->content;
    }

    /* If first child is an element, recursively get its child value (one level deep) */
    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        return taurus_element_child_value((TaurusElement)child);
    }

    return NULL;  /* Comment, PI, or other node types have no text value */
}

/**
 * Get attribute value by name (Public API)
 * PERFORMANCE: Uses linked list traversal (O(n) where n = attribute count)
 */
TAURUS_API const char* taurus_element_attribute(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;

    /* Validate node type - only elements have attributes */
    TaurusNode* node = (TaurusNode*)elem;
    if (node->type != TAURUS_NODE_TYPE_ELEMENT) {
        return NULL;  /* Not an element node */
    }

    /* Use accessor function to find attribute */
    struct taurus_attribute* attr = taurus_element_get_attribute_by_name(elem, name);
    if (!attr) return NULL;

    /* Lazy convert value to NULL-terminated and resolve entities */
    if (!attr->value) {
        /* Get pool from document for string conversion */
        TaurusMemoryPool* pool = taurus_element_get_pool(elem);

        /* PERFORMANCE: Use pre-computed has_entities flag */
        if (attr->has_entities) {
            attr->value = taurus_decode_entities_view(&attr->value_view, pool);
        }

        /* Fallback: convert without entity resolution */
        if (!attr->value) {
            attr->value = taurus_sv_to_cstr_pooled(&attr->value_view, pool);
        }
    }
    return attr->value;
}

TAURUS_API int taurus_element_has_attribute(TaurusElement elem, const char* name) {
    if (!elem || !name) return 0;
    return taurus_element_get_attribute_by_name(elem, name) != NULL;
}

/**
 * Get attribute value as integer (Public API)
 */
TAURUS_API int taurus_element_attribute_int(TaurusElement elem, const char* name, int default_value) {
    const char* value = taurus_element_attribute(elem, name);
    if (!value || !*value) return default_value;

    /* Trim leading whitespace */
    while (isspace((unsigned char)*value)) value++;

    /* Check for valid integer (no partial parsing like "42abc") */
    char* endptr;
    long result = strtol(value, &endptr, 10);

    /* If there's non-whitespace after the number, it's invalid */
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

    /* Trim leading whitespace */
    while (isspace((unsigned char)*value)) value++;

    /* Check for valid unsigned integer (no partial parsing) */
    char* endptr;
    unsigned long result = strtoul(value, &endptr, 10);

    /* If there's non-whitespace after the number, it's invalid */
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

    /* Trim leading whitespace */
    while (isspace((unsigned char)*value)) value++;

    /* Check for valid double (no partial parsing) */
    char* endptr;
    double result = strtod(value, &endptr);

    /* If there's non-whitespace after the number, it's invalid */
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

    /* Trim leading whitespace */
    while (isspace((unsigned char)*value)) value++;

    /* Check for valid float (no partial parsing) */
    char* endptr;
    float result = strtof(value, &endptr);

    /* If there's non-whitespace after the number, it's invalid */
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

    /* Case-insensitive comparison for true/false/yes/no/on/off/1/0 */
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

    /* Convert double to string with sufficient precision (17 significant digits for IEEE 754) */
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.17g", value);
    return taurus_element_set_attribute(elem, name, buffer);
}

/**
 * Set attribute value as float (Public API)
 */
TAURUS_API int taurus_element_set_attribute_float(TaurusElement elem, const char* name, float value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    /* Convert float to string */
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%g", value);
    return taurus_element_set_attribute(elem, name, buffer);
}

/**
 * Set attribute value as boolean (Public API)
 */
TAURUS_API int taurus_element_set_attribute_bool(TaurusElement elem, const char* name, int value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    /* Convert boolean to string */
    const char* bool_str = value ? "true" : "false";
    return taurus_element_set_attribute(elem, name, bool_str);
}

/**
 * Set attribute value as integer (Public API)
 */
TAURUS_API int taurus_element_set_attribute_int(TaurusElement elem, const char* name, int value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    /* Convert int to string */
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return taurus_element_set_attribute(elem, name, buffer);
}

/**
 * Set attribute value as unsigned integer (Public API)
 */
TAURUS_API int taurus_element_set_attribute_uint(TaurusElement elem, const char* name, unsigned int value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    /* Convert unsigned int to string */
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%u", value);
    return taurus_element_set_attribute(elem, name, buffer);
}

/**
 * Get child element by index (Public API)
 * Returns the index-th ELEMENT child (skips text, comments, etc.)
 *
 * Performance: O(index) — walks the linked list from first_child.
 * For sequential iteration prefer taurus_element_first_child_any +
 * taurus_element_next_sibling_any (O(1) per step).
 */
TAURUS_API TaurusElement taurus_element_child(TaurusElement elem, size_t index) {
    if (!elem) return NULL;
    if (index >= elem->child_count) return NULL;

    TaurusElement child = taurus_element_get_first_child(elem);
    for (size_t i = 0; i < index && child != NULL; i++) {
        child = taurus_element_get_next_sibling(child);
    }
    return child;
}

/**
 * Get parent element (Public API)
 */
TAURUS_API TaurusElement taurus_element_parent(TaurusElement elem) {
    if (!elem) return NULL;

    /* Use compact accessor to get parent */
    return taurus_element_get_parent(elem);
}

/**
 * Get root element of document (Public API)
 * Walks up the parent chain to find the element with no parent
 */
TAURUS_API TaurusElement taurus_element_root(TaurusElement elem) {
    if (!elem) return NULL;

    /* Walk up the parent chain until we find an element with no parent */
    TaurusElement current = elem;
    TaurusElement parent = taurus_element_get_parent(current);
    while (parent) {
        current = parent;
        parent = taurus_element_get_parent(current);
    }

    return current;
}

/**
 * Get first child element regardless of name (Public API)
 */
TAURUS_API TaurusElement taurus_element_first_child_any(TaurusElement elem) {
    if (!elem) return NULL;

    /* Iterate through children until we find an element */
    TaurusNode* child = (TaurusNode*)taurus_element_get_first_child(elem);
    while (child) {
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            return (TaurusElement)child;
        }
        child = taurus_node_get_next_sibling(child);
    }

    return NULL;
}

/**
 * Get last child element regardless of name (Public API)
 */
TAURUS_API TaurusElement taurus_element_last_child_any(TaurusElement elem) {
    if (!elem) return NULL;

    /* Iterate through children to find the last element */
    TaurusElement last_elem = NULL;

    TaurusNode* child = (TaurusNode*)taurus_element_get_first_child(elem);
    while (child) {
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            last_elem = (TaurusElement)child;
        }
        child = taurus_node_get_next_sibling(child);
    }

    return last_elem;
}

/**
 * Get next sibling element regardless of name (Public API)
 */
TAURUS_API TaurusElement taurus_element_next_sibling_any(TaurusElement elem) {
    if (!elem) return NULL;

    /* Use compact accessor to get next sibling */
    TaurusNode* sibling = (TaurusNode*)taurus_element_get_next_sibling(elem);

    /* Keep traversing until we find an element (skip text/comment nodes) */
    while (sibling) {
        if (sibling->type == TAURUS_NODE_TYPE_ELEMENT) {
            return (TaurusElement)sibling;
        }
        sibling = taurus_node_get_next_sibling(sibling);
    }

    return NULL;
}

/**
 * Get previous sibling element regardless of name (Public API)
 * NOTE: Compact mode doesn't have prev_sibling pointer, so we search from parent
 */
TAURUS_API TaurusElement taurus_element_previous_sibling_any(TaurusElement elem) {
    if (!elem) return NULL;

    /* Get parent */
    TaurusElement parent = taurus_element_get_parent(elem);
    if (!parent) return NULL;

    /* Find previous sibling by walking from parent's first child */
    TaurusNode* child = (TaurusNode*)taurus_element_get_first_child(parent);
    TaurusElement prev = NULL;

    while (child && child != (TaurusNode*)elem) {
        /* Only consider element nodes as previous siblings */
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            prev = (TaurusElement)child;
        }
        child = taurus_node_get_next_sibling(child);
    }

    return prev;
}

/**
 * Get first child element with specific name (Public API)
 * If name is NULL, returns first child regardless of name
 */
TAURUS_API TaurusElement taurus_element_first_child(TaurusElement elem, const char* name) {
    if (!elem) return NULL;

    /* Get first child and check name if specified */
    TaurusElement child = taurus_element_get_first_child(elem);
    if (!name) return child;

    /* Find first child with matching name via hash pre-filter (TODO 159) */
    uint16_t target_hash = taurus_name_hash_compute(name);
    while (child) {
        if (taurus_elem_name_is(child, name, target_hash)) {
            return child;
        }
        child = taurus_element_get_next_sibling(child);
    }

    return NULL;
}

/**
 * Get last child element with specific name (Public API)
 * If name is NULL, returns last child regardless of name
 */
TAURUS_API TaurusElement taurus_element_last_child(TaurusElement elem, const char* name) {
    if (!elem) return NULL;

    /* Get last child */
    TaurusElement last = taurus_element_get_last_child(elem);
    if (!name || !last) return last;

    /* Walk backwards from last child to find one with matching name */
    /* Since we don't have prev_sibling, we need to walk from first child */
    TaurusElement found = NULL;
    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        const char* child_name = taurus_element_name(child);
        if (child_name && strcmp(child_name, name) == 0) {
            found = child;  /* Keep updating to get the last one */
        }
        child = taurus_element_get_next_sibling(child);
    }

    return found;
}

/**
 * Get next sibling element with specific name (Public API)
 * If name is NULL, returns next sibling regardless of name
 */
TAURUS_API TaurusElement taurus_element_next_sibling(TaurusElement elem, const char* name) {
    if (!elem) return NULL;

    /* Get next sibling */
    TaurusElement sibling = taurus_element_get_next_sibling(elem);
    if (!name) return sibling;

    /* Find next sibling with matching name */
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
 * If name is NULL, returns previous sibling regardless of name
 */
TAURUS_API TaurusElement taurus_element_previous_sibling(TaurusElement elem, const char* name) {
    if (!elem) return NULL;

    /* Get parent */
    TaurusElement parent = taurus_element_get_parent(elem);
    if (!parent) return NULL;

    /* Find previous sibling by walking from parent's first child */
    TaurusElement child = taurus_element_get_first_child(parent);
    TaurusElement prev = NULL;
    TaurusElement found = NULL;

    while (child && child != elem) {
        if (!name) {
            prev = child;
        } else {
            const char* child_name = taurus_element_name(child);
            if (child_name && strcmp(child_name, name) == 0) {
                found = child;  /* Keep updating to get the most recent matching one */
            }
        }
        child = taurus_element_get_next_sibling(child);
    }

    return name ? found : prev;
}

/**
 * Find child element by name (Public API)
 * Searches all children for one with matching name
 */
TAURUS_API TaurusElement taurus_element_find_child(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;

    /* Walk all children to find one with matching name */
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
 * Searches all children for one with matching name and attribute value
 */
TAURUS_API TaurusElement taurus_element_find_child_by_attr(TaurusElement elem,
                                                           const char* child_name,
                                                           const char* attr_name,
                                                           const char* attr_value) {
    if (!elem || !attr_name || !attr_value) return NULL;

    /* Walk all children to find one with matching name and attribute */
    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        /* If child_name is NULL, match any child. Otherwise check name matches. */
        int name_matches = (child_name == NULL);
        if (!name_matches) {
            const char* child_elem_name = taurus_element_name(child);
            name_matches = (child_elem_name && strcmp(child_elem_name, child_name) == 0);
        }

        if (name_matches) {
            /* Child has matching name (or any name if child_name is NULL), check attribute */
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
 * Text Content Conversion Functions (Public API)
 * ============================================================================ */

/**
 * Get element text content as integer (Public API)
 */
TAURUS_API int taurus_element_text_int(TaurusElement elem, int default_value) {
    const char* text = taurus_element_text(elem);
    if (!text || !text[0]) return default_value;

    /* Skip leading whitespace */
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
        text++;
    }

    /* Check for hex prefix (0x or 0X) */
    int is_hex = 0;
    int is_negative = 0;

    if (*text == '-') {
        is_negative = 1;
        text++;
        /* Check for whitespace immediately after minus sign (invalid) */
        if (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
            return default_value;
        }
    }

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        is_hex = 1;
        text += 2;
    }

    /* Parse the number */
    char* endptr;
    long value;
    if (is_hex) {
        value = strtol(text, &endptr, 16);
    } else {
        /* Use base 10 for decimal (no octal support) */
        value = strtol(text, &endptr, 10);
    }

    /* Check if entire string was consumed */
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

    /* Skip leading whitespace */
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
        text++;
    }

    /* Check for hex prefix (0x or 0X) */
    int is_hex = 0;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        is_hex = 1;
        text += 2;
    }

    /* Parse the number */
    char* endptr;
    unsigned long value;
    if (is_hex) {
        value = strtoul(text, &endptr, 16);
    } else {
        /* Use base 10 for decimal (no octal support) */
        value = strtoul(text, &endptr, 10);
    }

    /* Check if entire string was consumed */
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

    /* Try to parse as double */
    char* endptr;
    double value = strtod(text, &endptr);

    /* Check if entire string was consumed */
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

    /* Try to parse as float */
    char* endptr;
    float value = strtof(text, &endptr);

    /* Check if entire string was consumed */
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
    if (!text || !text[0]) return 0;  /* Empty string is false */

    /* Skip leading whitespace */
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
        text++;
    }

    /* Skip trailing whitespace */
    size_t len = strlen(text);
    while (len > 0 && (text[len-1] == ' ' || text[len-1] == '\t' || text[len-1] == '\n' || text[len-1] == '\r')) {
        len--;
    }

    /* Case-insensitive comparison for various false values */
    if ((len == 5 && strncasecmp(text, "false", 5) == 0) ||
        (len == 2 && strncasecmp(text, "no", 2) == 0) ||
        (len == 3 && strncasecmp(text, "off", 3) == 0) ||
        (len == 1 && text[0] == '0')) {
        return 0;
    }

    /* Case-insensitive comparison for various true values */
    if ((len == 4 && strncasecmp(text, "true", 4) == 0) ||
        (len == 3 && strncasecmp(text, "yes", 3) == 0) ||
        (len == 2 && strncasecmp(text, "on", 2) == 0) ||
        (len == 1 && text[0] == '1')) {
        return 1;
    }

    /* Any non-empty text that's not explicitly false is true */
    return len > 0 ? 1 : 0;
}

/* ============================================================================
 * Namespace Operations (Public API)
 * ============================================================================ */

/**
 * Get element's active namespace URI
 * In compact mode, namespace is stored inline in the element.
 */
TAURUS_API TaurusNamespace taurus_element_namespace(TaurusElement elem) {
    if (!elem) return NULL;

    /* In compact mode, namespace_uri is cached from namespace_uri_view */
    /* Trigger lazy conversion if needed */
    return taurus_element_get_namespace_uri(elem);
}

/**
 * Get namespace URI from namespace handle
 * In compact mode, TaurusNamespace is the URI string itself.
 */
TAURUS_API const char* taurus_namespace_uri(TaurusNamespace ns) {
    /* In compact mode, TaurusNamespace IS the URI string */
    return ns;
}

/**
 * Get namespace prefix from namespace handle
 * In compact mode, namespaces are inline, so this returns NULL.
 * Use taurus_element_get_prefix() to get an element's prefix.
 */
TAURUS_API const char* taurus_namespace_prefix(TaurusNamespace ns) {
    /* In compact mode, prefix information is stored per-element */
    /* Use taurus_element_get_prefix() instead */
    (void)ns; /* Unused */
    return NULL;
}

/**
 * Resolve namespace prefix with inheritance.
 * Issue #222: previously this only checked the element's OWN prefix
 * field (e.g. "foo" in <foo:child>) and returned the element's
 * namespace_uri — which was rarely what callers wanted and ignored
 * the xmlns:prefix declarations entirely. Now delegates to
 * taurus_element_lookup_namespace which walks the element's
 * namespace-declaration list and recurses up the tree. */
TAURUS_API const char* taurus_element_namespace_for_prefix(TaurusElement elem, const char* prefix) {
    if (!elem) return NULL;
    return taurus_element_lookup_namespace(elem, prefix);
}



/**
 * Get hash value of element (Public API)
 * Returns a hash value based on the element's memory address
 */
TAURUS_API size_t taurus_element_hash_value(TaurusElement elem) {
    if (!elem) return 0;

    /* Simple hash based on pointer address */
    /* This works because elements are pool-allocated and stable */
    return (size_t)elem;
}

/* ============================================================================
 * FFI Helpers: indexed attribute and namespace access (TODO 138)
 *
 * O(n) walk over the attribute linked list. Nokogiri and libxml2 do
 * the same. The Ruby binding caches results.
 * ============================================================================ */

TAURUS_API const char* taurus_element_attribute_name_at(TaurusElement elem, size_t index) {
    struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
    size_t i = 0;
    while (attr) {
        if (i == index) {
            if (attr->name) return attr->name;
            TaurusStringView nv = attr->name_view;
            if (nv.length > 0 && nv.data) {
                attr->name = taurus_sv_to_cstr(&nv);
                return attr->name;
            }
            return NULL;
        }
        i++;
        attr = taurus_attr_next(attr);
    }
    return NULL;
}

TAURUS_API const char* taurus_element_attribute_value_at(TaurusElement elem, size_t index) {
    if (!elem) return NULL;
    struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
    size_t i = 0;
    while (attr) {
        if (i == index) {
            if (attr->value) return attr->value;
            TaurusStringView vv = attr->value_view;
            if (vv.length > 0 && vv.data) {
                attr->value = taurus_sv_to_cstr(&vv);
                return attr->value;
            }
            return NULL;
        }
        i++;
        attr = taurus_attr_next(attr);
    }
    return NULL;
}

TAURUS_API size_t taurus_element_namespace_count(TaurusElement elem) {
    if (!elem) return 0;
    size_t count = 0;
    /* The parser strips xmlns declarations from the regular attribute
     * list and moves them to elem->namespaces. Walk THAT list. */
    for (struct taurus_namespace* ns = taurus_elem_namespaces(elem); ns; ns = ns->next) {
        count++;
    }
    /* Backward compat: also count xmlns attributes in case any caller
     * added them via taurus_element_add_attribute instead of the
     * namespace API. This is the original implementation. */
    struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
    while (attr) {
        TaurusStringView nv = attr->name_view;
        if (nv.length >= 5 && nv.data &&
            nv.data[0] == 'x' && nv.data[1] == 'm' &&
            nv.data[2] == 'l' && nv.data[3] == 'n' &&
            nv.data[4] == 's') {
            count++;
        }
        attr = taurus_attr_next(attr);
    }
    return count;
}

/* Walk the union of elem->namespaces and any xmlns:* attributes to
 * the N-th declaration. The parser uses elem->namespaces; legacy
 * callers may have used the attribute list. Both are walked in order. */
static int element_namespace_decl_at(TaurusElement elem, size_t index,
                                      const char** out_prefix,
                                      const char** out_uri) {
    if (!elem) return 0;
    size_t i = 0;
    for (struct taurus_namespace* ns = taurus_elem_namespaces(elem); ns; ns = ns->next) {
        if (i == index) {
            *out_prefix = ns->prefix;
            *out_uri = ns->uri;
            return 1;
        }
        i++;
    }
    struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
    while (attr) {
        TaurusStringView nv = attr->name_view;
        if (nv.length >= 5 && nv.data &&
            nv.data[0] == 'x' && nv.data[1] == 'm' &&
            nv.data[2] == 'l' && nv.data[3] == 'n' &&
            nv.data[4] == 's') {
            if (i == index) {
                /* Materialize full name + value. */
                const char* full = attr->name;
                if (!full) {
                    attr->name = taurus_sv_to_cstr(&nv);
                    full = attr->name;
                }
                if (full && strlen(full) > 5) {
                    *out_prefix = full + 6;
                } else {
                    *out_prefix = NULL;
                }
                if (attr->value) {
                    *out_uri = attr->value;
                } else {
                    TaurusStringView vv = attr->value_view;
                    if (vv.length > 0 && vv.data) {
                        attr->value = taurus_sv_to_cstr(&vv);
                    }
                    *out_uri = attr->value;
                }
                return 1;
            }
            i++;
        }
        attr = taurus_attr_next(attr);
    }
    return 0;
}

TAURUS_API const char* taurus_element_namespace_decl_prefix(
    TaurusElement elem, size_t index) {
    const char* prefix = NULL;
    const char* uri = NULL;
    if (!element_namespace_decl_at(elem, index, &prefix, &uri)) return NULL;
    return prefix;
}

TAURUS_API const char* taurus_element_namespace_decl_uri(
    TaurusElement elem, size_t index) {
    const char* prefix = NULL;
    const char* uri = NULL;
    if (!element_namespace_decl_at(elem, index, &prefix, &uri)) return NULL;
    return uri;
}

/* ============================================================================
 * Namespace mutation (issue #186).
 *
 * Three public entry points wrap the existing internal namespace
 * list. The internal list lives on elem->namespaces; the parser
 * populates it during parse. These mutators let callers add/remove
 * declarations programmatically.
 * ============================================================================ */

TAURUS_API TaurusStatus taurus_element_add_namespace_definition(
    TaurusElement elem, const char* prefix, const char* href) {
    if (!elem || !href) return TAURUS_ERROR_NULL_ARG;
    taurus_document_ensure_promoted(taurus_element_get_document(elem));

    /* Normalize empty-string prefix to NULL (default ns). */
    if (prefix && !*prefix) prefix = NULL;

    TaurusMemoryPool* pool = taurus_element_get_pool(elem);
    struct taurus_namespace* ns;
    if (pool) {
        ns = taurus_namespace_new_pooled(prefix, href, pool);
    } else {
        /* No pool — fall back to heap-allocating the ns struct + copies. */
        ns = (struct taurus_namespace*)malloc(sizeof(*ns));
        if (!ns) return TAURUS_ERROR_MEMORY;
        ns->prefix = prefix ? strdup(prefix) : NULL;
        ns->uri = strdup(href);
        ns->next = NULL;
        if (!ns->uri || (prefix && !ns->prefix)) {
            free(ns->prefix);
            free(ns->uri);
            free(ns);
            return TAURUS_ERROR_MEMORY;
        }
    }
    if (!ns) return TAURUS_ERROR_MEMORY;

    taurus_element_add_namespace(elem, ns);
    return TAURUS_OK;
}

TAURUS_API TaurusStatus taurus_element_set_default_namespace(
    TaurusElement elem, const char* href) {
    return taurus_element_add_namespace_definition(elem, NULL, href);
}

TAURUS_API TaurusStatus taurus_element_remove_namespace_definition(
    TaurusElement elem, const char* prefix) {
    if (!elem) return TAURUS_ERROR_NULL_ARG;
    /* Normalize empty prefix to NULL. */
    if (prefix && !*prefix) prefix = NULL;

    /* No ns_cache = no namespace declarations on this element. */
    if (!elem->ns_cache) return TAURUS_ERROR_NOT_FOUND;

    struct taurus_namespace** link = &elem->ns_cache->declarations;
    while (*link) {
        struct taurus_namespace* cur = *link;
        int match = (prefix == NULL) ? (cur->prefix == NULL)
            : (cur->prefix && strcmp(cur->prefix, prefix) == 0);
        if (match) {
            *link = cur->next;
            /* Don't free cur->prefix / cur->uri — they're pool-owned.
             * The struct itself may be pool-allocated too; skip free. */
            return TAURUS_OK;
        }
        link = &cur->next;
    }
    return TAURUS_ERROR_NOT_FOUND;
}
