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

    idx->all_elements[idx->all_count++] = elem;

    /* Name bucket. */
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
                                    sizeof(TaurusElementIndexBucket)) != 0) return;
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
                                &bucket->capacity, sizeof(TaurusElement)) != 0) return;
        }
        bucket->matches[bucket->count++] = elem;
    }

    /* Attribute buckets (TODO 133). */
    struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
    while (attr) {
        TaurusStringView nv = attr->name_view;
        TaurusStringView vv = attr->value_view;
        if (nv.length > 0 && nv.data) {
            TaurusElementIndexAttrBucket* abucket = NULL;
            for (size_t i = 0; i < idx->attr_bucket_count; i++) {
                if (idx->attr_buckets[i].attr_name_len == nv.length &&
                    memcmp(idx->attr_buckets[i].attr_name, nv.data, nv.length) == 0) {
                    abucket = &idx->attr_buckets[i];
                    break;
                }
            }
            if (!abucket) {
                if (idx->attr_bucket_count >= idx->attr_bucket_capacity) {
                    if (grow_ptr_array((void**)&idx->attr_buckets,
                                        &idx->attr_bucket_capacity,
                                        sizeof(TaurusElementIndexAttrBucket)) != 0) {
                        attr = attr->next; continue;
                    }
                }
                abucket = &idx->attr_buckets[idx->attr_bucket_count++];
                abucket->attr_name = (char*)malloc(nv.length + 1);
                if (abucket->attr_name) {
                    memcpy(abucket->attr_name, nv.data, nv.length);
                    abucket->attr_name[nv.length] = '\0';
                }
                abucket->attr_name_len = nv.length;
                abucket->matches = NULL;
                abucket->count = 0;
                abucket->capacity = 0;
                abucket->values = NULL;
                abucket->value_count = 0;
                abucket->value_capacity = 0;
            }
            if (abucket->count >= abucket->capacity) {
                if (grow_ptr_array((void**)&abucket->matches,
                                    &abucket->capacity, sizeof(TaurusElement)) != 0) {
                    attr = attr->next; continue;
                }
            }
            abucket->matches[abucket->count++] = elem;

            if (vv.length > 0 && vv.data) {
                TaurusElementIndexAttrValue* vbucket = NULL;
                for (size_t i = 0; i < abucket->value_count; i++) {
                    if (abucket->values[i].value_len == vv.length &&
                        memcmp(abucket->values[i].value, vv.data, vv.length) == 0) {
                        vbucket = &abucket->values[i];
                        break;
                    }
                }
                if (!vbucket) {
                    if (abucket->value_count >= abucket->value_capacity) {
                        if (grow_ptr_array((void**)&abucket->values,
                                            &abucket->value_capacity,
                                            sizeof(TaurusElementIndexAttrValue)) != 0) {
                            attr = attr->next; continue;
                        }
                    }
                    vbucket = &abucket->values[abucket->value_count++];
                    vbucket->value = (char*)malloc(vv.length + 1);
                    if (vbucket->value) {
                        memcpy(vbucket->value, vv.data, vv.length);
                        vbucket->value[vv.length] = '\0';
                    }
                    vbucket->value_len = vv.length;
                    vbucket->matches = NULL;
                    vbucket->count = 0;
                    vbucket->capacity = 0;
                }
                if (vbucket->count >= vbucket->capacity) {
                    if (grow_ptr_array((void**)&vbucket->matches,
                                        &vbucket->capacity, sizeof(TaurusElement)) != 0) {
                        attr = attr->next; continue;
                    }
                }
                vbucket->matches[vbucket->count++] = elem;
            }
        }
        attr = attr->next;
    }

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
    if (!doc) return NULL;
    /* TODO 139 Phase D: lazy promote. */
    taurus_document_ensure_promoted(doc);
    if (!doc->new_dom_root) return NULL;

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
    for (size_t i = 0; i < idx->attr_bucket_count; i++) {
        free(idx->attr_buckets[i].attr_name);
        free(idx->attr_buckets[i].matches);
        for (size_t j = 0; j < idx->attr_buckets[i].value_count; j++) {
            free(idx->attr_buckets[i].values[j].value);
            free(idx->attr_buckets[i].values[j].matches);
        }
        free(idx->attr_buckets[i].values);
    }
    free(idx->attr_buckets);
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

const TaurusElementIndexAttrBucket* taurus_element_index_lookup_attr(
    const TaurusElementIndex* idx, const char* attr_name) {
    if (!idx || !attr_name) return NULL;
    size_t name_len = strlen(attr_name);
    for (size_t i = 0; i < idx->attr_bucket_count; i++) {
        if (idx->attr_buckets[i].attr_name_len == name_len &&
            memcmp(idx->attr_buckets[i].attr_name, attr_name, name_len) == 0) {
            return &idx->attr_buckets[i];
        }
    }
    return NULL;
}

const TaurusElementIndexAttrValue* taurus_element_index_attr_lookup_value(
    const TaurusElementIndexAttrBucket* bucket, const char* value) {
    if (!bucket || !value) return NULL;
    size_t value_len = strlen(value);
    for (size_t i = 0; i < bucket->value_count; i++) {
        if (bucket->values[i].value_len == value_len &&
            memcmp(bucket->values[i].value, value, value_len) == 0) {
            return &bucket->values[i];
        }
    }
    return NULL;
}
