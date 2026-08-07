/* dom/node_public.c — Public TaurusNodeRef API.
 *
 * Extracted from taurus.c (TODO 42 phase 2). These are the public-facing
 * wrappers around the node-navigation helpers in dom/node.c.
 */

#include "../include/taurus.h"
#include "../taurus_internal.h"
#include "element.h"
#include "element_index.h"
#include "node.h"
#include "text.h"
#include "comment.h"
#include "cdata.h"
#include "pi.h"

TAURUS_API int taurus_node_get_type(TaurusNodeRef node) {
    if (!node) return 0; /* TAURUS_NODE_TYPE_ELEMENT */
    return (int)node->type;
}

TAURUS_API TaurusNodeRef taurus_node_first_child(TaurusNodeRef node) {
    /* Use the internal accessor that returns ANY child type (text,
     * element, CDATA, etc.) — the public taurus_element_get_first_child
     * would skip non-element children and break node-level traversal. */
    return (TaurusNodeRef)taurus_node_first_child_internal((TaurusNode*)node);
}

TAURUS_API TaurusNodeRef taurus_node_last_child(TaurusNodeRef node) {
    return (TaurusNodeRef)taurus_node_last_child_internal((TaurusNode*)node);
}

TAURUS_API TaurusNodeRef taurus_node_next_sibling(TaurusNodeRef node) {
    if (!node) return NULL;
    return (TaurusNodeRef)taurus_node_get_next_sibling(node);
}

TAURUS_API TaurusNodeRef taurus_node_previous_sibling(TaurusNodeRef node) {
    if (!node) return NULL;
    if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
        TaurusElement elem = (TaurusElement)node;
        TaurusElement parent = taurus_element_get_parent(elem);
        if (!parent) return NULL;

        TaurusNodeRef prev = NULL;
        TaurusNodeRef child = (TaurusNodeRef)taurus_elem_first_child(parent);
        while (child && child != node) {
            prev = child;
            child = (TaurusNodeRef)taurus_node_get_next_sibling(child);
        }
        return prev;
    }
    return NULL;
}

TAURUS_API size_t taurus_node_child_count(TaurusNodeRef node) {
    if (!node) return 0;
    return taurus_node_child_count_internal(node);
}

TAURUS_API TaurusElement taurus_node_as_element(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_ELEMENT) return NULL;
    return (TaurusElement)node;
}

TAURUS_API TaurusNodeRef taurus_element_as_node(TaurusElement elem) {
    return (TaurusNodeRef)elem;
}

TAURUS_API const char* taurus_text_node_get_content(TaurusNodeRef node) {
    if (!node) return NULL;
    if (node->type == TAURUS_NODE_TYPE_TEXT) {
        return taurus_text_get_content((TaurusTextNode*)node);
    }
    if (node->type == TAURUS_NODE_TYPE_CDATA) {
        return ((TaurusCDATANode*)node)->content;
    }
    return NULL;
}

TAURUS_API const char* taurus_comment_node_get_content(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_COMMENT) return NULL;
    return ((TaurusCommentNode*)node)->content;
}

TAURUS_API const char* taurus_cdata_node_get_content(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_CDATA) return NULL;
    return ((TaurusCDATANode*)node)->content;
}

TAURUS_API const char* taurus_pi_node_get_target(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_PI) return NULL;
    return ((TaurusPINode*)node)->target;
}

TAURUS_API const char* taurus_pi_node_get_data(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_PI) return NULL;
    return ((TaurusPINode*)node)->data;
}

/* ============================================================================
 * Typed node creators (issue #167).
 *
 * Each creator allocates from the document's pool, copies the content
 * into the pool, and returns an unattached node. Use
 * taurus_element_append_child to attach.
 *
 * For setter functions, the new content is pool-copied so the caller
 * may free or modify the input immediately. The previous content is
 * NOT freed separately -- it lives in the pool and is reclaimed when
 * the document is freed.
 * ============================================================================ */

