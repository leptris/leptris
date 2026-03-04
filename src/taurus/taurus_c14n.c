/* taurus_c14n.c - Taurus Canonical XML (C14N) API
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C XML parser and XPath evaluator - C14N API.
 */

#include "../include/taurus.h"
#include "taurus_internal.h"
#include "dom/element.h"
#include "dom/node.h"
#include "dom/text.h"
#include "dom/comment.h"
#include "dom/cdata.h"
#include "dom/pi.h"
#include "common/entities.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Macro for appending strings to a dynamic buffer */
#define APPEND_STRING(str, len) do { \
    if (!buffer) goto cleanup; \
    while (*size + (len) + 1 > *capacity) { \
        size_t new_cap = *capacity * 2; \
        char* new_buf = (char*)realloc(*buffer, new_cap); \
        if (!new_buf) { \
            free(*buffer); \
            *buffer = NULL; \
            goto cleanup; \
        } \
        *buffer = new_buf; \
        *capacity = new_cap; \
    } \
    memcpy(*buffer + *size, str, len); \
    *size += len; \
    (*buffer)[*size] = '\0'; \
} while(0)

/* ============================================================================
 * C14N Helper Functions
 * ============================================================================ */

/**
 * Helper function to escape text for C14N output
 */
static char* c14n_escape_text(const char* text, int is_attribute_value) {
    if (!text) return NULL;

    size_t len = strlen(text);
    size_t escape_count = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '<') {
            escape_count += 4;
        } else if (text[i] == '&') {
            escape_count += 5;
        } else if (text[i] == '\r') {
            escape_count += 5;
        } else if (is_attribute_value && text[i] == '"') {
            escape_count += 6;
        }
    }

    if (escape_count == 0) {
        return taurus_strdup(text);
    }

    size_t new_len = len + escape_count;
    char* escaped = (char*)malloc(new_len + 1);
    if (!escaped) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '<') {
            memcpy(&escaped[j], "&lt;", 4);
            j += 4;
        } else if (text[i] == '&') {
            memcpy(&escaped[j], "&amp;", 5);
            j += 5;
        } else if (text[i] == '\r') {
            memcpy(&escaped[j], "&#xD;", 5);
            j += 5;
        } else if (is_attribute_value && text[i] == '"') {
            memcpy(&escaped[j], "&quot;", 6);
            j += 6;
        } else {
            escaped[j++] = text[i];
        }
    }
    escaped[j] = '\0';

    return escaped;
}

/**
 * Compare function for sorting attributes lexicographically (for qsort)
 */
static int compare_attributes(const void* a, const void* b) {
    const struct taurus_attribute* attr_a = *(const struct taurus_attribute**)a;
    const struct taurus_attribute* attr_b = *(const struct taurus_attribute**)b;

    const char* name_a = !taurus_sv_is_empty(&attr_a->name_view) ? attr_a->name_view.data : attr_a->name;
    const char* name_b = !taurus_sv_is_empty(&attr_b->name_view) ? attr_b->name_view.data : attr_b->name;
    size_t len_a = !taurus_sv_is_empty(&attr_a->name_view) ? attr_a->name_view.length : strlen(attr_a->name);
    size_t len_b = !taurus_sv_is_empty(&attr_b->name_view) ? attr_b->name_view.length : strlen(attr_b->name);

    size_t min_len = len_a < len_b ? len_a : len_b;
    int cmp = memcmp(name_a, name_b, min_len);
    if (cmp != 0) return cmp;
    if (len_a < len_b) return -1;
    if (len_a > len_b) return 1;
    return 0;
}

/**
 * Compare function for sorting namespace declarations lexicographically (for qsort)
 */
static int compare_namespaces(const void* a, const void* b) {
    const struct taurus_namespace* ns_a = *(const struct taurus_namespace**)a;
    const struct taurus_namespace* ns_b = *(const struct taurus_namespace**)b;

    const char* prefix_a = ns_a->prefix ? ns_a->prefix : "";
    const char* prefix_b = ns_b->prefix ? ns_b->prefix : "";

    int cmp = strcmp(prefix_a, prefix_b);
    if (cmp != 0) return cmp;

    const char* uri_a = ns_a->uri ? ns_a->uri : "";
    const char* uri_b = ns_b->uri ? ns_b->uri : "";
    return strcmp(uri_a, uri_b);
}

