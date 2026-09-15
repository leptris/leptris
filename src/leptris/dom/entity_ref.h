/* src/dom/entity_ref.h - Entity reference node type (#1094)
 *
 * An unexpanded &name; reference. Produced by the parser under
 * LEPTRIS_PARSE_KEEP_ENTITY_REFS or created programmatically via
 * leptris_entity_ref_node_create. Serializes back as "&name;";
 * text-content reads resolve the name against the predefined
 * entities and the DTD entity table.
 */

#ifndef LEPTRIS_DOM_ENTITY_REF_H
#define LEPTRIS_DOM_ENTITY_REF_H

#include "node.h"
#include "compact.h"

struct leptris_memory_pool;

/* Entity reference node - inherits from LeptrisNode. Same edge
 * shape as LeptrisPINode: unscaled int32 compact sibling/parent
 * edges (#450, #168) plus the detached-owner backpointer (#519). */
typedef struct leptris_entity_ref_node {
    LeptrisNode base;                   /* MUST be first */
    char* name;                         /* Entity name ("foo" in &foo;) */
    int32_t next_sibling_off;           /* 0 = NULL (compact int32) */
    int32_t parent_off;                 /* Byte offset to parent element */
    struct leptris_document* owner_doc; /* Owning document when detached */
} LeptrisEntityRefNode;

/* Pool-allocated creation; name is pool-copied. */
LeptrisEntityRefNode* leptris_entity_ref_create(const char* name,
                                                size_t name_len,
                                                struct leptris_memory_pool* pool);

const char* leptris_entity_ref_get_name(LeptrisEntityRefNode* ref);

/* Resolved replacement text for the reference: the predefined
 * entities (&amp; &lt; &gt; &apos; &quot;) first, then the
 * document's DTD entity table. Returns NULL when the name is
 * undeclared. */
const char* leptris_entity_ref_resolve(LeptrisEntityRefNode* ref);

#define LEPTRIS_NODE_AS_ENTITY_REF(node) \
    ((node) && (node)->type == LEPTRIS_NODE_TYPE_ENTITY_REF \
         ? (LeptrisEntityRefNode*)(node) : NULL)

#define LEPTRIS_ENTITY_REF_AS_NODE(r) ((LeptrisNode*)(r))

static inline LeptrisNode* leptris_entity_ref_next_sibling(const LeptrisEntityRefNode* r) {
    return (r)
        ? (LeptrisNode*)leptris_compact_int32_decode_inline((void*)r, r->next_sibling_off, &r->next_sibling_off)
        : NULL;
}

static inline void leptris_entity_ref_set_next_sibling(LeptrisEntityRefNode* r, LeptrisNode* sibling) {
    if (!r) return;
    r->next_sibling_off = leptris_compact_int32_encode_inline(r, sibling, &r->next_sibling_off);
}

static inline LeptrisElement leptris_entity_ref_parent(const LeptrisEntityRefNode* r) {
    return (r)
        ? (LeptrisElement)leptris_compact_int32_decode_inline((void*)r, r->parent_off, &r->parent_off)
        : NULL;
}

static inline void leptris_entity_ref_set_parent(LeptrisEntityRefNode* r, LeptrisElement parent) {
    if (!r) return;
    r->parent_off = leptris_compact_int32_encode_inline(r, parent, &r->parent_off);
}

#endif /* LEPTRIS_DOM_ENTITY_REF_H */
