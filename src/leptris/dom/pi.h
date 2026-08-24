/* lib/src/dom/pi.h - Processing Instruction node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * PI nodes contain <?target data?> processing instructions.
 */

#ifndef LEPTRIS_DOM_PI_H
#define LEPTRIS_DOM_PI_H

#include "node.h"
#include "compact.h"  /* compact-pointer helpers (TODO 121, TODO 178) */

/* Forward declaration */
struct leptris_memory_pool;

/* Processing Instruction node - inherits from LeptrisNode.
 * TODO 179 Phase B: next_sibling is a 2-byte compact pointer (cp16).
 * Issue #168: parent_off mirrors next_sibling_off. */
typedef struct leptris_pi_node {
    LeptrisNode base;                   /* MUST be first */
    char* target;                      /* PI target (e.g., "xml-stylesheet") */
    char* data;                        /* PI data/content */
    int32_t next_sibling_off;          /* (#450) unscaled int32 sibling edge — cp16's
                                      ±256 KB range cannot hold cross-block sibling
                                      links on large documents. 0 = NULL. */
    int32_t parent_off;                /* Byte offset to parent element (0=NULL) */
    /* Issue #519: owning document for DETACHED nodes. Parentless
     * non-element nodes could not reach their pool (document was
     * resolved via the parent chain) — mutations on freshly created
     * nodes failed with INVALID_ARG until attach. NULL for parsed
     * nodes is fine (parent route resolves first). */
    struct leptris_document* owner_doc;
} LeptrisPINode;

/* PI node creation.  Pool-allocated with contiguous target/data
 * storage (TODO 18 + TODO 26: single entry point, no _fast variant). */
LeptrisPINode* leptris_pi_create(const char* target,
                                size_t target_len,
                                const char* data,
                                size_t data_len,
                                struct leptris_memory_pool* pool);

void leptris_pi_free(LeptrisPINode* pi);

/* Content access */
const char* leptris_pi_get_target(LeptrisPINode* pi);
const char* leptris_pi_get_data(LeptrisPINode* pi);

/* Casting helpers */
#define LEPTRIS_NODE_AS_PI(node) \
    (LEPTRIS_NODE_IS_PI(node) ? (LeptrisPINode*)(node) : NULL)

#define LEPTRIS_PI_AS_NODE(pi) \
    ((LeptrisNode*)(pi))

/* Compact next_sibling accessors (TODO 179 Phase B — cp16). */
static inline LeptrisNode* leptris_pi_next_sibling(const LeptrisPINode* p) {
    return (p)
        ? (LeptrisNode*)leptris_compact_int32_decode((void*)p, p->next_sibling_off, &p->next_sibling_off)
        : NULL;
}

static inline void leptris_pi_set_next_sibling(LeptrisPINode* p, LeptrisNode* sibling) {
    if (!p) return;
    p->next_sibling_off = leptris_compact_int32_encode(p, sibling, &p->next_sibling_off);
}

/* Compact parent accessors (issue #168). */
static inline LeptrisElement leptris_pi_parent(const LeptrisPINode* p) {
    return (p)
        ? (LeptrisElement)leptris_compact_int32_decode((void*)p, p->parent_off, &p->parent_off)
        : NULL;
}

static inline void leptris_pi_set_parent(LeptrisPINode* p, LeptrisElement parent) {
    if (!p) return;
    p->parent_off = leptris_compact_int32_encode(p, parent, &p->parent_off);
}

#endif /* LEPTRIS_DOM_PI_H */