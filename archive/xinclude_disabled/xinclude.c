/* lib/src/xinclude/xinclude.c - XInclude 1.0 Implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Implements W3C XInclude 1.0:
 * https://www.w3.org/TR/xinclude/
 */

#include "xinclude.h"
#include "../dom/element.h"
#include "../dom/node.h"
#include "../dom/text.h"
#include "../parse/parser_new.h"
#include "../common/string_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Check if namespace URI matches XInclude namespace */
static int is_xinclude_namespace(TaurusElement elem) {
    const char* ns = taurus_element_get_namespace_uri(elem);
    if (!ns) return 0;
    return strcmp(ns, TAURUS_XINCLUDE_NS) == 0;
}

/* Check if element name matches */
static int element_name_equals(TaurusElement elem, const char* name) {
    const char* elem_name = taurus_element_name(elem);
    if (!elem_name) return 0;
    return strcmp(elem_name, name) == 0;
}

/* ===========================================================================
 * Public API Implementation
 * =========================================================================== */

int taurus_xinclude_is_include_element(TaurusElement elem) {
    if (!elem) return 0;
    return is_xinclude_namespace(elem) &&
           element_name_equals(elem, TAURUS_XINCLUDE_INCLUDE);
}

int taurus_xinclude_is_fallback_element(TaurusElement elem) {
    if (!elem) return 0;
    return is_xinclude_namespace(elem) &&
           element_name_equals(elem, TAURUS_XINCLUDE_FALLBACK);
}

const char* taurus_xinclude_get_href(TaurusElement include_elem) {
    if (!include_elem) return NULL;
    return taurus_element_attribute(include_elem, TAURUS_XINCLUDE_ATTR_HREF);
}

const char* taurus_xinclude_get_parse(TaurusElement include_elem) {
    if (!include_elem) return NULL;
    const char* parse = taurus_element_attribute(include_elem, TAURUS_XINCLUDE_ATTR_PARSE);
    /* Default to XML if not specified */
    return parse ? parse : TAURUS_XINCLUDE_PARSE_XML;
}

const char* taurus_xinclude_get_xpointer(TaurusElement include_elem) {
    if (!include_elem) return NULL;
    return taurus_element_attribute(include_elem, TAURUS_XINCLUDE_ATTR_XPOINTER);
}

const char* taurus_xinclude_get_encoding(TaurusElement include_elem) {
    if (!include_elem) return NULL;
    return taurus_element_attribute(include_elem, TAURUS_XINCLUDE_ATTR_ENCODING);
}

/* Read file into buffer */
static char* read_file(const char* filename, size_t* out_len) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = (char*)malloc(size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    *out_len = size;
    return buf;
}

/* Resolve href relative to base URL */
static char* resolve_href(const char* href, const char* base_url) {
    if (!href) return NULL;

    /* If no base URL, return href as-is */
    if (!base_url || strlen(base_url) == 0) {
        return strdup(href);
    }

    /* Simple implementation: if href is absolute, return it
     * Otherwise, concatenate base + href
     * TODO: Implement proper URL resolution RFC 3986 */
    if (href[0] == '/' || strstr(href, "://")) {
        return strdup(href);
    }

    /* Concatenate base and href */
    size_t base_len = strlen(base_url);
    size_t href_len = strlen(href);
    char* result = (char*)malloc(base_len + href_len + 2);

    /* Remove trailing slash from base if present */
    if (base_len > 0 && base_url[base_len - 1] == '/') {
        snprintf(result, base_len + href_len + 1, "%s%s", base_url, href);
    } else {
        snprintf(result, base_len + href_len + 2, "%s/%s", base_url, href);
    }

    return result;
}

/* Find fallback child element */
static TaurusElement find_fallback_child(TaurusElement include_elem) {
    TaurusNode* child = taurus_node_first_child_internal((TaurusNode*)include_elem);
    while (child) {
        if (TAURUS_NODE_IS_ELEMENT(child)) {
            TaurusElement elem = (TaurusElement)child;
            if (taurus_xinclude_is_fallback_element(elem)) {
                return elem;
            }
        }
        child = child->next_sibling;
    }
    return NULL;
}

