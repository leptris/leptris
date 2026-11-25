/* lib/src/dom/name_intern.c - per-document element-name intern table
 * Copyright (c) 2024, Ribose Inc.
 *
 * #2132 phase 2. See name_intern.h for the soundness contract.
 */

#include "../leptris_internal.h"
#include "name_intern.h"
#include "element.h"

#include <string.h>

/* Initial slot capacity (entries). Small docs pay one pool carve of
 * this; growth doubles under the rehash gate below. */
#define DOM_INTERN_INIT_CAP 64u

/* Slot-array ceiling: 8192 entries sit at 50% load of this — the
 * rehash gate naturally stops here and the distinct cap fires first. */
#define DOM_INTERN_MAX_SLOTS 16384u

/* All file statics carry the dom_intern_ prefix: the amalgamated
 * single-TU build compiles every .c into one translation unit (#1217),
 * so static names must be globally unique. */

/* The table struct + slot arrays are carved from the DOCUMENT POOL
 * (#1124 elem_pos-journal pattern): they die with the pool, and the
 * doc-cycle shape (mutcycle bench) pays ZERO heap traffic — the
 * malloc/calloc/free churn per cycle grew peak RSS ~350 KiB on the
 * macOS allocator (measured 2026-11-25); pool carving is flat vs
 * main. Rehash abandons the old array to the bump (geometric waste
 * <= 2x the final array; only cap-scale documents pay it). */
static uint32_t dom_intern_fnv1a(const char* s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

/* Slot index from the 32-bit hash. Raw FNV truncation maps digit-
 * differing names onto arithmetic progressions (the attr_index
 * round-20 law — exactly the distinct-heavy bench shape), so a
 * murmur-style avalanche step breaks them before the mask cut. */
static size_t dom_intern_slot(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    return h;
}

/* Pool-carved replacement for `ix->slots` at `ncap` entries (zeroed),
 * or NULL on allocation failure (caller keeps the old array). */
static struct leptris_intern_entry* dom_intern_slots_new(
    LeptrisMemoryPool* pool, size_t ncap) {
    struct leptris_intern_entry* ns = (struct leptris_intern_entry*)
        leptris_pool_alloc(pool, ncap * sizeof(*ns));
    if (!ns) return NULL;
    memset(ns, 0, ncap * sizeof(*ns));
    return ns;
}

static void dom_intern_rehash(struct leptris_document* doc,
                              struct leptris_name_intern* ix) {
    size_t ncap = ix->cap * 2;
    struct leptris_intern_entry* ns = dom_intern_slots_new(doc->pool, ncap);
    if (!ns) return; /* stay at old size; the probe bound handles full */
    for (size_t i = 0; i < ix->cap; i++) {
        const struct leptris_intern_entry* e = &ix->slots[i];
        if (!e->name) continue;
        size_t j = dom_intern_slot(e->probe_hash) & (ncap - 1);
        while (ns[j].name) j = (j + 1) & (ncap - 1);
        ns[j] = *e;
    }
    ix->slots = ns; /* the old array is abandoned to the pool bump */
    ix->cap = ncap;
}

struct leptris_name_intern* leptris_name_intern_get(
    struct leptris_document* doc) {
    if (!doc) return NULL;
    if (doc->name_intern) return doc->name_intern;
    if (!doc->pool) return NULL;
    struct leptris_name_intern* ix = (struct leptris_name_intern*)
        leptris_pool_alloc(doc->pool, sizeof(*ix));
    if (!ix) return NULL;
    ix->slots = dom_intern_slots_new(doc->pool, DOM_INTERN_INIT_CAP);
    if (!ix->slots) return NULL;
    ix->cap = DOM_INTERN_INIT_CAP;
    ix->used = 0;
    ix->memo_name = NULL;
    ix->memo_hash = 0;
    ix->memo_len = 0;
    ix->overflow = 0;
    doc->name_intern = ix;
    return ix;
}

/* Pool-carved storage dies with the pool (destroyed by
 * document_free) — this only clears the document's pointer so no
 * caller can reach storage whose pool may already be gone. */
void leptris_name_intern_doc_release(struct leptris_document* doc) {
    if (!doc) return;
    doc->name_intern = NULL;
}

size_t leptris_name_intern_distinct(struct leptris_document* doc) {
    if (!doc || !doc->name_intern) return 0;
    return doc->name_intern->used;
}

const char* leptris_name_intern_intern(struct leptris_document* doc,
                                       struct leptris_name_intern* ix,
                                       const char* name, size_t name_len,
                                       uint16_t* hash16_out) {
    if (!doc || !ix || !name || !hash16_out || name_len == 0 ||
        name_len > 254)
        return NULL;

    /* 1. Last-name memo: ONE memcmp over bytes + NUL decides both
     * length and content — the sequential same-name create (the
     * mutcycle shape) never hashes and never probes. */
    if (ix->memo_name && ix->memo_len == (uint16_t)name_len &&
        memcmp(name, ix->memo_name, name_len + 1) == 0) {
        *hash16_out = ix->memo_hash;
        return ix->memo_name;
    }

    /* 2. Table probe (open addressing, linear step). */
    uint32_t h32 = dom_intern_fnv1a(name, name_len);
    uint16_t h16 = leptris_name_hash_compute(name);
    size_t mask = ix->cap - 1;
    size_t i = dom_intern_slot(h32) & mask;
    size_t probe;
    for (probe = 0; probe < ix->cap; probe++) {
        struct leptris_intern_entry* e = &ix->slots[i];
        if (!e->name) break; /* empty: insert here */
        if (e->probe_hash == h32 && e->len == (uint16_t)name_len &&
            memcmp(e->name, name, name_len) == 0) {
            /* Hit: refresh the memo and share the slot. */
            ix->memo_name = e->name;
            ix->memo_hash = e->hash16;
            ix->memo_len = e->len;
            *hash16_out = e->hash16;
            return e->name;
        }
        i = (i + 1) & mask;
    }
    if (probe == ix->cap) {
        /* Table full (a rehash once failed): bounded fallback. */
        ix->overflow = 1;
        return NULL;
    }

    /* 3. Cap check BEFORE carving: past DOM_INTERN_MAX_DISTINCT this
     * document permanently carves (the owner-accepted bound). */
    if (ix->used >= DOM_INTERN_MAX_DISTINCT) {
        ix->overflow = 1;
        return NULL;
    }

    /* 4. Insert: the shared slot IS the carved mut-block slot — the
     * doc backpointer layout (and document-free reclamation) is the
     * round-21 carve, unchanged. */
    char* slot = leptris_dom_name_slot_carve(doc, name, name_len);
    if (!slot) return NULL;
    struct leptris_intern_entry* e = &ix->slots[i];
    e->name = slot;
    e->probe_hash = h32;
    e->hash16 = h16;
    e->len = (uint16_t)name_len;
    ix->used++;
    ix->memo_name = slot;
    ix->memo_hash = h16;
    ix->memo_len = (uint16_t)name_len;
    if (ix->used * 10 >= ix->cap * 7 && ix->cap < DOM_INTERN_MAX_SLOTS)
        dom_intern_rehash(doc, ix);
    *hash16_out = h16;
    return slot;
}
