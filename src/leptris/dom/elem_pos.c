/* lib/src/dom/elem_pos.c — element source-offset journal.
 *
 * #1285 slice 3a: start_tag_end_off/element_end_off (#1124) left the
 * element struct (cold diagnostics-only data was costing hot-path
 * cache lines). Parse lanes record here instead; the single reader
 * (node_public.c position accessor) probes.
 *
 * Lane-18 round 10: append-only JOURNAL, pool-carved. The old
 * open-addressed inline table cost a 384-byte zeroing per document
 * (embedded in the doc struct memset) plus a hash+probe per record
 * plus a full hash LOOKUP per element close. Parse now appends at
 * open, updates by slot at close (O(1), no hashing); lookups scan
 * linearly and promote to a lazily built open-addressed index when a
 * large journal is first probed. The journal and index die with the
 * pool — no free path. */
#include "../leptris_internal.h"
#include "element.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Journal growth: pool-carved, double, memcpy the live prefix. */
static struct leptris_elem_pos_entry* elem_pos_grow(
    struct leptris_document* doc) {
    size_t ncap = doc->elem_pos_cap ? doc->elem_pos_cap * 2 : 32;
    struct leptris_elem_pos_entry* nt =
        (struct leptris_elem_pos_entry*)leptris_pool_alloc(
            doc->pool, ncap * sizeof(*nt));
    if (!nt) return NULL;
    if (doc->elem_pos_count)
        memcpy(nt, doc->elem_pos,
               doc->elem_pos_count * sizeof(*nt));
    doc->elem_pos = nt;
    doc->elem_pos_cap = ncap;
    return nt;
}

int leptris_elem_pos_record(struct leptris_document* doc,
                            struct leptris_element* elem,
                            uint32_t start_tag_end, uint32_t element_end) {
    if (!doc || !elem) return -1;
    /* Append-only (re-records of the same element, if any lane ever
     * does, leave the newest entry last — lookups and the lazy index
     * prefer the latest). */
    if (doc->elem_pos_count == doc->elem_pos_cap) {
        if (!elem_pos_grow(doc)) return -1;   /* cold data: skip on OOM */
    }
    struct leptris_elem_pos_entry* e =
        &doc->elem_pos[doc->elem_pos_count];
    e->elem = elem;
    e->start_tag_end = start_tag_end;
    e->element_end = element_end;
    doc->elem_pos_count++;
    return (int)(doc->elem_pos_count - 1);
}

void leptris_elem_pos_close_at(struct leptris_document* doc, int slot,
                               uint32_t element_end) {
    if (!doc || slot < 0 || (size_t)slot >= doc->elem_pos_count) return;
    /* Close fills ONLY the end offset — the start offset recorded at
     * open survives verbatim (the old close path re-read it through
     * a hash lookup to write it back). */
    doc->elem_pos[slot].element_end = element_end;
}

int leptris_elem_pos_lookup(const struct leptris_document* doc,
                            const struct leptris_element* elem,
                            uint32_t* start_tag_end, uint32_t* element_end) {
    if (!doc || !elem || !doc->elem_pos_count) return 0;
    /* Small journals: linear scan beats any index build. */
    if (doc->elem_pos_count <= 32) {
        for (size_t i = doc->elem_pos_count; i > 0; i--) {
            if (doc->elem_pos[i - 1].elem == elem) {
                if (start_tag_end)
                    *start_tag_end = doc->elem_pos[i - 1].start_tag_end;
                if (element_end)
                    *element_end = doc->elem_pos[i - 1].element_end;
                return 1;
            }
        }
        return 0;
    }
    /* Large journal: lazily build (and keep current) an open-addressed
     * index over the frozen entries. Cold path — built once, then
     * O(1) per probe. */
    struct leptris_document* d = (struct leptris_document*)doc;
    if (d->elem_pos_index_count != d->elem_pos_count) {
        size_t ncap = 32;
        while (ncap < d->elem_pos_count * 2) ncap *= 2;
        struct leptris_elem_pos_entry* nt =
            (struct leptris_elem_pos_entry*)leptris_pool_alloc(
                d->pool, ncap * sizeof(*nt));
        if (!nt) {   /* fall back to the linear scan */
            for (size_t i = 0; i < d->elem_pos_count; i++) {
                if (d->elem_pos[i].elem == elem) {
                    if (start_tag_end)
                        *start_tag_end = d->elem_pos[i].start_tag_end;
                    if (element_end)
                        *element_end = d->elem_pos[i].element_end;
                    return 1;
                }
            }
            return 0;
        }
        memset(nt, 0, ncap * sizeof(*nt));
        for (size_t i = 0; i < d->elem_pos_count; i++) {
            uintptr_t k = (uintptr_t)d->elem_pos[i].elem;
            size_t h = ((size_t)((k * 0x9E3779B97F4A7C15ULL) >> 32))
                       & (ncap - 1);
            while (nt[h].elem) h = (h + 1) & (ncap - 1);
            nt[h] = d->elem_pos[i];
        }
        d->elem_pos_index = nt;
        d->elem_pos_index_cap = ncap;
        d->elem_pos_index_count = d->elem_pos_count;
    }
    uintptr_t k = (uintptr_t)elem;
    size_t h = ((size_t)((k * 0x9E3779B97F4A7C15ULL) >> 32))
               & (doc->elem_pos_index_cap - 1);
    while (doc->elem_pos_index[h].elem) {
        if (doc->elem_pos_index[h].elem == elem) {
            if (start_tag_end)
                *start_tag_end = doc->elem_pos_index[h].start_tag_end;
            if (element_end)
                *element_end = doc->elem_pos_index[h].element_end;
            return 1;
        }
        h = (h + 1) & (doc->elem_pos_index_cap - 1);
    }
    return 0;
}
