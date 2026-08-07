/* dom/element_index.h — Per-document element index for O(1) descendant.
 *
 * Maintained lazily on TaurusDocument. The index has:
 *   - flat array of all elements in document order (preorder traversal)
 *   - per-name buckets pointing into the flat array
 *
 * Queries:
 *   - descendant::* / //* (all elements): return flat array slice.
 *   - descendant::name / //name (named): return name bucket.
 *
 * Build cost: O(N) walk. Amortized across queries — typical workload
 * runs many XPath expressions against the same document, so the
 * per-query cost drops from O(N) to O(K) where K = match count.
 *
 * For 50-element docs, descendant::* goes from ~5 us (walk) to
 * ~0.5 us (slice + nodeset build). Brings taurus to libxml2 parity
 * on the descendant axis.
 *
 * Thread safety: same model as the AST cache. Concurrent first-access
 * is a benign race (worst case is duplicate build, last writer wins).
 *
 * The index references elements owned by the document pool; it does
 * NOT own them. The index is freed when the document is freed.
 */
#ifndef TAURUS_DOM_ELEMENT_INDEX_H
#define TAURUS_DOM_ELEMENT_INDEX_H

#include "../taurus_internal.h"

typedef struct taurus_element_index_bucket {
    char* name;                  /* Owned, NUL-terminated */
    size_t name_len;
    TaurusElement* matches;      /* Array of elements with this name */
    size_t count;
    size_t capacity;
} TaurusElementIndexBucket;

typedef struct taurus_element_index_attr_value {
    char* value;                 /* Owned */
    size_t value_len;
    TaurusElement* matches;      /* Elements with attr name AND this value */
    size_t count;
    size_t capacity;
} TaurusElementIndexAttrValue;

typedef struct taurus_element_index_attr_bucket {
    char* attr_name;             /* Owned, NUL-terminated */
    size_t attr_name_len;
    TaurusElement* matches;      /* Elements with this attr (any value) */
    size_t count;
    size_t capacity;

    /* Per-value breakdown for [@attr='value'] predicates. Linear scan
     * is fine — the number of distinct values per attr is typically
     * small. */
    TaurusElementIndexAttrValue* values;
    size_t value_count;
    size_t value_capacity;
} TaurusElementIndexAttrBucket;

typedef struct taurus_element_index {
    TaurusElement* all_elements; /* Flat array in preorder */
    size_t all_count;

    /* Per-name buckets. Linear scan; the number of distinct names is
     * typically small (<20 for most docs). For very large numbers of
     * distinct names, switch to a hash table. */
    TaurusElementIndexBucket* buckets;
    size_t bucket_count;
    size_t bucket_capacity;

    /* Per-attribute-name buckets (TODO 133). Lets predicate opcodes
     * BC_PRED_ATTR_EXISTS / BC_PRED_ATTR_EQ_STRING skip per-element
     * attribute scanning. */
    TaurusElementIndexAttrBucket* attr_buckets;
    size_t attr_bucket_count;
    size_t attr_bucket_capacity;
} TaurusElementIndex;

/* Build the index for a document. Walks the tree once.
 * Returns NULL on allocation failure. */
TaurusElementIndex* taurus_element_index_build(struct taurus_document* doc);

/* Free an index. Does not free the elements themselves (document owns them). */
void taurus_element_index_free(TaurusElementIndex* idx);

/* Invalidate the cached index on a document. Called by tree-mutating
 * operations (append_child, remove_child, set_name) so the next
 * descendant query rebuilds. If no index is cached, this is a no-op. */
void taurus_element_index_invalidate(struct taurus_document* doc);

/* Look up a bucket by name. Returns NULL if not found. */
const TaurusElementIndexBucket* taurus_element_index_lookup(
    const TaurusElementIndex* idx, const char* name);

/* Look up an attribute-name bucket by attribute name. Returns NULL
 * if no element in the document has an attribute with that name. */
const TaurusElementIndexAttrBucket* taurus_element_index_lookup_attr(
    const TaurusElementIndex* idx, const char* attr_name);

/* Within an attribute bucket, look up elements matching a specific
 * value. Returns NULL if no element has that attr=value combination. */
const TaurusElementIndexAttrValue* taurus_element_index_attr_lookup_value(
    const TaurusElementIndexAttrBucket* bucket, const char* value);

#endif /* TAURUS_DOM_ELEMENT_INDEX_H */
