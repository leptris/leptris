/* dom/element_index.c — Per-document element index (TODO 132).
 *
 * Build a flat preorder array of all elements + per-name buckets.
 * The index lives on TaurusDocument and is built lazily on first
 * descendant-axis access.
 *
 * Build walks the tree once via the same parent / first_child /
 * next_sibling links the iterative descendant_walk uses. */
#include "element_index.h"
#include "element.h"
#include <stdlib.h>
#include <string.h>

/* Grow a generic pointer array. */
static int grow_ptr_array(void** arr, size_t* cap, size_t elem_size) {
    size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
    void* grown = realloc(*arr, new_cap * elem_size);
    if (!grown) return -1;
    *arr = grown;
    *cap = new_cap;
    return 0;
}

/* Recursive preorder walk. Appends each element to all_elements and
 * to the appropriate name bucket. */
static void index_walk(TaurusElementIndex* idx, TaurusElement elem) {
    if (!elem) return;

    /* Append to flat array. */
    if (idx->all_count == idx->all_count /* sentinel; capacity tracked separately */) {
        /* noop */
    }
    idx->all_elements[idx->all_count++] = elem;

    /* Append to name bucket. */
    const char* name = taurus_element_get_name(elem);
    if (name) {
        TaurusElementIndexBucket* bucket = NULL;
        for (size_t i = 0; i < idx->bucket_count; i++) {
            if (idx->buckets[i].name_len == strlen(name) &&
                memcmp(idx->buckets[i].name, name, idx->buckets[i].name_len) == 0) {
                bucket = &idx->buckets[i];
                break;
            }
        }
        if (!bucket) {
            if (idx->bucket_count >= idx->bucket_capacity) {
                if (grow_ptr_array((void**)&idx->buckets,
                                    &idx->bucket_capacity,
                                    sizeof(TaurusElementIndexBucket)) != 0) {
                    return;
                }
            }
            bucket = &idx->buckets[idx->bucket_count++];
            bucket->name = strdup(name);
            bucket->name_len = strlen(name);
            bucket->matches = NULL;
            bucket->count = 0;
            bucket->capacity = 0;
        }
        if (bucket->count >= bucket->capacity) {
            if (grow_ptr_array((void**)&bucket->matches,
                                &bucket->capacity,
                                sizeof(TaurusElement)) != 0) {
                return;
            }
        }
        bucket->matches[bucket->count++] = elem;
    }

    /* Recurse into children. */
    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        index_walk(idx, child);
        child = taurus_element_get_next_sibling(child);
    }
}

/* Count elements in the tree (for initial allocation). */
static size_t count_elements(TaurusElement elem) {
    if (!elem) return 0;
    size_t n = 1;
    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        n += count_elements(child);
        child = taurus_element_get_next_sibling(child);
    }
    return n;
}

TaurusElementIndex* taurus_element_index_build(struct taurus_document* doc) {
    if (!doc || !doc->new_dom_root) return NULL;

    TaurusElement root = (TaurusElement)doc->new_dom_root;
    size_t total = count_elements(root);

    TaurusElementIndex* idx = (TaurusElementIndex*)calloc(1, sizeof(*idx));
    if (!idx) return NULL;

    idx->all_elements = (TaurusElement*)malloc(total * sizeof(TaurusElement));
    if (!idx->all_elements) {
        free(idx);
        return NULL;
    }
    idx->all_count = 0;

    index_walk(idx, root);
    return idx;
}

void taurus_element_index_free(TaurusElementIndex* idx) {
    if (!idx) return;
    free(idx->all_elements);
    for (size_t i = 0; i < idx->bucket_count; i++) {
        free(idx->buckets[i].name);
        free(idx->buckets[i].matches);
    }
    free(idx->buckets);
    free(idx);
}

void taurus_element_index_invalidate(struct taurus_document* doc) {
    if (!doc || !doc->element_index) return;
    taurus_element_index_free(doc->element_index);
    doc->element_index = NULL;
}

const TaurusElementIndexBucket* taurus_element_index_lookup(
    const TaurusElementIndex* idx, const char* name) {
    if (!idx || !name) return NULL;
    size_t name_len = strlen(name);
    for (size_t i = 0; i < idx->bucket_count; i++) {
        if (idx->buckets[i].name_len == name_len &&
            memcmp(idx->buckets[i].name, name, name_len) == 0) {
            return &idx->buckets[i];
        }
    }
    return NULL;
}