/**
 * Recursive helper to serialize element in C14N format
 */
static void c14n_serialize_element(TaurusElement elem, char** buffer, size_t* size, size_t* capacity) {
    if (!elem) return;

    const char* name = taurus_element_get_name(elem);
    uint8_t attr_count = taurus_element_attribute_count(elem);

    struct taurus_attribute** sorted_attrs = NULL;
    if (attr_count > 0) {
        sorted_attrs = (struct taurus_attribute**)malloc(attr_count * sizeof(struct taurus_attribute*));
        if (sorted_attrs) {
            for (uint8_t i = 0; i < attr_count; i++) {
                sorted_attrs[i] = taurus_element_get_attribute_by_index(elem, i);
            }
            qsort(sorted_attrs, attr_count, sizeof(struct taurus_attribute*), compare_attributes);
        }
    }

    int has_content = 0;
    int has_children = 0;

    TaurusNode* child = (TaurusNode*)elem->first_child;
    while (child) {
        has_children = 1;
        if (child->type == TAURUS_NODE_TYPE_TEXT) {
            const char* text = taurus_text_get_content((TaurusTextNode*)child);
            if (text && *text) {
                has_content = 1;
                break;
            }
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

    char temp[4096];
    int len;

    len = snprintf(temp, sizeof(temp), "<%s", name ? name : "element");
    APPEND_STRING(temp, len);

    /* POINTER-BASED: Namespaces are stored as xmlns:prefix attributes */
    struct ptr_attribute* attr = elem->first_attr;
    while (attr) {
        if (attr->name && (strcmp(attr->name, "xmlns") == 0 || strncmp(attr->name, "xmlns:", 6) == 0)) {
            /* This is a namespace declaration */
            if (strncmp(attr->name, "xmlns:", 6) == 0) {
                /* Prefixed namespace */
                len = snprintf(temp, sizeof(temp), " %s=\"%s\"", attr->name, attr->value ? attr->value : "");
            } else {
                /* Default namespace */
                len = snprintf(temp, sizeof(temp), " xmlns=\"%s\"", attr->value ? attr->value : "");
            }
            APPEND_STRING(temp, len);
        }
        attr = attr->next_attr;
    }

    if (sorted_attrs) {
        extern int taurus_get_strict_mode(void);
        for (size_t i = 0; i < attr_count; i++) {
            struct taurus_attribute* attr = sorted_attrs[i];
            const char* attr_name = !taurus_sv_is_empty(&attr->name_view) ? attr->name_view.data : attr->name;
            size_t attr_name_len = !taurus_sv_is_empty(&attr->name_view) ? attr->name_view.length : strlen(attr->name);

            const char* attr_value = attr->value;
            if (!attr_value && !taurus_sv_is_empty(&attr->value_view)) {
                int old_strict = taurus_get_strict_mode();
                taurus_set_strict_mode(0);
                attr_value = taurus_decode_entities_view(&attr->value_view, NULL);
                taurus_set_strict_mode(old_strict);
            }

            if (attr_name && attr_value) {
                char* escaped_value = c14n_escape_text(attr_value, 1);
                if (escaped_value) {
                    len = snprintf(temp, sizeof(temp), " %.*s=\"%s\"", (int)attr_name_len, attr_name, escaped_value);
                    APPEND_STRING(temp, len);
                    free(escaped_value);
                }

                if (attr_value != attr->value && attr_value) {
                    free((void*)attr_value);
                }
            }
        }
    }

    if (!has_children && !has_content) {
        len = snprintf(temp, sizeof(temp), "></%s>", name ? name : "element");
        APPEND_STRING(temp, len);
    } else {
        len = snprintf(temp, sizeof(temp), ">");
        APPEND_STRING(temp, len);

        TaurusNode* child = (TaurusNode*)elem->first_child;
        while (child) {
            if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
                c14n_serialize_element((TaurusElement)child, buffer, size, capacity);
                child = taurus_node_get_next_sibling(child);
            } else if (child->type == TAURUS_NODE_TYPE_TEXT) {
                const char* text = taurus_text_get_content((TaurusTextNode*)child);
                if (text) {
                    char* escaped_text = c14n_escape_text(text, 0);
                    if (escaped_text) {
                        APPEND_STRING(escaped_text, (int)strlen(escaped_text));
                        free(escaped_text);
                    }
                }
                child = (TaurusNode*)(((TaurusTextNode*)child)->next_sibling);
            } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
                const char* text = taurus_cdata_get_content((TaurusCDATANode*)child);
                if (text) {
                    char* escaped_text = c14n_escape_text(text, 0);
                    if (escaped_text) {
                        APPEND_STRING(escaped_text, (int)strlen(escaped_text));
                        free(escaped_text);
                    }
                }
                child = (TaurusNode*)(((TaurusCDATANode*)child)->next_sibling);
            } else if (child->type == TAURUS_NODE_TYPE_PI) {
                TaurusPINode* pi = (TaurusPINode*)child;
                const char* target = taurus_pi_get_target(pi);
                const char* data = taurus_pi_get_data(pi);
                if (target) {
                    len = snprintf(temp, sizeof(temp), "<?%s", target);
                    APPEND_STRING(temp, len);
                    if (data && *data) {
                        APPEND_STRING(" ", 1);
                        APPEND_STRING(data, (int)strlen(data));
                    }
                    APPEND_STRING("?>", 2);
                }
                child = (TaurusNode*)(((TaurusPINode*)child)->next_sibling);
            } else if (child->type == TAURUS_NODE_TYPE_COMMENT) {
                child = (TaurusNode*)(((TaurusCommentNode*)child)->next_sibling);
            } else {
                child = NULL;
            }
        }

        len = snprintf(temp, sizeof(temp), "</%s>", name ? name : "element");
        APPEND_STRING(temp, len);
    }

    if (sorted_attrs) free(sorted_attrs);

cleanup:
    return;
}

