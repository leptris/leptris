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
#include "root_doc_map.h"
#include <stdio.h>
#include <string.h>

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

    /* Issue #182: previous_sibling must work for any node type, not
     * just elements. taurus_node_parent (added in #168) gives us the
     * parent for any node; we then walk the parent's child chain to
     * find the node immediately preceding this one. O(N) over the
     * number of siblings, which is acceptable for typical docs. */
    TaurusElement parent = taurus_node_parent(node);
    if (!parent) return NULL;

    TaurusNodeRef prev = NULL;
    TaurusNodeRef child = (TaurusNodeRef)taurus_elem_first_child(parent);
    while (child && child != node) {
        prev = child;
        child = (TaurusNodeRef)taurus_node_get_next_sibling(child);
    }
    /* If child == node, we found it; return prev. If child == NULL,
     * node isn't in parent's chain (corrupt tree) -- return NULL. */
    return (child == node) ? prev : NULL;
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
        return taurus_element_get_document((TaurusElement)node);
    }
    TaurusElement parent = taurus_node_parent(node);
    return parent ? taurus_element_get_document(parent) : NULL;
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

    /* Round 20: an element being detached becomes a potential root
     * again — resolve the doc while still attached and re-register
     * after the splice so get_document keeps working on the orphan
     * (attach paths unregister for exactly the mirror reason). */
    struct taurus_document* orphan_doc =
        (node->type == TAURUS_NODE_TYPE_ELEMENT)
            ? taurus_element_get_document((TaurusElement)node) : NULL;

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
        if (orphan_doc) taurus_root_doc_register((TaurusElement)node, orphan_doc);
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
    if (taurus_element_get_document(parent)) {
        taurus_element_index_invalidate(taurus_element_get_document(parent));
    }
    return TAURUS_OK;
}

/* ============================================================================
 * node_line + node_compare (issue #172).
 * ============================================================================ */

/* High bit of base.line marks a RESOLVED line; otherwise the field
 * holds byteOffset+1 into doc->xml_buffer (lazy line tracking — the
 * parse scans carry no '\n' compares). */
#define TAURUS_LINE_RESOLVED 0x80000000u

/* Reach the owning document from any node: elements go through the
 * root map; other node types hop their parent edge first. */
static struct taurus_document* node_document(TaurusNodeRef node) {
    if (!node) return NULL;
    switch (node->type) {
    case TAURUS_NODE_TYPE_ELEMENT:
        return taurus_element_get_document((TaurusElement)node);
    case TAURUS_NODE_TYPE_TEXT:
        return taurus_element_get_document(
            taurus_textnode_parent((const TaurusTextNode*)node));
    case TAURUS_NODE_TYPE_COMMENT:
        return taurus_element_get_document(
            taurus_comment_parent((const TaurusCommentNode*)node));
    case TAURUS_NODE_TYPE_CDATA:
        return taurus_element_get_document(
            taurus_cdata_parent((const TaurusCDATANode*)node));
    case TAURUS_NODE_TYPE_PI:
        return taurus_element_get_document(
            taurus_pi_parent((const TaurusPINode*)node));
    default:
        return NULL;
    }
}

/* Build (once) the per-document table of '\n' byte offsets. The
 * buffer is required to stay unmodified for the document's lifetime
 * (the same contract that keeps StringViews valid), so the table
 * never goes stale. */
static const uint32_t* doc_line_breaks(struct taurus_document* doc,
                                       size_t* count) {
    if (!doc->line_breaks) {
        size_t cap = 256, n_ = 0;
        uint32_t* a = (uint32_t*)malloc(cap * sizeof(uint32_t));
        const char* b = doc->xml_buffer;
        const char* end = b + doc->xml_buffer_len;
        const char* q = b;
        while (q < end) {
            const char* hit = (const char*)memchr(q, '\n',
                                                  (size_t)(end - q));
            if (!hit) break;
            if (n_ == cap) {
                cap *= 2;
                uint32_t* grown = (uint32_t*)realloc(
                    a, cap * sizeof(uint32_t));
                if (!grown) { free(a); return NULL; }
                a = grown;
            }
            a[n_++] = (uint32_t)(size_t)(hit - b);
            q = hit + 1;
        }
        doc->line_breaks = a;
        doc->line_break_count = n_;
    }
    *count = doc->line_break_count;
    return doc->line_breaks;
}

