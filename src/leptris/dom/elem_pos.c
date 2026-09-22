/* lib/src/dom/elem_pos.c — element source-offset side table.
 *
 * #1285 slice 3a: start_tag_end_off/element_end_off (#1124) left the
 * element struct (cold diagnostics-only data was costing hot-path
 * cache lines). Parse lanes record here instead; the single reader
 * (node_public.c position accessor) probes. Open addressing,
 * Fibonacci hash, insert-or-update, power-of-two capacity, freed
 * with the document. */
#include "../leptris_internal.h"
#include "element.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

struct leptris_elem_pos_entry {
    struct leptris_element* elem;
    uint32_t start_tag_end;
    uint32_t element_end;
};

static size_t elem_pos_hash(const struct leptris_element* e, size_t cap) {
    uintptr_t k = (uintptr_t)e;
    return ((size_t)((k * 0x9E3779B97F4A7C15ULL) >> 32)) & (cap - 1);
}

static void elem_pos_insert_raw(struct leptris_elem_pos_entry* tab,
                                size_t cap, struct leptris_element* elem,
                                uint32_t ste, uint32_t ee) {
    size_t i = elem_pos_hash(elem, cap);
    while (tab[i].elem && tab[i].elem != elem)
        i = (i + 1) & (cap - 1);
    tab[i].elem = elem;
    tab[i].start_tag_end = ste;
    tab[i].element_end = ee;
}

static int elem_pos_grow(struct leptris_document* doc) {
    size_t ncap = doc->elem_pos_cap ? doc->elem_pos_cap * 2 : 64;
    struct leptris_elem_pos_entry* nt =
        (struct leptris_elem_pos_entry*)calloc(ncap, sizeof(*nt));
    if (!nt) return -1;
    for (size_t i = 0; i < doc->elem_pos_cap; i++) {
        struct leptris_elem_pos_entry* e = &doc->elem_pos[i];
        if (e->elem)
            elem_pos_insert_raw(nt, ncap, e->elem, e->start_tag_end,
                                e->element_end);
    }
    free(doc->elem_pos);
    doc->elem_pos = nt;
    doc->elem_pos_cap = ncap;
    return 0;
}

void leptris_elem_pos_record(struct leptris_document* doc,
                             struct leptris_element* elem,
                             uint32_t start_tag_end, uint32_t element_end) {
    if (!doc || !elem) return;
    if (doc->elem_pos_count * 2 + 2 >= doc->elem_pos_cap) {
        if (elem_pos_grow(doc) != 0) return;   /* cold data: skip on OOM */
    }
    struct leptris_elem_pos_entry* tab = doc->elem_pos;
    size_t cap = doc->elem_pos_cap;
    size_t i = elem_pos_hash(elem, cap);
    while (tab[i].elem && tab[i].elem != elem)
        i = (i + 1) & (cap - 1);
    if (!tab[i].elem) {
        tab[i].elem = elem;
        doc->elem_pos_count++;
    }
    tab[i].start_tag_end = start_tag_end;
    tab[i].element_end = element_end;
}

int leptris_elem_pos_lookup(const struct leptris_document* doc,
                            const struct leptris_element* elem,
                            uint32_t* start_tag_end, uint32_t* element_end) {
    if (!doc || !elem || !doc->elem_pos_cap) return 0;
    size_t i = elem_pos_hash(elem, doc->elem_pos_cap);
    while (doc->elem_pos[i].elem) {
        if (doc->elem_pos[i].elem == elem) {
            if (start_tag_end) *start_tag_end = doc->elem_pos[i].start_tag_end;
            if (element_end) *element_end = doc->elem_pos[i].element_end;
            return 1;
        }
        i = (i + 1) & (doc->elem_pos_cap - 1);
    }
    return 0;
}
