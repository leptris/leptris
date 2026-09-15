/* dom/edges.h — shared tree-edge offset tables + inline accessors.
 *
 * #450: all parent/sibling edges are unscaled int32 byte offsets
 * stored per node type. The parser wires them branchlessly via
 * compile-time offset tables (dp_par_off / dp_ns_off_int32); the DOM
 * paths paid a type-dispatched switch in an out-of-line call
 * (leptris_node_parent) for every parent read — the tail-cache
 * validation in append alone ran it twice per node (S8 profile).
 * One home for the tables, both users. */
#ifndef LEPTRIS_DOM_EDGES_H
#define LEPTRIS_DOM_EDGES_H

#include "element.h"
#include "text.h"
#include "comment.h"
#include "cdata.h"
#include "pi.h"
#include "entity_ref.h"
#include "../common/port.h"
#include <stddef.h>

/* Index: LeptrisNodeKind values 0-4 (element, text, comment, cdata,
 * pi) plus ENTITY_REF (kind 10) mapped to index 5 — the six
 * child-bearing types. */
static const size_t leptris_edge_par_off[6] = {
    offsetof(struct leptris_element, parent_off),
    offsetof(LeptrisTextNode,    parent_off),
    offsetof(LeptrisCommentNode, parent_off),
    offsetof(LeptrisCDATANode,   parent_off),
    offsetof(LeptrisPINode,      parent_off),
    offsetof(LeptrisEntityRefNode, parent_off),
};

static const size_t leptris_edge_sib_off[6] = {
    offsetof(struct leptris_element, next_sibling_off),
    offsetof(LeptrisTextNode,    next_sibling_off),
    offsetof(LeptrisCommentNode, next_sibling_off),
    offsetof(LeptrisCDATANode,   next_sibling_off),
    offsetof(LeptrisPINode,      next_sibling_off),
    offsetof(LeptrisEntityRefNode, next_sibling_off),
};

static LEPTRIS_ALWAYS_INLINE size_t leptris_edge_idx(unsigned t) {
    return t <= 4u ? t : (t == 10u ? 5u : 6u);
}

/* Branchless parent read: one table load + inline decode. NULL for
 * unattached nodes (offset 0) and non-child types. */
static LEPTRIS_ALWAYS_INLINE LeptrisElement leptris_node_parent_inline(
    LeptrisNodeRef node) {
    if (!node) return NULL;
    size_t ti = leptris_edge_idx((unsigned)node->type);
    if (ti >= 6) return NULL;
    int32_t* f = (int32_t*)((char*)node + leptris_edge_par_off[ti]);
    if (*f == 0) return NULL;
    return (LeptrisElement)leptris_compact_int32_decode_inline(
        (void*)node, *f, f);
}

#endif /* LEPTRIS_DOM_EDGES_H */
