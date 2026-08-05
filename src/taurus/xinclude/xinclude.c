/* xinclude/xinclude.c — XInclude 1.0 support.
 *
 * Implements XInclude 1.0 processing for both parse="text" and
 * parse="xml" modes.  Walks the document tree, finds xi:include
 * elements, and substitutes them with the included content.
 *
 *   parse="text" — load the file as plain text, splice a text node
 *                  in place of the xi:include element.
 *   parse="xml"  — load+parse the file as XML, deep-copy the root
 *                  of the included document into the parent document's
 *                  pool, splice the copy in place of the xi:include.
 *                  xpointer fragment selection is TODO 92 Phase 3.
 *
 * xi:fallback is honored in both modes.  Element-classification
 * helpers (is_include_element, etc.) are fully implemented and
 * documented in taurus.h.
 */

#include "../../include/taurus.h"
#include "../taurus_internal.h"
#include "../dom/element.h"
#include "../dom/node.h"
#include "../dom/text.h"
#include "../dom/cdata.h"
#include "../dom/comment.h"
#include "../dom/pi.h"
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

/* ---- cross-document deep copy ---- */

/* Deep-copy a node subtree into `target_pool`, rewriting every StringView
 * and char* into freshly pool-owned storage.  The returned subtree is
 * self-contained — none of its pointers reference the source document.
 *
 * Returns NULL on allocation failure (partial copies are abandoned
 * along with the pool, which is freed document-scoped). */
static TaurusNode* deep_copy_node(const TaurusNode* src,
                                   TaurusMemoryPool* target_pool);

static TaurusElement deep_copy_element(const TaurusElement src,
                                        TaurusMemoryPool* target_pool) {
    const char* name = taurus_element_get_name(src);
    TaurusElement dst = taurus_element_create_pooled(name, target_pool);
    if (!dst) return NULL;

    /* Copy attributes. */
    for (struct taurus_attribute* a = taurus_elem_first_attribute(src); a; a = a->next) {
        const char* a_name = a->name ? a->name : "";
        const char* a_value = a->value ? a->value : "";
        taurus_element_add_attribute_pooled(dst, a_name, a_value, target_pool);
    }

    /* Copy children (any type).  We can't use taurus_element_append_child
     * because it only links elements — mixed content needs the generic
     * set_next_sibling + parent.first_child / last_child wiring. */
    TaurusNode* prev_copy = NULL;
    for (TaurusNode* child = taurus_elem_first_child(src);
         child;
         child = taurus_node_get_next_sibling(child)) {

        TaurusNode* child_copy = deep_copy_node(child, target_pool);
        if (!child_copy) return NULL;

        if (prev_copy) {
            taurus_node_set_next_sibling(prev_copy, child_copy);
        } else {
            taurus_elem_set_first_child(dst, child_copy);
        }
        prev_copy = child_copy;
    }
    if (prev_copy) {
        taurus_elem_set_last_child(dst, prev_copy);
    }
    return dst;
}

static TaurusNode* deep_copy_node(const TaurusNode* src,
                                   TaurusMemoryPool* target_pool) {
    if (!src) return NULL;

    switch (src->type) {
        case TAURUS_NODE_TYPE_ELEMENT:
            return (TaurusNode*)deep_copy_element((TaurusElement)src, target_pool);

        case TAURUS_NODE_TYPE_TEXT: {
            const TaurusTextNode* t = (const TaurusTextNode*)src;
            const char* content = t->content ? t->content : "";
            return (TaurusNode*)taurus_text_create(content, strlen(content), target_pool);
        }

        case TAURUS_NODE_TYPE_CDATA: {
            const TaurusCDATANode* c = (const TaurusCDATANode*)src;
            const char* content = c->content ? c->content : "";
            return (TaurusNode*)taurus_cdata_create(content, strlen(content), target_pool);
        }

        case TAURUS_NODE_TYPE_COMMENT: {
            const TaurusCommentNode* cm = (const TaurusCommentNode*)src;
            const char* content = cm->content ? cm->content : "";
            return (TaurusNode*)taurus_comment_create(content, strlen(content), target_pool);
        }

        case TAURUS_NODE_TYPE_PI: {
            const TaurusPINode* pi = (const TaurusPINode*)src;
            const char* tgt = pi->target ? pi->target : "";
            const char* data = pi->data ? pi->data : "";
            return (TaurusNode*)taurus_pi_create(tgt, strlen(tgt), data, strlen(data), target_pool);
        }

        default:
            /* DOCTYPE and other specialized nodes don't appear in element
             * content; nothing to copy. */
            return NULL;
    }
}

