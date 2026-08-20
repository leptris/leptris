/* dom/root_doc_map.c — Thread-local root-element → document mapping.
 *
 * TODO 155 Phase A: the `document` field was removed from struct
 * taurus_element to fit it in one 64-byte cache line. Non-root
 * elements reach their document by walking parent_off to the root,
 * then looking up the root in this thread-local hash table.
 *
 * TODO 157 (perf): uses a free-list to avoid per-parse malloc/free.
 * After warmup, register and unregister are O(1) with zero heap ops. */
#include "root_doc_map.h"
#include "element.h"
#include "../common/port.h"
#include <stdlib.h>

#define ROOT_DOC_BUCKETS 256

/* Round 20: header.flags bit marking "an entry for this element is
 * (possibly) in the map". flags is otherwise reserved — node types
 * live in base.type. With the bit, register skips the duplicate-
 * check walk for never-registered elements (the mutation hot path:
 * create registers every new element so pre-attach ops can resolve
 * the doc; the walk made sequential appends O(chain) with chains
 * polluted by every element ever created). */
#define ROOTMAP_FLAG 0x80u

static inline int rootmap_marked(TaurusElement e) {
    return (e->header.flags & ROOTMAP_FLAG) != 0;
}

static inline void rootmap_set(TaurusElement e, int on) {
    if (on) e->header.flags |= ROOTMAP_FLAG;
    else e->header.flags &= (uint8_t)~ROOTMAP_FLAG;
}

typedef struct root_doc_entry {
    TaurusElement root;
    struct taurus_document* doc;
    struct root_doc_entry* next;
} RootDocEntry;

static TAURUS_THREAD_LOCAL RootDocEntry* g_root_doc_buckets[ROOT_DOC_BUCKETS];

/* Free-list: recycled entries from unregistered roots. Eliminates
 * malloc/free churn on the parse→free cycle. */
static TAURUS_THREAD_LOCAL RootDocEntry* g_free_list;

static size_t bucket_index(TaurusElement root) {
    uintptr_t v = (uintptr_t)root;
    v ^= v >> 16;
    v ^= v >> 8;
    return (size_t)(v & (ROOT_DOC_BUCKETS - 1));
}

void taurus_root_doc_register(TaurusElement root, struct taurus_document* doc) {
    if (!root || !doc) return;
    size_t idx = bucket_index(root);
    if (rootmap_marked(root)) {
        /* Possibly already present: walk to update. */
        for (RootDocEntry* e = g_root_doc_buckets[idx]; e; e = e->next) {
            if (e->root == root) { e->doc = doc; return; }
        }
    } else {
        /* Never registered: prepend directly, no duplicate walk. */
        RootDocEntry* e = g_free_list;
        if (e) {
            g_free_list = e->next;
        } else {
            e = (RootDocEntry*)malloc(sizeof(*e));
            if (!e) return;
        }
        e->root = root; e->doc = doc;
        e->next = g_root_doc_buckets[idx];
        g_root_doc_buckets[idx] = e;
        rootmap_set(root, 1);
        return;
    }
    /* Pop from free-list, or malloc if empty. */
    RootDocEntry* e = g_free_list;
    if (e) {
        g_free_list = e->next;
    } else {
        e = (RootDocEntry*)malloc(sizeof(*e));
        if (!e) return;
    }
    e->root = root; e->doc = doc;
    e->next = g_root_doc_buckets[idx];
    g_root_doc_buckets[idx] = e;
}

void taurus_root_doc_unregister(TaurusElement root) {
    if (!root) return;
    if (!rootmap_marked(root)) return;  /* never registered: O(1) out */
    size_t idx = bucket_index(root);
    RootDocEntry** pp = &g_root_doc_buckets[idx];
    while (*pp) {
        if ((*pp)->root == root) {
            RootDocEntry* freed = *pp;
            *pp = freed->next;
            /* Push to free-list instead of free(). */
            freed->next = g_free_list;
            g_free_list = freed;
            rootmap_set(root, 0);
            return;
        }
        pp = &(*pp)->next;
    }
}

struct taurus_document* taurus_root_doc_lookup(TaurusElement root) {
    if (!root) return NULL;
    size_t idx = bucket_index(root);
    for (RootDocEntry* e = g_root_doc_buckets[idx]; e; e = e->next) {
        if (e->root == root) return e->doc;
    }
    return NULL;
}

struct taurus_document* taurus_element_get_document(TaurusElement elem) {
    if (!elem) return NULL;
    TaurusElement cur = elem;
    for (int i = 0; i < 1000000; i++) {
        TaurusElement parent = taurus_elem_parent(cur);
        if (!parent) break;
        cur = parent;
    }
    struct taurus_document* d = taurus_root_doc_lookup(cur);
    if (d) return d;
    /* Round 21: unattached mutation elements carry their doc in the
     * name slot backpointer — a stateless fallback that replaced the
     * register-on-create / unregister-on-attach map pair. */
    if (cur->name && taurus_elem_has_namebp(cur)) {
        return taurus_elem_namebp_doc(cur);
    }
    return NULL;
}

TaurusMemoryPool* taurus_element_get_pool(TaurusElement elem) {
    struct taurus_document* d = taurus_element_get_document(elem);
    return d ? d->pool : NULL;
}