/* Look up the document that owns a node. Goes via the parent element
 * (every node reachable from a tree was attached via append_child,
 * which sets parent_off and propagates the document pointer on
 * element children). For unattached nodes, returns NULL. */
static TaurusDocument node_public_document(TaurusNodeRef node) {
    if (!node) return NULL;
    if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
        return ((TaurusElement)node)->document;
    }
    TaurusElement parent = taurus_node_parent(node);
    return parent ? parent->document : NULL;
}

/* Pool-strdup helper scoped to this TU. */
static char* node_public_pool_strdup(TaurusMemoryPool* pool,
                                      const char* s, size_t len) {
    if (!pool) return NULL;
    char* copy = (char*)taurus_pool_alloc(pool, len + 1);
    if (!copy) return NULL;
    if (len > 0) memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

TAURUS_API TaurusNodeRef taurus_text_node_create(TaurusDocument doc,
                                                  const char* content) {
    if (!doc) return NULL;
    /* TODO 139 Phase D: trigger lazy promote so doc->pool is set. */
    taurus_document_ensure_promoted(doc);
    if (!doc->pool) return NULL;
    if (!content) content = "";
    size_t len = strlen(content);
    TaurusTextNode* n = taurus_text_create_borrowed(content, len, doc->pool);
    if (!n) return NULL;
    /* Materialize a pool-owned NUL-terminated copy so future reads via
     * taurus_text_node_get_content don't have to lazy-alloc. */
    char* copy = node_public_pool_strdup(doc->pool, content, len);
    if (copy) {
        n->content = copy;
        n->content_len = len;
        n->borrowed = 0;
    }
    return (TaurusNodeRef)n;
}

TAURUS_API TaurusNodeRef taurus_comment_node_create(TaurusDocument doc,
                                                     const char* content) {
    if (!doc) return NULL;
    taurus_document_ensure_promoted(doc);
    if (!doc->pool) return NULL;
    if (!content) content = "";
    return (TaurusNodeRef)taurus_comment_create(content, strlen(content),
                                                  doc->pool);
}

TAURUS_API TaurusNodeRef taurus_cdata_node_create(TaurusDocument doc,
                                                   const char* content) {
    if (!doc) return NULL;
    taurus_document_ensure_promoted(doc);
    if (!doc->pool) return NULL;
    if (!content) content = "";
    return (TaurusNodeRef)taurus_cdata_create(content, strlen(content),
                                                doc->pool);
}

TAURUS_API TaurusNodeRef taurus_pi_node_create(TaurusDocument doc,
                                                const char* target,
                                                const char* data) {
    if (!doc) return NULL;
    taurus_document_ensure_promoted(doc);
    if (!doc->pool || !target) return NULL;
    if (!data) data = "";
    return (TaurusNodeRef)taurus_pi_create(target, strlen(target),
                                             data, strlen(data), doc->pool);
}

TAURUS_API TaurusStatus taurus_text_node_set_content(TaurusNodeRef node,
                                                      const char* content) {
    if (!node) return TAURUS_ERROR_NULL_ARG;
    if (node->type != TAURUS_NODE_TYPE_TEXT) return TAURUS_ERROR_INVALID_ARG;
    if (!content) content = "";
    size_t len = strlen(content);
    TaurusTextNode* t = (TaurusTextNode*)node;
    if (!t->pool) {
        /* Use parent doc pool if the node's own pool is unset
         * (happens after manual construction in some paths). */
        TaurusDocument doc = node_public_document(node);
        if (!doc || !doc->pool) return TAURUS_ERROR_INVALID_ARG;
        t->pool = doc->pool;
    }
    char* copy = node_public_pool_strdup(t->pool, content, len);
    if (!copy) return TAURUS_ERROR_MEMORY;
    t->content = copy;
    t->content_len = len;
    t->borrowed = 0;
    return TAURUS_OK;
}

TAURUS_API TaurusStatus taurus_cdata_node_set_content(TaurusNodeRef node,
                                                       const char* content) {
    if (!node) return TAURUS_ERROR_NULL_ARG;
    if (node->type != TAURUS_NODE_TYPE_CDATA) return TAURUS_ERROR_INVALID_ARG;
    if (!content) content = "";
    size_t len = strlen(content);
    TaurusCDATANode* c = (TaurusCDATANode*)node;
    TaurusDocument doc = node_public_document(node);
    if (!doc || !doc->pool) return TAURUS_ERROR_INVALID_ARG;
    char* copy = node_public_pool_strdup(doc->pool, content, len);
    if (!copy) return TAURUS_ERROR_MEMORY;
    c->content = copy;
    return TAURUS_OK;
}

TAURUS_API TaurusStatus taurus_comment_node_set_content(TaurusNodeRef node,
                                                         const char* content) {
    if (!node) return TAURUS_ERROR_NULL_ARG;
    if (node->type != TAURUS_NODE_TYPE_COMMENT) return TAURUS_ERROR_INVALID_ARG;
    if (!content) content = "";
    size_t len = strlen(content);
    TaurusCommentNode* c = (TaurusCommentNode*)node;
    TaurusDocument doc = node_public_document(node);
    if (!doc || !doc->pool) return TAURUS_ERROR_INVALID_ARG;
    char* copy = node_public_pool_strdup(doc->pool, content, len);
    if (!copy) return TAURUS_ERROR_MEMORY;
    c->content = copy;
    return TAURUS_OK;
}

TAURUS_API TaurusStatus taurus_pi_node_set_target(TaurusNodeRef node,
                                                   const char* target) {
    if (!node) return TAURUS_ERROR_NULL_ARG;
    if (node->type != TAURUS_NODE_TYPE_PI) return TAURUS_ERROR_INVALID_ARG;
    if (!target) return TAURUS_ERROR_NULL_ARG;
    size_t len = strlen(target);
    TaurusPINode* p = (TaurusPINode*)node;
    TaurusDocument doc = node_public_document(node);
    if (!doc || !doc->pool) return TAURUS_ERROR_INVALID_ARG;
    char* copy = node_public_pool_strdup(doc->pool, target, len);
    if (!copy) return TAURUS_ERROR_MEMORY;
    p->target = copy;
    return TAURUS_OK;
}

TAURUS_API TaurusStatus taurus_pi_node_set_data(TaurusNodeRef node,
                                                 const char* data) {
    if (!node) return TAURUS_ERROR_NULL_ARG;
    if (node->type != TAURUS_NODE_TYPE_PI) return TAURUS_ERROR_INVALID_ARG;
    if (!data) data = "";
    size_t len = strlen(data);
    TaurusPINode* p = (TaurusPINode*)node;
    TaurusDocument doc = node_public_document(node);
    if (!doc || !doc->pool) return TAURUS_ERROR_INVALID_ARG;
    char* copy = node_public_pool_strdup(doc->pool, data, len);
    if (!copy) return TAURUS_ERROR_MEMORY;
    p->data = copy;
    return TAURUS_OK;
}

/* ============================================================================
 * Generic parent + unlink (issue #168).
 * ============================================================================ */

TAURUS_API TaurusElement taurus_node_parent(TaurusNodeRef node) {
    if (!node) return NULL;
    if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
        return taurus_element_get_parent((TaurusElement)node);
    }
    /* Non-element nodes carry parent_off (issue #168). The parser
     * and append_child_internal both populate it when attaching. */
    switch (node->type) {
        case TAURUS_NODE_TYPE_TEXT:
            return taurus_textnode_parent((TaurusTextNode*)node);
        case TAURUS_NODE_TYPE_COMMENT:
            return taurus_comment_parent((TaurusCommentNode*)node);
        case TAURUS_NODE_TYPE_CDATA:
            return taurus_cdata_parent((TaurusCDATANode*)node);
        case TAURUS_NODE_TYPE_PI:
            return taurus_pi_parent((TaurusPINode*)node);
        default:
            return NULL;
    }
}