TAURUS_API int taurus_node_line(TaurusNodeRef node) {
    if (!node) return 0;
    uint32_t v = node->line;
    if (v == 0) return 0;                       /* unknown */
    if (v & TAURUS_LINE_RESOLVED) return (int)(v & ~TAURUS_LINE_RESOLVED);
    /* Offset-encoded: resolve against the document buffer. */
    struct taurus_document* doc = node_document(node);
    if (!doc || !doc->xml_buffer) return 0;
    size_t off = (size_t)(v - 1);
    if (off > doc->xml_buffer_len) return 0;    /* defensive */
    size_t n_ = 0;
    const uint32_t* brks = doc_line_breaks(doc, &n_);
    if (!brks) return 0;
    /* line = 1 + number of newlines strictly before the node start */
    size_t lo = 0, hi = n_;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (brks[mid] < off) lo = mid + 1; else hi = mid;
    }
    node->line = TAURUS_LINE_RESOLVED | (uint32_t)(lo + 1);
    return (int)(lo + 1);
}

TAURUS_API void* taurus_node_get_binding_wrapper(TaurusNodeRef node) {
    return node ? node->binding_wrapper : NULL;
}

TAURUS_API void taurus_node_set_binding_wrapper(TaurusNodeRef node, void* wrapper) {
    if (node) node->binding_wrapper = wrapper;
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

/* ----- taurus_node_get_xpath helpers (TODO 148 Phase 3) ----- */

/* Count element siblings with the same name that appear at or before
 * `elem` in document order. Returns the 1-based position and writes
 * the total same-named count to *total (so the caller can decide
 * whether the [N] suffix is needed). */
static uint32_t count_same_name_element_position(TaurusElement elem,
                                                  uint32_t* total) {
    uint32_t pos = 0;
    uint32_t same = 0;
    const char* name = taurus_element_name(elem);
    TaurusElement parent = taurus_element_parent(elem);
    if (!parent) {
        if (total) *total = 1;
        return 1;
    }
    TaurusNodeRef child = taurus_node_first_child(taurus_element_as_node(parent));
    while (child) {
        if (taurus_node_get_type(child) == 0 /* ELEMENT */) {
            TaurusElement e = (TaurusElement)child;
            const char* cn = taurus_element_name(e);
            if (cn && name && strcmp(cn, name) == 0) {
                same++;
                if (e == elem) pos = same;
            }
        }
        child = taurus_node_next_sibling(child);
    }
    if (total) *total = same;
    return pos;
}

TAURUS_API char* taurus_node_get_xpath(TaurusNodeRef node) {
    if (!node) return NULL;

    /* Walk up collecting ancestors; the path is built deepest-first
     * then reversed. The buffer is heap-grown as needed. */
    char* buf = (char*)malloc(64);
    if (!buf) return NULL;
    size_t len = 0;
    size_t cap = 64;
    buf[0] = '\0';

    /* For non-element nodes, render the type-test marker as the
     * DEEPEST segment. We walk up the element chain from the parent
     * and append the marker last (after the reverse concat). */
    TaurusNodeRef cur = node;
    int leaf_type = taurus_node_get_type(cur);
    TaurusNodeRef start_elem;
    if (leaf_type == 0 /* ELEMENT */) {
        start_elem = cur;
    } else {
        TaurusElement parent = taurus_node_parent(cur);
        if (!parent) {
            /* Detached non-element node — emit just the marker. */
            const char* marker = "text()";
            if (leaf_type == 2) marker = "comment()";
            else if (leaf_type == 4) marker = "processing-instruction()";
            size_t mlen = strlen(marker);
            if (mlen + 2 > cap) {
                free(buf);
                buf = (char*)malloc(mlen + 2);
                if (!buf) return NULL;
                cap = mlen + 2;
            }
            buf[len++] = '/';
            memcpy(buf + len, marker, mlen);
            len += mlen;
            buf[len] = '\0';
            return buf;
        }
        start_elem = taurus_element_as_node(parent);
    }

    /* Walk up the element chain, rendering each segment into a
     * temp list, then concat in reverse. */
    char* segs[256];
    size_t seg_lens[256];
    int nseg = 0;
    TaurusNodeRef walk = start_elem;
    while (walk && nseg < 256) {
        TaurusElement e = (TaurusElement)walk;
        uint32_t total = 0;
        uint32_t pos = count_same_name_element_position(e, &total);
        const char* name = taurus_element_name(e);
        if (!name) break;

        char tmp[256];
        int written;
        if (total > 1) {
            written = snprintf(tmp, sizeof(tmp), "/%s[%u]", name, pos);
        } else {
            written = snprintf(tmp, sizeof(tmp), "/%s", name);
        }
        if (written < 0) break;
        size_t sl = (size_t)written;
        char* seg = (char*)malloc(sl + 1);
        if (!seg) {
            for (int i = 0; i < nseg; i++) free(segs[i]);
            free(buf);
            return NULL;
        }
        memcpy(seg, tmp, sl + 1);
        segs[nseg] = seg;
        seg_lens[nseg] = sl;
        nseg++;

        TaurusElement parent = taurus_element_parent(e);
        if (!parent) break;
        walk = taurus_element_as_node(parent);
    }

    /* Concat in reverse order (root first). */
    for (int i = nseg - 1; i >= 0; i--) {
        if (len + seg_lens[i] + 1 > cap) {
            while (cap < len + seg_lens[i] + 1) cap *= 2;
            char* new_buf = (char*)realloc(buf, cap);
            if (!new_buf) {
                for (int j = 0; j < nseg; j++) free(segs[j]);
                free(buf);
                return NULL;
            }
            buf = new_buf;
        }
        memcpy(buf + len, segs[i], seg_lens[i]);
        len += seg_lens[i];
        buf[len] = '\0';
        free(segs[i]);
    }

    /* Append the leaf marker for non-element nodes. */
    if (leaf_type != 0) {
        const char* marker = "text()";
        if (leaf_type == 2) marker = "comment()";
        else if (leaf_type == 4) marker = "processing-instruction()";
        size_t mlen = strlen(marker);
        if (len + mlen + 2 > cap) {
            while (cap < len + mlen + 2) cap *= 2;
            char* new_buf = (char*)realloc(buf, cap);
            if (!new_buf) return buf;  /* best-effort: return what we have */
            buf = new_buf;
        }
        buf[len++] = '/';
        memcpy(buf + len, marker, mlen);
        len += mlen;
        buf[len] = '\0';
    }

    return buf;
}

TAURUS_API int taurus_node_traverse(TaurusNodeRef root,
                                     TaurusTraverseOrder order,
                                     int (*callback)(TaurusNodeRef node,
                                                     void* user_data),
                                     void* user_data) {
    if (!root || !callback) return -1;
    if (order != TAURUS_TRAVERSE_PRE_ORDER &&
        order != TAURUS_TRAVERSE_POST_ORDER) {
        return -1;
    }

    /* Iterative DFS with an explicit 256-deep stack (matches
     * taurus_node_freeze). Each frame carries a "done" flag:
     *   false → still need to descend into first child
     *   true  → children exhausted, ready to visit (post-order) or pop
     * Stack depth is bounded by tree depth, not branching factor —
     * siblings are visited via next-sibling on pop, not pushed eagerly.
     * This keeps a 4KB stack frame regardless of fan-out. */
    typedef struct { TaurusNode* node; int done; } TraverseFrame;
    TraverseFrame stack[256];
    int depth = 0;
    stack[depth].node = (TaurusNode*)root;
    stack[depth].done = 0;
    depth++;

    int count = 0;
    while (depth > 0) {
        TraverseFrame* f = &stack[depth - 1];
        if (!f->done) {
            if (order == TAURUS_TRAVERSE_PRE_ORDER) {
                if (callback((TaurusNodeRef)f->node, user_data) != 0) {
                    return count;
                }
                count++;
            }
            f->done = 1;
            TaurusNode* child = taurus_node_first_child_internal(f->node);
            if (child && depth < 256) {
                stack[depth].node = child;
                stack[depth].done = 0;
                depth++;
            }
        } else {
            if (order == TAURUS_TRAVERSE_POST_ORDER) {
                if (callback((TaurusNodeRef)f->node, user_data) != 0) {
                    return count;
                }
                count++;
            }
            TaurusNode* sib = taurus_node_get_next_sibling(f->node);
            depth--;
            if (sib && depth < 256) {
                stack[depth].node = sib;
                stack[depth].done = 0;
                depth++;
            }
        }
    }

    return count;
}
