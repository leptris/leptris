/* lib/src/dom/pi.h - Processing Instruction node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * PI nodes contain <?target data?> processing instructions.
 */

#ifndef TAURUS_DOM_PI_H
#define TAURUS_DOM_PI_H

#include "node.h"
#include "compact.h"  /* compact-pointer helpers (TODO 121, TODO 178) */

/* Forward declaration */
struct taurus_memory_pool;

/* Processing Instruction node - inherits from TaurusNode.
 * TODO 179 Phase B: next_sibling is a 2-byte compact pointer (cp16).
 * Issue #168: parent_off mirrors next_sibling_off. */
typedef struct taurus_pi_node {
    TaurusNode base;                   /* MUST be first */
    char* target;                      /* PI target (e.g., "xml-stylesheet") */
    char* data;                        /* PI data/content */
    int32_t next_sibling_off;          /* (#450) unscaled int32 sibling edge — cp16's
                                      ±256 KB range cannot hold cross-block sibling
                                      links on large documents. 0 = NULL. */
    int32_t parent_off;                /* Byte offset to parent element (0=NULL) */
} TaurusPINode;

/* PI node creation.  Pool-allocated with contiguous target/data
 * storage (TODO 18 + TODO 26: single entry point, no _fast variant). */
TaurusPINode* taurus_pi_create(const char* target,
                                size_t target_len,
                                const char* data,
                                size_t data_len,
                                struct taurus_memory_pool* pool);

void taurus_pi_free(TaurusPINode* pi);

/* Content access */
const char* taurus_pi_get_target(TaurusPINode* pi);
const char* taurus_pi_get_data(TaurusPINode* pi);

/* Casting helpers */
#define TAURUS_NODE_AS_PI(node) \
    (TAURUS_NODE_IS_PI(node) ? (TaurusPINode*)(node) : NULL)

#define TAURUS_PI_AS_NODE(pi) \
    ((TaurusNode*)(pi))

/* Compact next_sibling accessors (TODO 179 Phase B — cp16). */
static inline TaurusNode* taurus_pi_next_sibling(const TaurusPINode* p) {
    return (p)
        ? (TaurusNode*)taurus_compact_int32_decode((void*)p, p->next_sibling_off, &p->next_sibling_off)
        : NULL;
}

static inline void taurus_pi_set_next_sibling(TaurusPINode* p, TaurusNode* sibling) {
    if (!p) return;
    p->next_sibling_off = taurus_compact_int32_encode(p, sibling, &p->next_sibling_off);
}

/* Compact parent accessors (issue #168). */
static inline TaurusElement taurus_pi_parent(const TaurusPINode* p) {
    return (p)
        ? (TaurusElement)taurus_compact_int32_decode((void*)p, p->parent_off, &p->parent_off)
        : NULL;
}

static inline void taurus_pi_set_parent(TaurusPINode* p, TaurusElement parent) {
    if (!p) return;
    p->parent_off = taurus_compact_int32_encode(p, parent, &p->parent_off);
}

#endif /* TAURUS_DOM_PI_H */