/* ============================================================================
 * Public C14N API
 * ============================================================================ */

/**
 * Get hash value of element (Public API)
 */
TAURUS_API size_t taurus_element_hash_value(TaurusElement elem) {
    if (!elem) return 0;
    return (size_t)elem;
}

/**
 * Canonicalize document to C14N format (Public API)
 */
TAURUS_API char* taurus_c14n_canonicalize(struct taurus_document* doc, int version, int flags) {
    (void)version;
    (void)flags;

    if (!doc) return NULL;

    TaurusElement root = (TaurusElement)doc->new_dom_root;
    if (!root) return NULL;

    size_t capacity = 4096;
    size_t size = 0;
    char* buf = (char*)malloc(capacity);
    if (!buf) return NULL;
    buf[0] = '\0';

    for (struct taurus_processing_instruction* pi = doc->pis; pi; pi = pi->next) {
        char temp[1024];
        int len;
        if (pi->target) {
            len = snprintf(temp, sizeof(temp), "<?%s", pi->target);
            while (size + len + 1 > capacity) {
                size_t new_cap = capacity * 2;
                char* new_buf = (char*)realloc(buf, new_cap);
                if (!new_buf) {
                    free(buf);
                    return NULL;
                }
                buf = new_buf;
                capacity = new_cap;
            }
            memcpy(buf + size, temp, len);
            size += len;
            buf[size] = '\0';

            if (pi->data && *pi->data) {
                len = 1;
                while (size + len + 1 > capacity) {
                    size_t new_cap = capacity * 2;
                    char* new_buf = (char*)realloc(buf, new_cap);
                    if (!new_buf) {
                        free(buf);
                        return NULL;
                    }
                    buf = new_buf;
                    capacity = new_cap;
                }
                memcpy(buf + size, " ", len);
                size += len;
                buf[size] = '\0';

                len = (int)strlen(pi->data);
                while (size + len + 1 > capacity) {
                    size_t new_cap = capacity * 2;
                    char* new_buf = (char*)realloc(buf, new_cap);
                    if (!new_buf) {
                        free(buf);
                        return NULL;
                    }
                    buf = new_buf;
                    capacity = new_cap;
                }
                memcpy(buf + size, pi->data, len);
                size += len;
                buf[size] = '\0';
            }

            len = 2;
            while (size + len + 1 > capacity) {
                size_t new_cap = capacity * 2;
                char* new_buf = (char*)realloc(buf, new_cap);
                if (!new_buf) {
                    free(buf);
                    return NULL;
                }
                buf = new_buf;
                capacity = new_cap;
            }
            memcpy(buf + size, "?>", len);
            size += len;
            buf[size] = '\0';
        }
    }

    c14n_serialize_element(root, &buf, &size, &capacity);

    return buf;
}