/* Replace one child of `parent` with `new_node`.
 * Walks parent->first_child to find the node before `old_node`,
 * then splices new_node in. */
static void replace_child_node(TaurusElement parent,
                                TaurusNode* old_node,
                                TaurusNode* new_node) {
    if (!parent || !old_node || !new_node) return;

    TaurusNode* prev = NULL;
    TaurusNode* child = taurus_elem_first_child(parent);
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
        taurus_elem_set_first_child(parent, new_node);
    }

    /* Update last_child if old_node was last. */
    if (taurus_elem_last_child(parent) == old_node) {
        taurus_elem_set_last_child(parent, new_node);
    }
}

/* Build the full path to an xi:include href relative to base_url.
 * Writes into out (fixed buffer) and returns out on success, NULL on
 * overflow.  No dynamic allocation — keeps the call site simple. */
static char* join_path(char* out, size_t out_size,
                       const char* base_url, const char* href) {
    if (!href) return NULL;
    int n;
    if (base_url && base_url[0]) {
        n = snprintf(out, out_size, "%s/%s", base_url, href);
    } else {
        n = snprintf(out, out_size, "%s", href);
    }
    return (n < 0 || (size_t)n >= out_size) ? NULL : out;
}

/* Find the xi:fallback child of an xi:include element, if any. */
static TaurusElement find_fallback(TaurusElement include_elem) {
    for (TaurusNodeRef fb = taurus_node_first_child((TaurusNodeRef)include_elem);
         fb;
         fb = taurus_node_next_sibling(fb)) {
        TaurusElement fe = taurus_node_as_element(fb);
        if (fe && taurus_xinclude_is_fallback_element(fe)) return fe;
    }
    return NULL;
}

/* Recursive walker. Bottom-up so that included content can itself
 * contain nested xi:include elements.  Returns 0 on first hard failure
 * (parse error, alloc failure), 1 otherwise. */