/* Process XML include */
static TaurusStatus process_xml_include(TaurusElement include_elem,
                                        const char* href,
                                        const char* xpointer,
                                        TaurusDocument target_doc) {
    /* Read included file */
    size_t file_len;
    char* file_content = read_file(href, &file_len);
    if (!file_content) {
        return TAURUS_ERROR_IO;
    }

    /* Parse included document */
    TaurusStatus status;
    TaurusDocument included_doc = taurus_parse_string_inplace(file_content, file_len, &status);
    if (!included_doc) {
        free(file_content);
        return status;
    }

    /* Get root element of included document */
    TaurusElement included_root = taurus_document_root(included_doc);
    if (!included_root) {
        taurus_document_free(included_doc);
        free(file_content);
        return TAURUS_ERROR_PARSE;
    }

    /* TODO: If xpointer specified, evaluate it to select specific nodes
     * For now, we just include the entire document */

    /* Get include node for insertion */
    TaurusNode* include_node = (TaurusNode*)include_elem;

    /* Clone and insert all children of included root */
    TaurusNode* child = taurus_node_first_child_internal((TaurusNode*)included_root);
    while (child) {
        TaurusNode* next = child->next_sibling;

        /* Detach from included document */
        taurus_node_remove(child);

        /* Change parent to target document */
        if (TAURUS_NODE_IS_ELEMENT(child)) {
            TaurusElement elem_child = (TaurusElement)child;
            elem_child->document = target_doc;
        }

        /* Insert before include element using internal API */
        taurus_node_insert_before(include_node, child);

        child = next;
    }

    /* Clean up */
    taurus_document_free(included_doc);
    free(file_content);

    return TAURUS_OK;
}

/* Process text include */
static TaurusStatus process_text_include(TaurusElement include_elem,
                                        const char* href,
                                        const char* encoding,
                                        TaurusDocument target_doc) {
    /* Read included file */
    size_t file_len;
    char* file_content = read_file(href, &file_len);
    if (!file_content) {
        return TAURUS_ERROR_IO;
    }

    /* Get pool from target document */
    TaurusMemoryPool* pool = target_doc->pool;

    /* Create text node with file content */
    TaurusTextNode* text_node = taurus_text_create_fast(file_content, file_len, pool);
    if (!text_node) {
        free(file_content);
        return TAURUS_ERROR_MEMORY;
    }

    /* Set document */
    text_node->base.parent = ((TaurusNode*)include_elem)->parent;

    /* Insert text before include element */
    taurus_node_insert_before((TaurusNode*)include_elem, (TaurusNode*)text_node);

    free(file_content);
    return TAURUS_OK;
}

/* Recursively process XInclude elements in subtree */
static TaurusStatus process_xinclude_recursive(TaurusNode* node,
                                              const char* base_url,
                                              TaurusDocument doc) {
    if (!node) return TAURUS_OK;

    /* Process element nodes */
    if (TAURUS_NODE_IS_ELEMENT(node)) {
        TaurusElement elem = (TaurusElement)node;

        /* Check if this is an XInclude include element */
        if (taurus_xinclude_is_include_element(elem)) {
            const char* href = taurus_xinclude_get_href(elem);
            const char* parse = taurus_xinclude_get_parse(elem);
            const char* xpointer = taurus_xinclude_get_xpointer(elem);
            const char* encoding = taurus_xinclude_get_encoding(elem);

            if (!href) {
                /* Missing href - check for fallback */
                TaurusElement fallback = find_fallback_child(elem);
                if (fallback) {
                    /* Process fallback content recursively */
                    return process_xinclude_recursive((TaurusNode*)fallback, base_url, doc);
                }
                return TAURUS_ERROR_PARSE; /* No href, no fallback */
            }

            /* Resolve href */
            char* resolved_href = resolve_href(href, base_url);

            /* Process based on parse mode */
            TaurusStatus status;
            if (strcmp(parse, TAURUS_XINCLUDE_PARSE_TEXT) == 0) {
                status = process_text_include(elem, resolved_href, encoding, doc);
            } else {
                /* Default to XML */
                status = process_xml_include(elem, resolved_href, xpointer, doc);
            }

            free(resolved_href);

            if (status != TAURUS_OK) {
                /* Include failed - check for fallback */
                TaurusElement fallback = find_fallback_child(elem);
                if (fallback) {
                    /* Process fallback content recursively */
                    status = process_xinclude_recursive((TaurusNode*)fallback, base_url, doc);
                }
            }

            /* Remove include element (whether success or fallback) */
            if (status == TAURUS_OK) {
                taurus_node_remove(node);
                return TAURUS_OK; /* Don't process children of removed element */
            }

            return status;
        }
    }

    /* Recursively process children */
    TaurusNode* child = taurus_node_first_child_internal(node);
    while (child) {
        TaurusNode* next = child->next_sibling;
        TaurusStatus status = process_xinclude_recursive(child, base_url, doc);
        if (status != TAURUS_OK) {
            return status;
        }
        child = next;
    }

    return TAURUS_OK;
}

/* ===========================================================================
 * Main API
 * =========================================================================== */

TaurusStatus taurus_xinclude_process(TaurusDocument doc, const char* base_url) {
    if (!doc) return TAURUS_ERROR_NULL_ARG;

    TaurusElement root = taurus_document_root(doc);
    if (!root) return TAURUS_OK; /* Empty document */

    return process_xinclude_recursive((TaurusNode*)root, base_url, doc);
}
