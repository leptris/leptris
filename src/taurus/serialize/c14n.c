/* serialize/c14n.c — Canonical XML (C14N 1.0) implementation.
 *
 * Extracted from taurus.c (TODO 42 phase 4). Implements
 * taurus_c14n_canonicalize and its supporting helpers.
 */

#include "../include/taurus.h"
#include "../taurus_internal.h"
#include "../dom/element.h"
#include "../dom/node.h"
#include "../dom/text.h"
#include "../dom/comment.h"
#include "../dom/cdata.h"
#include "../dom/pi.h"
#include "../common/string_view.h"
#include "../common/entities.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Macro for appending strings to a dynamic buffer.
 * Mirrors the definition in taurus.c — these helpers are local to
 * the document-lifecycle / C14N translation units. */
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
 * Canonical XML (C14N) Operations
 * ============================================================================ */

/**
 * Helper function to escape text for C14N output
 *
 * C14N 1.0 escaping rules:
 * - In text content: < and & must be escaped
 * - In attribute values: <, &, and " must be escaped
 *
 * Returns a newly allocated string that must be freed
 */
static char* c14n_escape_text(const char* text, int is_attribute_value) {
    if (!text) return NULL;

    /* Count how much space we need
     * C14N spec requires: <, &, \r always escaped; " escaped in attributes */
    size_t len = strlen(text);
    size_t escape_count = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '<') {
            escape_count += 4;  /* &lt; */
        } else if (text[i] == '&') {
            escape_count += 5;  /* &amp; */
        } else if (text[i] == '\r') {
            escape_count += 5;  /* &#xD; (C14N requires \r to be escaped) */
        } else if (is_attribute_value && text[i] == '"') {
            escape_count += 6;  /* &quot; */
        }
    }

    /* If no escaping needed, return a copy */
    if (escape_count == 0) {
        return taurus_strdup(text);
    }

    /* Allocate buffer */
    size_t new_len = len + escape_count;
    char* escaped = (char*)malloc(new_len + 1);
    if (!escaped) return NULL;

    /* Escape characters (C14N spec: <, &, \r always escaped; " escaped in attributes) */
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

    /* Get attribute names */
    const char* name_a = !taurus_sv_is_empty(&attr_a->name_view) ? attr_a->name_view.data : attr_a->name;
    const char* name_b = !taurus_sv_is_empty(&attr_b->name_view) ? attr_b->name_view.data : attr_b->name;
    size_t len_a = !taurus_sv_is_empty(&attr_a->name_view) ? attr_a->name_view.length : strlen(attr_a->name);
    size_t len_b = !taurus_sv_is_empty(&attr_b->name_view) ? attr_b->name_view.length : strlen(attr_b->name);

    /* Lexicographic comparison */
    size_t min_len = len_a < len_b ? len_a : len_b;
    int cmp = memcmp(name_a, name_b, min_len);
    if (cmp != 0) return cmp;
    if (len_a < len_b) return -1;
    if (len_a > len_b) return 1;
    return 0;
}

/**
 * Recursive helper to serialize element in C14N format
 */