static int process_element_xinclude(TaurusElement elem,
                                     struct taurus_document* doc,
                                     const char* base_url) {
    if (!elem) return 1;

    TaurusNodeRef child = taurus_node_first_child((TaurusNodeRef)elem);
    while (child) {
        TaurusNodeRef next = taurus_node_next_sibling(child);
        if (taurus_node_get_type(child) == TAURUS_NODE_TYPE_ELEMENT) {
            int rc = process_element_xinclude((TaurusElement)child, doc, base_url);
            if (!rc) return 0;
        }
        child = next;
    }

    if (!taurus_xinclude_is_include_element(elem)) return 1;

    const char* parse = taurus_xinclude_get_parse(elem);
    const char* href = taurus_xinclude_get_href(elem);
    if (!href || !href[0]) return 1;

    char full_path[4096];
    if (!join_path(full_path, sizeof(full_path), base_url, href)) return 1;

    size_t content_len = 0;
    char* content = load_file_content(full_path, &content_len);

    /* The substitute node — element for parse="xml", text for parse="text",
     * NULL on failure (fallback path below). */
    TaurusNode* substitute = NULL;

    if (content) {
        int is_xml = (!parse || strcmp(parse, "xml") == 0);

        if (is_xml) {
            TaurusStatus st = TAURUS_OK;
            TaurusDocument included_doc = taurus_parse_string(content, content_len, &st);
            if (included_doc && st == TAURUS_OK) {
                /* Resolve any xi:include nested inside the included doc
                 * before splicing — XInclude processes recursively. */
                taurus_xinclude_process(included_doc, base_url);
                TaurusElement inc_root = taurus_document_root(included_doc);
                if (inc_root) {
                    /* Phase 4 of TODO 92: xpointer attribute selects
                     * fragment from the included document. The value
                     * is evaluated as an XPath expression against the
                     * included doc. If it returns a nodeset, the
                     * resulting nodes are spliced into the parent
                     * (replacing the xi:include). Non-nodeset results
                     * or missing xpointer fall back to the root element.
                     *
                     * The XPointer framework also defines scheme-
                     * qualified forms (xpointer(...), element(/1/2),
                     * xmlns(...)) which we don't support here — the
                     * raw attribute value is passed straight to the
                     * XPath engine as a best-effort implementation. */
                    const char* xp = taurus_xinclude_get_xpointer(elem);
                    if (xp && xp[0]) {
                        TaurusXPathResult xr = taurus_xpath_eval(
                            included_doc, NULL, xp);
                        if (xr) {
                            size_t n = taurus_xpath_result_count(xr);
                            if (n > 0) {
                                /* Splice each node from the result. The
                                 * first one becomes the substitute; the
                                 * rest are appended as siblings via
                                 * element_insert_after on each new
                                 * node's predecessor — but we don't
                                 * have multi-splice API yet, so for
                                 * now take just the first node. */
                                TaurusElement first = taurus_xpath_result_get(xr, 0);
                                if (first) {
                                    substitute = deep_copy_node(
                                        (const TaurusNode*)first, doc->pool);
                                }
                            }
                            taurus_xpath_result_free(xr);
                        }
                    }
                    if (!substitute) {
                        substitute = deep_copy_node(
                            (const TaurusNode*)inc_root, doc->pool);
                    }
                }
            }
            if (included_doc) taurus_document_free(included_doc);
        } else if (strcmp(parse, "text") == 0) {
            substitute = (TaurusNode*)taurus_text_create(content, content_len, doc->pool);
        }
        /* Unknown parse= value: silently skip, per XInclude spec. */
        free(content);
    }

    if (!substitute) {
        /* Resource error: try xi:fallback before giving up. */
        TaurusElement fb = find_fallback(elem);
        if (fb) {
            char* fb_text = taurus_element_get_text_content(fb);
            if (fb_text) {
                substitute = (TaurusNode*)taurus_text_create(fb_text, strlen(fb_text), doc->pool);
                taurus_free(fb_text);
            }
        }
        /* No fallback: spec says resource error SHOULD be reported; we're
         * lenient and leave the xi:include in place. */
    }

    if (substitute) {
        TaurusElement parent = taurus_element_get_parent(elem);
        if (parent) {
            replace_child_node(parent, (TaurusNode*)elem, substitute);

            /* The substitute was allocated from doc->pool but its
             * document pointer (and the document pointers of every
             * descendant) is still NULL.  Re-stamp them so future
             * callers can reach the pool through element->document. */
            if (substitute->type == TAURUS_NODE_TYPE_ELEMENT) {
                taurus_element_set_document_tree((TaurusElement)substitute, doc);
            }
        }
    }

    return 1;
}

TAURUS_API TaurusStatus taurus_xinclude_process(TaurusDocument doc, const char* base_url) {
    if (!doc) return TAURUS_ERROR_NULL_ARG;

    TaurusElement root = taurus_document_root(doc);
    if (!root) return TAURUS_OK;

    int rc = process_element_xinclude(root, doc, base_url);
    if (!rc) return TAURUS_ERROR_IO;
    return TAURUS_OK;
}
