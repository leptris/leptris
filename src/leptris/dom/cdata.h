/* lib/src/dom/cdata.h - CDATA node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * CDATA nodes contain <![CDATA[...]]> sections.
 * Content is preserved exactly including special chars like <, >, &.
 */

#ifndef LEPTRIS_DOM_CDATA_H
#define LEPTRIS_DOM_CDATA_H

#include "node.h"
#include "compact.h"  /* compact-pointer helpers (TODO 121, TODO 178) */

/* CDATA node - inherits from LeptrisNode.
 * TODO 179 Phase B: next_sibling is a 2-byte compact pointer (cp16).
 * Issue #168: parent_off mirrors next_sibling_off. */
typedef struct leptris_cdata_node {
    LeptrisNode base;                   /* MUST be first */
    char* content;                    /* CDATA content - never escaped */
    int32_t next_sibling_off;         /* (#450) unscaled int32 sibling edge — cp16's
                                     ±256 KB range cannot hold cross-block sibling
                                     links on large documents. 0 = NULL. */
    int32_t parent_off;               /* Byte offset to parent element (0=NULL) */
    /* Issue #519: owning document for DETACHED nodes. Parentless
     * non-element nodes could not reach their pool (document was
     * resolved via the parent chain) — mutations on freshly created
     * nodes failed with INVALID_ARG until attach. NULL for parsed
     * nodes is fine (parent route resolves first). */
    struct leptris_document* owner_doc;
} LeptrisCDATANode;

/* CDATA node creation.  Pool-allocated with contiguous content
 * storage (TODO 18 + TODO 26: single entry point, no _fast variant). */
LeptrisCDATANode* leptris_cdata_create(const char* content,
                                      size_t content_len,
                                      struct leptris_memory_pool* pool);
void leptris_cdata_free(LeptrisCDATANode* cdata);

/* Content access */
const char* leptris_cdata_get_content(LeptrisCDATANode* cdata);

/* Casting helpers */
#define LEPTRIS_NODE_AS_CDATA(node) \
    (LEPTRIS_NODE_IS_CDATA(node) ? (LeptrisCDATANode*)(node) : NULL)

#define LEPTRIS_CDATA_AS_NODE(cdata) \
    ((LeptrisNode*)(cdata))

/* Compact next_sibling accessors (TODO 179 Phase B — cp16). */
static inline LeptrisNode* leptris_cdata_next_sibling(const LeptrisCDATANode* c) {
    return (c)
        ? (LeptrisNode*)leptris_compact_int32_decode((void*)c, c->next_sibling_off, &c->next_sibling_off)
        : NULL;
}

static inline void leptris_cdata_set_next_sibling(LeptrisCDATANode* c, LeptrisNode* sibling) {
    if (!c) return;
    c->next_sibling_off = leptris_compact_int32_encode(c, sibling, &c->next_sibling_off);
}

/* Compact parent accessors (issue #168). */
static inline LeptrisElement leptris_cdata_parent(const LeptrisCDATANode* c) {
    return (c)
        ? (LeptrisElement)leptris_compact_int32_decode((void*)c, c->parent_off, &c->parent_off)
        : NULL;
}

static inline void leptris_cdata_set_parent(LeptrisCDATANode* c, LeptrisElement parent) {
    if (!c) return;
    c->parent_off = leptris_compact_int32_encode(c, parent, &c->parent_off);
}

#endif /* LEPTRIS_DOM_CDATA_H */