static void c14n_serialize_element(TaurusElement elem, char** buffer, size_t* size, size_t* capacity) {
    if (!elem) return;

    /* Get element name */
    const char* name = taurus_element_get_name(elem);

    /* Get attribute count using accessor */
    uint8_t attr_count = taurus_element_attribute_count(elem);

    /* Allocate temporary array for sorting attributes */
    struct taurus_attribute** sorted_attrs = NULL;
    if (attr_count > 0) {
        sorted_attrs = (struct taurus_attribute**)malloc(attr_count * sizeof(struct taurus_attribute*));
        if (sorted_attrs) {
            /* Copy attributes from linked list to temporary array */
            for (uint8_t i = 0; i < attr_count; i++) {
                sorted_attrs[i] = taurus_element_get_attribute_by_index(elem, i);
            }
            /* Sort attributes lexicographically */
            qsort(sorted_attrs, attr_count, sizeof(struct taurus_attribute*), compare_attributes);
        }
    }

    /* For namespace declarations - TODO: Implement namespace tracking in compact mode */
    /* Currently namespaces are stored in prefix/namespace_uri fields, not as a list */

    /* Append opening tag: <name */
    char temp[4096];
    int len;

    /* Check if element has content (including text nodes) */
    int has_content = 0;
    int has_children = 0;

    TaurusNode* child = (TaurusNode*)taurus_node_first_child_internal((TaurusNode*)elem);
    while (child) {
        has_children = 1;
        /* Check if child is text or CDATA by checking node type */
        if (child->type == TAURUS_NODE_TYPE_TEXT) {
            const char* text = taurus_text_get_content((TaurusTextNode*)child);
            if (text && *text) {
                has_content = 1;
                break;
            }
        }
        /* Get next sibling - the generic dispatch handles every node type */
        child = taurus_node_get_next_sibling(child);
    }

    /* Start opening tag */
    len = snprintf(temp, sizeof(temp), "<%s", name ? name : "element");
    APPEND_STRING(temp, len);

    /* Add namespace declarations */
    struct taurus_namespace* ns = elem->namespaces;
    while (ns) {
        /* Serialize namespace as xmlns:prefix="uri" or xmlns="uri" for default */
        if (ns->prefix) {
            len = snprintf(temp, sizeof(temp), " xmlns:%s=\"%s\"", ns->prefix, ns->uri);
        } else {
            len = snprintf(temp, sizeof(temp), " xmlns=\"%s\"", ns->uri);
        }
        APPEND_STRING(temp, len);
        ns = ns->next;
    }

    /* Add sorted attributes */
    if (sorted_attrs) {
        for (size_t i = 0; i < attr_count; i++) {
            struct taurus_attribute* attr = sorted_attrs[i];
            const char* attr_name = !taurus_sv_is_empty(&attr->name_view) ? attr->name_view.data : attr->name;
            size_t attr_name_len = !taurus_sv_is_empty(&attr->name_view) ? attr->name_view.length : strlen(attr->name);

            /* Get attribute value (convert if needed) */
            const char* attr_value = attr->value;
            if (!attr_value && !taurus_sv_is_empty(&attr->value_view)) {
                /* Need to convert StringView to string with entity expansion
                 * Use lenient mode for C14N to handle edge cases like "&" in attributes */
                int old_strict = taurus_get_strict_mode();
                taurus_set_strict_mode(0);  /* Enable lenient mode */
                attr_value = taurus_decode_entities_view(&attr->value_view, NULL);
                taurus_set_strict_mode(old_strict);  /* Restore strict mode */
            }

            if (attr_name && attr_value) {
                /* Escape attribute value for C14N (<, &, and " need escaping) */
                char* escaped_value = c14n_escape_text(attr_value, 1);
                if (escaped_value) {
                    len = snprintf(temp, sizeof(temp), " %.*s=\"%s\"", (int)attr_name_len, attr_name, escaped_value);
                    APPEND_STRING(temp, len);
                    free(escaped_value);
                }

                /* Free temporary value if we created it (wasn't cached) */
                if (attr_value != attr->value && attr_value) {
                    free((void*)attr_value);
                }
            }
        }
    }

    /* Close opening tag */
    if (!has_children && !has_content) {
        /* Empty element: <tag></tag> (NOT <tag/>) */
        len = snprintf(temp, sizeof(temp), "></%s>", name ? name : "element");
        APPEND_STRING(temp, len);
    } else {
        len = snprintf(temp, sizeof(temp), ">");
        APPEND_STRING(temp, len);

        /* Add children - traverse ALL children including text nodes */
        TaurusNode* child = (TaurusNode*)taurus_node_first_child_internal((TaurusNode*)elem);
        while (child) {
            if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
                c14n_serialize_element((TaurusElement)child, buffer, size, capacity);
                child = taurus_node_get_next_sibling(child);
            } else if (child->type == TAURUS_NODE_TYPE_TEXT) {
                const char* text = taurus_text_get_content((TaurusTextNode*)child);
                if (text) {
                    /* Escape text content for C14N (only < and & need escaping) */
                    char* escaped_text = c14n_escape_text(text, 0);
                    if (escaped_text) {
                        APPEND_STRING(escaped_text, (int)strlen(escaped_text));
                        free(escaped_text);
                    }
                }
                child = taurus_node_get_next_sibling(child);
            } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
                /* CDATA content is treated as character data in C14N */
                const char* text = taurus_cdata_get_content((TaurusCDATANode*)child);
                if (text) {
                    /* Escape CDATA content for C14N (same rules as text content) */
                    char* escaped_text = c14n_escape_text(text, 0);
                    if (escaped_text) {
                        APPEND_STRING(escaped_text, (int)strlen(escaped_text));
                        free(escaped_text);
                    }
                }
                child = taurus_node_get_next_sibling(child);
            } else if (child->type == TAURUS_NODE_TYPE_PI) {
                /* Processing Instruction: <?target data?> */
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
                child = taurus_node_get_next_sibling(child);
            } else if (child->type == TAURUS_NODE_TYPE_COMMENT) {
                /* Comments are NOT included in C14N - skip */
                child = taurus_node_get_next_sibling(child);
            } else {
                /* Unknown node type - stop */
                child = NULL;
            }
        }

        /* Closing tag */
        len = snprintf(temp, sizeof(temp), "</%s>", name ? name : "element");
        APPEND_STRING(temp, len);
    }

    /* Cleanup */
    if (sorted_attrs) free(sorted_attrs);

cleanup:
    return;
}

/**
 * Canonicalize document to C14N format (Public API)
 */
TAURUS_API char* taurus_c14n_canonicalize(struct taurus_document* doc, int version, int flags) {
    (void)version;  /* Version reserved for future C14N 1.1 differences */
    (void)flags;    /* Flags reserved for future use */

    if (!doc) return NULL;

    /* Get root element */
    TaurusElement root = (TaurusElement)doc->new_dom_root;
    if (!root) return NULL;

    /* Allocate initial buffer */
    size_t capacity = 4096;
    size_t size = 0;
    char* buf = (char*)malloc(capacity);
    if (!buf) return NULL;
    buf[0] = '\0';

    /* Add document-level processing instructions before root element */
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

    /* Serialize document in C14N format */
    char* buffer = buf;
    c14n_serialize_element(root, &buffer, &size, &capacity);
    buf = buffer;

    /* Note: Line ending normalization is done during parsing per XML 1.0 spec.
     * Any remaining \r in the document should be escaped as &#xD; by the parser.
     * We don't do additional line ending transformation here. */
    buf[size] = '\0';

    return buf;
}

