/* doc_block.c — the node-layout fork's document block (slice 1).
 * See doc_block.h for the design contract. */

#include "doc_block.h"
#include <stdlib.h>
#include <string.h>

#define DOC_BLOCK_MIN_CAP ((size_t)4096)
#define DOC_BLOCK_ALIGN   ((size_t)8)

struct LeptrisDocBlock {
    char*  nodes;        /* node region base (8-aligned) */
    size_t nodes_cap;
    size_t nodes_used;
    char*  strs;         /* string region base */
    size_t strs_cap;
    size_t strs_used;
};

static char* region_grow(char* p, size_t* cap, size_t need) {
    size_t want = *cap ? *cap : DOC_BLOCK_MIN_CAP;
    while (want < need) want *= 2;
    char* grown = (char*)realloc(p, want);
    if (grown) *cap = want;
    return grown;
}

LeptrisDocBlock* leptris_doc_block_create(size_t initial_capacity) {
    LeptrisDocBlock* b =
        (LeptrisDocBlock*)calloc(1, sizeof(*b));
    if (!b) return NULL;
    if (initial_capacity && initial_capacity < DOC_BLOCK_MIN_CAP)
        initial_capacity = DOC_BLOCK_MIN_CAP;
    if (initial_capacity) {
        b->nodes = (char*)malloc(initial_capacity);
        b->strs = (char*)malloc(initial_capacity);
        if (!b->nodes || !b->strs) {
            free(b->nodes);
            free(b->strs);
            free(b);
            return NULL;
        }
        b->nodes_cap = b->strs_cap = initial_capacity;
    }
    return b;
}

void leptris_doc_block_free(LeptrisDocBlock* b) {
    if (!b) return;
    free(b->nodes);
    free(b->strs);
    free(b);
}

size_t leptris_doc_block_node_alloc(LeptrisDocBlock* b, size_t sz) {
    if (!b || sz == 0 || sz > (size_t)-1 / 2) return (size_t)-1;
    size_t off = (b->nodes_used + DOC_BLOCK_ALIGN - 1) &
                 ~(DOC_BLOCK_ALIGN - 1);
    size_t need = off + sz;
    if (need > b->nodes_cap) {
        char* grown = region_grow(b->nodes, &b->nodes_cap, need);
        if (!grown) return (size_t)-1;
        b->nodes = grown;
    }
    b->nodes_used = need;
    return off;
}

size_t leptris_doc_block_str(LeptrisDocBlock* b, const char* s,
                             size_t len) {
    if (!b || (!s && len)) return (size_t)-1;
    size_t need = b->strs_used + len + 1;
    if (need > b->strs_cap) {
        char* grown = region_grow(b->strs, &b->strs_cap, need);
        if (!grown) return (size_t)-1;
        b->strs = grown;
    }
    size_t off = b->strs_used;
    if (len) memcpy(b->strs + off, s, len);
    b->strs[off + len] = '\0';
    b->strs_used = need;
    return off;
}

void* leptris_doc_block_node(const LeptrisDocBlock* b, size_t off) {
    return (b && off < b->nodes_used) ? b->nodes + off : NULL;
}

void* leptris_doc_block_nodes_base(const LeptrisDocBlock* b) {
    return b ? b->nodes : NULL;
}

size_t leptris_doc_block_nodes_used(const LeptrisDocBlock* b) {
    return b ? b->nodes_used : 0;
}

const char* leptris_doc_block_str_at(const LeptrisDocBlock* b,
                                     size_t off) {
    return (b && off < b->strs_used) ? b->strs + off : NULL;
}

size_t leptris_doc_block_bytes_used(const LeptrisDocBlock* b) {
    return b ? b->nodes_used + b->strs_used : 0;
}

size_t leptris_doc_block_capacity(const LeptrisDocBlock* b) {
    return b ? b->nodes_cap + b->strs_cap : 0;
}
