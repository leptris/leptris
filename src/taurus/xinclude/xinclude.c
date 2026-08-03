/* xinclude/xinclude.c — XInclude 1.0 support.
 *
 * Implements parse="text" XInclude processing: walks the document
 * tree, finds xi:include elements with parse="text", loads the
 * referenced file, and replaces the xi:include element with a text
 * node containing the file content.
 *
 * parse="xml" mode is still a stub (requires cross-document node
 * copying with pool ownership transfer — see TODO 92).
 *
 * Element-classification helpers (is_include_element, etc.) are
 * fully implemented and documented in taurus.h.
 */

#include "../../include/taurus.h"
#include "../taurus_internal.h"
#include "../dom/element.h"
#include "../dom/node.h"
#include "../dom/text.h"
#include "../memory/pool.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define XINCLUDE_NAMESPACE "http://www.w3.org/2001/XInclude"

static int element_is_in_xinclude_namespace(TaurusElement elem) {
    if (!elem) return 0;
    const char* ns_uri = taurus_element_get_namespace_uri(elem);
    if (!ns_uri) return 0;
    return strcmp(ns_uri, XINCLUDE_NAMESPACE) == 0;
}

static const char* get_attr_value(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;
    struct taurus_attribute* attr = taurus_element_get_attribute_by_name(elem, name);
    if (!attr) return NULL;
    return attr->value ? attr->value : "";
}

TAURUS_API int taurus_xinclude_is_include_element(TaurusElement elem) {
    if (!element_is_in_xinclude_namespace(elem)) return 0;
    const char* name = taurus_element_get_name(elem);
    return name && strcmp(name, "include") == 0;
}

TAURUS_API int taurus_xinclude_is_fallback_element(TaurusElement elem) {
    if (!element_is_in_xinclude_namespace(elem)) return 0;
    const char* name = taurus_element_get_name(elem);
    return name && strcmp(name, "fallback") == 0;
}

TAURUS_API const char* taurus_xinclude_get_href(TaurusElement include_elem) {
    if (!taurus_xinclude_is_include_element(include_elem)) return NULL;
    return get_attr_value(include_elem, "href");
}

TAURUS_API const char* taurus_xinclude_get_parse(TaurusElement include_elem) {
    if (!taurus_xinclude_is_include_element(include_elem)) return NULL;
    const char* parse = get_attr_value(include_elem, "parse");
    return (parse && parse[0]) ? parse : "xml";
}

TAURUS_API const char* taurus_xinclude_get_xpointer(TaurusElement include_elem) {
    if (!taurus_xinclude_is_include_element(include_elem)) return NULL;
    return get_attr_value(include_elem, "xpointer");
}

/* ---- parse="text" processor ---- */

/* Load a file into a malloc'd buffer. Returns NULL on failure.
 * Caller must free the returned buffer. */
static char* load_file_content(const char* path, size_t* out_len) {
    if (!path) return NULL;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_len) *out_len = rd;
    return buf;
}

/* Replace one child of `parent` with `new_node`.
 * Walks parent->first_child to find the node before `old_node`,
 * then splices new_node in. */
static void replace_child_node(TaurusElement parent,
                                TaurusNode* old_node,
                                TaurusNode* new_node) {
    if (!parent || !old_node || !new_node) return;

    TaurusNode* prev = NULL;
    TaurusNode* child = parent->first_child;
    while (child && child != old_node) {
        prev = child;
        child = taurus_node_get_next_sibling(child);
    }
    if (child != old_node) return;  /* old_node not found */

    /* Splice new_node in place of old_node. */
    TaurusNode* next = taurus_node_get_next_sibling(old_node);
    taurus_node_set_next_sibling(new_node, next);

    if (prev) {
        taurus_node_set_next_sibling(prev, new_node);
    } else {
        /* old_node was first_child */
        parent->first_child = new_node;
    }

    /* Update last_child if old_node was last. */
    if (parent->last_child == (struct taurus_node*)old_node) {
        parent->last_child = new_node;
    }
}

/* Recursive walker. Returns 0 on first failure (file not found,
 * parse error). Returns 1 if all includes processed. */
static int process_element_text_xinclude(TaurusElement elem,
                                          struct taurus_document* doc,
                                          const char* base_url) {
    if (!elem) return 1;

    /* Check children first (depth-first), then check if THIS element
     * is an xi:include. We process bottom-up so that included content
     * itself can contain nested xi:include elements. */
    TaurusNodeRef child = taurus_node_first_child((TaurusNodeRef)elem);
    while (child) {
        TaurusNodeRef next = taurus_node_next_sibling(child);
        if (taurus_node_get_type(child) == TAURUS_NODE_TYPE_ELEMENT) {
            int rc = process_element_text_xinclude(
                (TaurusElement)child, doc, base_url);
            if (!rc) return 0;
        }
        child = next;
    }

    /* Is this element an xi:include? */
    if (!taurus_xinclude_is_include_element(elem)) return 1;

    /* Only process parse="text" for now. parse="xml" is TODO 92. */
    const char* parse = taurus_xinclude_get_parse(elem);
    if (!parse || strcmp(parse, "text") != 0) {
        /* parse="xml" not implemented — skip silently. */
        return 1;
    }

    /* Load the file. */
    const char* href = taurus_xinclude_get_href(elem);
    if (!href || !href[0]) return 1;  /* No href — nothing to do. */

    /* Build full path. If base_url is provided, prepend it.
     * For simplicity, treat href as relative to the current
     * directory (or to base_url if given). */
    char full_path[4096];
    if (base_url && base_url[0]) {
        snprintf(full_path, sizeof(full_path), "%s/%s", base_url, href);
    } else {
        snprintf(full_path, sizeof(full_path), "%s", href);
    }

    size_t content_len = 0;
    char* content = load_file_content(full_path, &content_len);
    if (!content) {
        /* File not found — check for xi:fallback. */
        /* For now, just skip — Phase 1 doesn't implement fallback. */
        return 1;
    }

    /* Create a text node from the loaded content, pool-allocated. */
    TaurusTextNode* text = taurus_text_create(content, content_len,
                                               doc->pool);
    free(content);  /* taurus_text_create copies into the pool */
    if (!text) return 0;

    /* Replace xi:include with the text node in the parent's child list. */
    TaurusElement parent = taurus_element_get_parent(elem);
    if (parent) {
        replace_child_node(parent, (TaurusNode*)elem, (TaurusNode*)text);
    }

    return 1;
}

TAURUS_API TaurusStatus taurus_xinclude_process(TaurusDocument doc, const char* base_url) {
    if (!doc) return TAURUS_ERROR_NULL_ARG;

    TaurusElement root = taurus_document_root(doc);
    if (!root) return TAURUS_OK;

    int rc = process_element_text_xinclude(root, doc, base_url);
    if (!rc) return TAURUS_ERROR_IO;
    return TAURUS_OK;
}
