/* lib/src/dom/cdata.h - CDATA node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * CDATA nodes contain <![CDATA[...]]> sections.
 * Content is preserved exactly including special chars like <, >, &.
 */

#ifndef TAURUS_DOM_CDATA_H
#define TAURUS_DOM_CDATA_H

#include "node.h"
#include "compact.h"  /* int32 compact-pointer helpers (TODO 121) */

/* CDATA node - inherits from TaurusNode.
 * Phase 2c of TODO 90: next_sibling is a 4-byte offset (0=NULL). */
typedef struct taurus_cdata_node {
    TaurusNode base;                   /* MUST be first */
    char* content;                    /* CDATA content - never escaped */
    int32_t next_sibling_off;         /* Byte offset to next sibling (0=NULL) */
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

/* Compact next_sibling accessors (TODO 90 Phase 2c). */
static inline TaurusNode* taurus_cdata_next_sibling(const TaurusCDATANode* c) {
    return (c)
        ? (TaurusNode*)taurus_compact_int32_decode((void*)c, c->next_sibling_off, &c->next_sibling_off)
        : NULL;
}

static inline void taurus_cdata_set_next_sibling(TaurusCDATANode* c, TaurusNode* sibling) {
    if (!c) return;
    c->next_sibling_off = taurus_compact_int32_encode(c, sibling, &c->next_sibling_off);
}

#endif /* TAURUS_DOM_CDATA_H */