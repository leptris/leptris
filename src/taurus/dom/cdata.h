/* lib/src/dom/cdata.h - CDATA node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * CDATA nodes contain <![CDATA[...]]> sections.
 * Content is preserved exactly including special chars like <, >, &.
 */

#ifndef TAURUS_DOM_CDATA_H
#define TAURUS_DOM_CDATA_H

#include "node.h"
#include "compact.h"  /* compact-pointer helpers (TODO 121, TODO 178) */

/* CDATA node - inherits from TaurusNode.
 * TODO 179 Phase B: next_sibling is a 2-byte compact pointer (cp16).
 * Issue #168: parent_off mirrors next_sibling_off. */
typedef struct taurus_cdata_node {
    TaurusNode base;                   /* MUST be first */
    char* content;                    /* CDATA content - never escaped */
    int32_t next_sibling_off;         /* (#450) unscaled int32 sibling edge — cp16's
                                     ±256 KB range cannot hold cross-block sibling
                                     links on large documents. 0 = NULL. */
    int32_t parent_off;               /* Byte offset to parent element (0=NULL) */
} TaurusCDATANode;

/* CDATA node creation.  Pool-allocated with contiguous content
 * storage (TODO 18 + TODO 26: single entry point, no _fast variant). */
TaurusCDATANode* taurus_cdata_create(const char* content,
                                      size_t content_len,
                                      struct taurus_memory_pool* pool);
void taurus_cdata_free(TaurusCDATANode* cdata);

/* Content access */
const char* taurus_cdata_get_content(TaurusCDATANode* cdata);

/* Casting helpers */
#define TAURUS_NODE_AS_CDATA(node) \
    (TAURUS_NODE_IS_CDATA(node) ? (TaurusCDATANode*)(node) : NULL)

#define TAURUS_CDATA_AS_NODE(cdata) \
    ((TaurusNode*)(cdata))

/* Compact next_sibling accessors (TODO 179 Phase B — cp16). */
static inline TaurusNode* taurus_cdata_next_sibling(const TaurusCDATANode* c) {
    return (c)
        ? (TaurusNode*)taurus_compact_int32_decode((void*)c, c->next_sibling_off, &c->next_sibling_off)
        : NULL;
}

static inline void taurus_cdata_set_next_sibling(TaurusCDATANode* c, TaurusNode* sibling) {
    if (!c) return;
    c->next_sibling_off = taurus_compact_int32_encode(c, sibling, &c->next_sibling_off);
}

/* Compact parent accessors (issue #168). */
static inline TaurusElement taurus_cdata_parent(const TaurusCDATANode* c) {
    return (c)
        ? (TaurusElement)taurus_compact_int32_decode((void*)c, c->parent_off, &c->parent_off)
        : NULL;
}

static inline void taurus_cdata_set_parent(TaurusCDATANode* c, TaurusElement parent) {
    if (!c) return;
    c->parent_off = taurus_compact_int32_encode(c, parent, &c->parent_off);
}

#endif /* TAURUS_DOM_CDATA_H */