TAURUS_API TaurusStatus taurus_node_unlink(TaurusNodeRef node) {
    if (!node) return TAURUS_ERROR_NULL_ARG;
    TaurusElement parent = taurus_node_parent(node);
    if (!parent) return TAURUS_ERROR_NOT_FOUND;

    /* Splice node out of parent's child chain. We need the previous
     * sibling to update its next_sibling pointer; if node is the
     * first child, update parent.first_child instead. */
    TaurusNodeRef prev = NULL;
    TaurusNodeRef cur = (TaurusNodeRef)taurus_elem_first_child(parent);
    while (cur && cur != node) {
        prev = cur;
        cur = (TaurusNodeRef)taurus_node_get_next_sibling(cur);
    }
    if (!cur) {
        /* Node is not in parent's child chain -- corrupt tree state. */
        return TAURUS_ERROR_NOT_FOUND;
    }

    TaurusNodeRef next = (TaurusNodeRef)taurus_node_get_next_sibling(node);
    if (prev) {
        taurus_node_set_next_sibling(prev, next);
    } else {
        /* Node was first child. */
        taurus_elem_set_first_child(parent, next);
        /* If also last child (only child), clear last too. */
        if (taurus_elem_last_child(parent) == (TaurusNode*)node) {
            taurus_elem_set_last_child(parent, prev ? (TaurusNode*)prev : NULL);
        }
    }
    /* If node was last child, update last to prev. */
    if (taurus_elem_last_child(parent) == (TaurusNode*)node) {
        taurus_elem_set_last_child(parent, prev ? (TaurusNode*)prev : NULL);
    }

    /* Clear node's own sibling + parent links so it's a clean orphan. */
    taurus_node_set_next_sibling(node, NULL);
    if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
        taurus_element_set_parent((TaurusElement)node, NULL);
    } else {
        switch (node->type) {
            case TAURUS_NODE_TYPE_TEXT:
                taurus_textnode_set_parent((TaurusTextNode*)node, NULL);
                break;
            case TAURUS_NODE_TYPE_COMMENT:
                taurus_comment_set_parent((TaurusCommentNode*)node, NULL);
                break;
            case TAURUS_NODE_TYPE_CDATA:
                taurus_cdata_set_parent((TaurusCDATANode*)node, NULL);
                break;
            case TAURUS_NODE_TYPE_PI:
                taurus_pi_set_parent((TaurusPINode*)node, NULL);
                break;
            default:
                break;
        }
    }

    if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
        if (parent->child_count > 0) parent->child_count--;
    }
    /* Invalidate element index (mutation). */
    if (parent->document) {
        taurus_element_index_invalidate(parent->document);
    }
    return TAURUS_OK;
}

/* ============================================================================
 * node_line + node_compare (issue #172).
 * ============================================================================ */

TAURUS_API int taurus_node_line(TaurusNodeRef node) {
    /* The parser tracks line numbers internally for error reporting
     * but does not store them per-node in the persistent tree.
     * Returning 0 (no source info) is the documented behavior for
     * programmatically-created nodes; for parsed nodes, a future
     * enhancement would thread line numbers through from the parser
     * into the node struct. */
    (void)node;
    return 0;
}

TAURUS_API int taurus_node_compare(TaurusNodeRef a, TaurusNodeRef b) {
    if (a == b) return 0;
    if (!a || !b) return 0;
    /* Document-order comparison requires walking ancestor chains to
     * find the common ancestor and comparing sibling positions. The
     * compact-pointer tree supports this via parent accessors on
     * elements; non-element nodes lack a parent pointer (see
     * taurus_node_parent above). For elements, fall back to pointer
     * identity for now -- a correct implementation is a future
     * enhancement. */
    if (a < b) return -1;
    return 1;
}
