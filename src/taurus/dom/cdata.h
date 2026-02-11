/* lib/src/dom/cdata.h - CDATA node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * CDATA nodes contain <![CDATA[...]]> sections.
 * Content is preserved exactly including special chars like <, >, &.
 */

#ifndef TAURUS_DOM_CDATA_H
#define TAURUS_DOM_CDATA_H

#include "node.h"

/* CDATA node - inherits from TaurusNode */
typedef struct taurus_cdata_node {
    TaurusNode base;                   /* MUST be first */
    char* content;                    /* CDATA content - never escaped */
    void* next_sibling;               /* Next sibling in linked list (mixed content) */
} TaurusCDATANode;

/* CDATA node creation and destruction */
TaurusCDATANode* taurus_cdata_create(const char* content);
void taurus_cdata_free(TaurusCDATANode* cdata);

/* Create CDATA node with bulk allocation (optimized) */
TaurusCDATANode* taurus_cdata_create_fast(
    const char* content,
    size_t content_len,
    struct taurus_memory_pool* pool
);

/* Content access */
const char* taurus_cdata_get_content(TaurusCDATANode* cdata);

/* Casting helpers */
#define TAURUS_NODE_AS_CDATA(node) \
    (TAURUS_NODE_IS_CDATA(node) ? (TaurusCDATANode*)(node) : NULL)

#define TAURUS_CDATA_AS_NODE(cdata) \
    ((TaurusNode*)(cdata))

#endif /* TAURUS_DOM_CDATA_H */