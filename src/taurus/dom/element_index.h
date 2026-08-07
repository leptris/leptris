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

typedef struct taurus_element_index {
    TaurusElement* all_elements; /* Flat array in preorder */
    size_t all_count;

    /* Per-name buckets. Linear scan; the number of distinct names is
     * typically small (<20 for most docs). For very large numbers of
     * distinct names, switch to a hash table. */
    TaurusElementIndexBucket* buckets;
    size_t bucket_count;
    size_t bucket_capacity;
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

#endif /* TAURUS_DOM_ELEMENT_INDEX_H */
