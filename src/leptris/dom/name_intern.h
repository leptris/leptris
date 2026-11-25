/* lib/src/dom/name_intern.h - per-document element-name intern table
 * Copyright (c) 2024, Ribose Inc.
 *
 * #2132 phase 2: leptris_element_create interns COLON-FREE names in a
 * per-document open-addressed table (name bytes -> shared mut-block
 * name slot + cached 16-bit element hash + length), with a last-name
 * memo so sequential same-name creates resolve with ONE memcmp.
 *
 * Soundness contract (issue #2132 owner-approved audit):
 * - Colon'd names NEVER intern: #846's leptris_elem_split_qname
 *   mutates name bytes IN PLACE, so a shared slot must never be a
 *   split candidate. Callers gate on the colon probe.
 * - set_name routes through the same lookup; interned bytes are never
 *   mutated after insertion (replacements re-point the element).
 * - Per-document table: no cross-doc sharing; cross-doc adoption
 *   copies fresh names (#1528/#358 family). Released with the
 *   document (leptris_name_intern_doc_release from document_free).
 * - Capped at DOM_INTERN_MAX_DISTINCT distinct names; past the cap
 *   the document permanently falls back to the carve path (bounded
 *   worst case for distinct-heavy creation).
 *
 * The shared slot keeps the round-21 carve layout ([8B doc
 * backpointer][bytes][NUL]) so LEPTRIS_NAMEBP_FLAG resolution and
 * document-free reclamation are unchanged — sharing the name IS
 * sharing the slot pointer.
 */

#ifndef LEPTRIS_DOM_NAME_INTERN_H
#define LEPTRIS_DOM_NAME_INTERN_H

#include <stddef.h>
#include <stdint.h>

struct leptris_document;

#ifdef __cplusplus
extern "C" {
#endif

/* Distinct-name cap: past this the table stops interning and the
 * document permanently carves (the #2132 owner-accepted bound for
 * pathological distinct-heavy creation). */
#define DOM_INTERN_MAX_DISTINCT 8192u

/* One interned element name. */
struct leptris_intern_entry {
    char* name;           /* mut-block slot bytes (NUL-terminated) */
    uint32_t probe_hash;  /* full-width probe key (rehash-free moves) */
    uint16_t hash16;      /* element name_hash for these bytes */
    uint16_t len;         /* byte length (<= 254; long names never intern) */
};

/* Open-addressed table state; owned by the document
 * (doc->name_intern). Table struct AND slot arrays are carved from
 * the document pool (#1124 journal pattern) — they die with the pool
 * and the doc-cycle shape pays zero heap traffic. The ENTRY bytes
 * live in the document's mut-name blocks. */
struct leptris_name_intern {
    struct leptris_intern_entry* slots;
    size_t cap;   /* power of two */
    size_t used;  /* distinct names live */
    /* Last-name memo: sequential same-name creates resolve with one
     * memcmp (bytes + NUL) — no hash, no probe. Points at the last
     * interned slot's bytes. */
    char* memo_name;
    uint16_t memo_hash;
    uint16_t memo_len;
    int overflow; /* cap hit: permanent carve fallback for this doc */
};

/* Lazily build the document's table (NULL only on allocation
 * failure — callers fall back to the carve path). */
struct leptris_name_intern* leptris_name_intern_get(
    struct leptris_document* doc);

/* Look up (or insert) `name`; returns the shared NUL-terminated slot
 * bytes and (on non-NULL) the cached 16-bit element hash through
 * *hash16_out. Returns NULL when the caller must carve instead:
 * table allocation failure, slot-carve failure, or overflow (cap
 * reached — the document then carves permanently). Callers MUST have
 * gated the name colon-free and <= 254 bytes. */
const char* leptris_name_intern_intern(struct leptris_document* doc,
                                       struct leptris_name_intern* ix,
                                       const char* name, size_t name_len,
                                       uint16_t* hash16_out);

/* Lifecycle chokepoint for document_free: pool-carved storage dies
 * with the pool, so this only clears the document's pointer. */
void leptris_name_intern_doc_release(struct leptris_document* doc);

/* Distinct names live in the table (0 when never built). Spec hook:
 * intern-share and cap assertions. */
size_t leptris_name_intern_distinct(struct leptris_document* doc);

/* The round-21 mutation name-slot carve, exposed for the intern
 * insert path (was element_modify.c's static mut_name_carve). Carves
 * [8B doc backpointer][bytes][NUL] from the per-document name block;
 * NULL for names > 254 bytes or when a block cannot be allocated. */
char* leptris_dom_name_slot_carve(struct leptris_document* doc,
                                  const char* name, size_t name_len);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_DOM_NAME_INTERN_H */
