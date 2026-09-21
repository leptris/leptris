/* dom/root_doc_map.c — Thread-local root-element → document mapping.
 *
 * TODO 155 Phase A: the `document` field was removed from struct
 * leptris_element to fit it in one 64-byte cache line. Non-root
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

static inline int rootmap_marked(LeptrisElement e) {
    return (e->header.flags & ROOTMAP_FLAG) != 0;
}

static inline void rootmap_set(LeptrisElement e, int on) {
    if (on) e->header.flags |= ROOTMAP_FLAG;
    else e->header.flags &= (uint8_t)(~ROOTMAP_FLAG & 0xFFu);
}

typedef struct root_doc_entry {
    LeptrisElement root;
    struct leptris_document* doc;
    struct root_doc_entry* next;       /* bucket chain */
    struct root_doc_entry* doc_next;   /* owning doc's chain */
} RootDocEntry;

static LEPTRIS_THREAD_LOCAL RootDocEntry* g_root_doc_buckets[ROOT_DOC_BUCKETS];

/* Doc-entry chain helpers: every register pushes the entry onto its
 * document's list (doc->map_entries), so unregister_doc walks exactly
 * this doc's entries instead of all 256 TLS buckets (the #1038 sweep
 * was ~42% of small-document parse+free). Entries are map-owned
 * storage — the chain never touches element headers, preserving the
 * #1038 adopted-pool rule. */
static void doc_chain_push(struct leptris_document* doc, RootDocEntry* e) {
    e->doc_next = doc->map_entries;
    doc->map_entries = e;
}

static void doc_chain_unlink(struct leptris_document* doc, RootDocEntry* e) {
    RootDocEntry** pp = (RootDocEntry**)&doc->map_entries;
    while (*pp) {
        if (*pp == e) { *pp = e->doc_next; return; }
        pp = &(*pp)->doc_next;
    }
}

/* Free-list: recycled entries from unregistered roots. Eliminates
 * malloc/free churn on the parse→free cycle. */
static LEPTRIS_THREAD_LOCAL RootDocEntry* g_free_list;

/* Entry chunks (lane 18 P0): one TLS bump chunk serves fresh
 * entries so a create-heavy document pays no malloc per element —
 * the free-list only refills after unregister. Entries are still
 * exactly the same map nodes with the same lifetime; only the
 * allocation source changed. */
#define ROOTMAP_CHUNK 128
typedef struct root_doc_chunk {
    struct root_doc_chunk* next;
    RootDocEntry entries[ROOTMAP_CHUNK];
} RootDocChunk;
static LEPTRIS_THREAD_LOCAL RootDocChunk* g_entry_chunks;
static LEPTRIS_THREAD_LOCAL RootDocEntry* g_entry_cursor;
static LEPTRIS_THREAD_LOCAL RootDocEntry* g_entry_end;

static RootDocEntry* entry_take(void) {
    if (g_free_list) {
        RootDocEntry* e = g_free_list;
        g_free_list = e->next;
        return e;
    }
    if (g_entry_cursor < g_entry_end) return g_entry_cursor++;
    RootDocChunk* c = (RootDocChunk*)malloc(sizeof(*c));
    if (!c) return NULL;
    c->next = g_entry_chunks;
    g_entry_chunks = c;
    g_entry_end = c->entries + ROOTMAP_CHUNK;
    RootDocEntry* e = c->entries;
    g_entry_cursor = e + 1;
    return e;
}

static size_t bucket_index(LeptrisElement root) {
    uintptr_t v = (uintptr_t)root;
    v ^= v >> 16;
    v ^= v >> 8;
    return (size_t)(v & (ROOT_DOC_BUCKETS - 1));
}

/* #682 2x lever (sound): last-hit (root, doc) memo. The climb to
 * the root is unavoidable without a per-element field, but the TLS
 * bucket-array walk is not — consecutive queries in a transform
 * hit the same root, so one pointer compare replaces the hash
 * bucket chain. One TLS access total (the memo pair), and the
 * 1M-loop sentinel becomes a plain NULL check. */
static LEPTRIS_THREAD_LOCAL LeptrisElement g_memo_root;
static LEPTRIS_THREAD_LOCAL struct leptris_document* g_memo_doc;

/* #1242 strike-12: the memo was keyed on the root ADDRESS alone.
 * The iterparse yield path primes it with a subtree root the
 * consumer frees; the allocator then recycles that address —
 * frequently for the NEXT parsed document's root — and the stale
 * (address, old-doc) pair resolved the new root to the OLD
 * document. Both observed symptoms come from that one resolution:
 * all-NULL attribute reads (wrong-doc bail upstream of the
 * ATTR_MISS dump) and set_root's "element belongs to a different
 * document" false positive on a same-document element.
 *
 * The fix is a generation counter: every map mutation
 * (register / unregister / doc sweep / explicit invalidate) bumps
 * it, and a memo hit requires the prime-time generation to match.
 * The transform steady state the #682 lever targets performs
 * lookups only, so the memo keeps hitting there; any churn —
 * precisely the strike interleaving — kills it deterministically
 * instead of by allocator luck. */
static LEPTRIS_THREAD_LOCAL uint64_t g_rootmap_generation;
static LEPTRIS_THREAD_LOCAL uint64_t g_memo_generation;

static void rootmap_generation_bump(void) { g_rootmap_generation++; }

void leptris_root_doc_register(LeptrisElement root, struct leptris_document* doc) {
    if (!root || !doc) return;
    rootmap_generation_bump();
    size_t idx = bucket_index(root);
    if (rootmap_marked(root)) {
        /* Possibly already present: walk to update. */
        for (RootDocEntry* e = g_root_doc_buckets[idx]; e; e = e->next) {
            if (e->root == root) {
                if (e->doc != doc) {
                    /* Re-registered under a new document: the entry
                     * must move to the new doc's chain or the old
                     * doc's sweep would recycle a live entry. */
                    doc_chain_unlink(e->doc, e);
                    e->doc = doc;
                    doc_chain_push(doc, e);
                }
                return;
            }
        }
    } else {
        /* Never registered: prepend directly, no duplicate walk. */
        RootDocEntry* e = entry_take();
        if (!e) return;
        e->root = root; e->doc = doc;
        e->next = g_root_doc_buckets[idx];
        g_root_doc_buckets[idx] = e;
        doc_chain_push(doc, e);
        rootmap_set(root, 1);
        return;
    }
    /* Pop from free-list, or the bump chunk. */
    RootDocEntry* e = entry_take();
    if (!e) return;
    e->root = root; e->doc = doc;
    e->next = g_root_doc_buckets[idx];
    g_root_doc_buckets[idx] = e;
    doc_chain_push(doc, e);
}

/* TODO.concurrency/08: TLS free-list entries outlive their thread
 * (no portable C99 TLS destructor). leptris_thread_cleanup() drains
 * them from each worker thread before exit. */
void leptris_root_doc_drain_thread_caches(void) {
    while (g_free_list) {
        RootDocEntry* next = g_free_list->next;
        g_free_list = next; /* chunk-owned; freed with the chunks */
    }
    while (g_entry_chunks) {
        RootDocChunk* next = g_entry_chunks->next;
        free(g_entry_chunks);
        g_entry_chunks = next;
    }
    g_entry_cursor = NULL;
    g_entry_end = NULL;
}

void leptris_root_doc_unregister(LeptrisElement root) {
    if (!root) return;
    if (!rootmap_marked(root)) return;  /* never registered: O(1) out */
    /* #1242: any map mutation kills the memo (generation bump).
     * The address-match clear below stays as the immediate drop;
     * the generation covers the prime-then-churn interleaving the
     * address match cannot see. */
    rootmap_generation_bump();
    if (g_memo_root == root) {
        g_memo_root = NULL;
        g_memo_doc = NULL;
    }
    size_t idx = bucket_index(root);
    RootDocEntry** pp = &g_root_doc_buckets[idx];
    while (*pp) {
        if ((*pp)->root == root) {
            RootDocEntry* freed = *pp;
            *pp = freed->next;
            /* Leave the owning doc's chain too — a stale doc_next
             * here would alias a free-list-recycled entry. */
            doc_chain_unlink(freed->doc, freed);
            /* Push to free-list instead of free(). */
            freed->next = g_free_list;
            g_free_list = freed;
            rootmap_set(root, 0);
            return;
        }
        pp = &(*pp)->next;
    }
}

/* #1038: root-doc map entries must die with their document. The
 * pool-fallback create paths register DETACHED elements (long
 * names, carve failure) that never become new_dom_root, and the
 * adopted-child free path nulls new_dom_root before recursing —
 * pre-fix those entries outlived the doc, and a malloc-recycled
 * element address later resolved the FREED doc through the stale
 * entry: roaming heap corruption in downstream binding suites
 * (~5% of runs, v1.9.151-155).
 *
 * The doc-entry chain (doc->map_entries) makes the sweep O(this
 * doc's entries) instead of O(all 256 TLS buckets) — the bucket
 * sweep was ~42% of small-document parse+free cost, which had
 * pushed the CI parse-ratio guard to its margin. Bucket unlinking
 * uses only the entry ADDRESS and its root POINTER VALUE (bucket
 * hashing): element storage is never dereferenced or written,
 * preserving the adopted-pool rule above. */
size_t leptris_root_doc_unregister_doc(struct leptris_document* doc) {
    if (!doc) return 0;
    size_t removed = 0;
    /* #1242: doc sweep mutates the map — kill the memo. */
    rootmap_generation_bump();
    RootDocEntry* e = (RootDocEntry*)doc->map_entries;
    doc->map_entries = NULL;
    while (e) {
        RootDocEntry* next = e->doc_next;
        /* #1242: a swept root address must not survive in the memo
         * even when the memo's doc differs (a previously poisoned
         * pair). */
        if (g_memo_root == e->root) {
            g_memo_root = NULL;
            g_memo_doc = NULL;
        }
        size_t idx = bucket_index(e->root);
        RootDocEntry** pp = &g_root_doc_buckets[idx];
        while (*pp) {
            if (*pp == e) {
                *pp = e->next;
                break;
            }
            pp = &(*pp)->next;
        }
        e->next = g_free_list;
        g_free_list = e;
        removed++;
        e = next;
    }
    return removed;
}

struct leptris_document* leptris_root_doc_lookup(LeptrisElement root) {
    if (!root) return NULL;
    size_t idx = bucket_index(root);
    for (RootDocEntry* e = g_root_doc_buckets[idx]; e; e = e->next) {
        if (e->root == root) return e->doc;
    }
    return NULL;
}

/* #682 2x lever: an ambient document hint set by the transform
 * driver. Result-tree elements walk up to a root created in THIS
 * document — when the hint matches, the root climb + TLS hash map
 * are skipped entirely (get_document was 3.5% of the light bench).
 * Hint is thread-local-scoped by construction (one transform per
 * thread), validated by the root-walk fallback when it misses. */
/* #904: prime the TLS last-root memo from a driver that already
 * knows the (root, doc) pair — the iterparse yield path hands out
 * a subtree that is released before the next, so the memo misses
 * on every cold read and each pays the climb + bucket walk. The
 * caller must pass a REGISTERED root (register-on-create contract
 * guarantees created subtree roots are); free invalidates. */
void leptris_root_doc_memo_prime(LeptrisElement root,
                                 struct leptris_document* doc) {
    if (!root || !doc) return;
    g_memo_root = root;
    g_memo_doc = doc;
    g_memo_generation = g_rootmap_generation;
}

void leptris_root_doc_memo_invalidate(const struct leptris_document* doc) {
    if (g_memo_doc == doc) {
        rootmap_generation_bump();
        g_memo_root = NULL;
        g_memo_doc = NULL;
    }
}

/* Test hook (#1242): the memo root the engine would TRUST — NULL
 * when the memo is generation-stale (primed before a map
 * mutation), even if the pair fields still hold values. */
LeptrisElement leptris_root_doc_memo_root_for_tests(void) {
    if (g_memo_generation != g_rootmap_generation) return NULL;
    return g_memo_root;
}

struct leptris_document* leptris_element_get_document(LeptrisElement elem) {
    if (!elem) return NULL;
    LeptrisElement cur = elem;
    /* lane18 S2: hoist the TLS memo read (tlv_get_addr was the
     * hottest site in the create+append profile). Bisect note: the
     * fused name pass is reverted — this push isolates the TLS
     * hoist against the macos small-doc parse-ratio guard. */
    LeptrisElement memo_root = g_memo_root;
    /* #1242: the memo hit requires the prime-time generation to
     * match — a map mutation since the prime means the primed
     * root may be freed and its address recycled, so the pair is
     * untrustworthy regardless of the address match. */
    uint64_t memo_generation = g_memo_generation;
    for (;;) {
        /* #1189: a namebp-carrying element is UNATTACHED by
         * definition - its document lives statelessly in the name
         * slot and must never resolve through this recyclable-
         * address memo. A freed root's address recycled by the
         * allocator made the memo hit with a stale document
         * before the authoritative namebp was consulted. */
        if (cur == memo_root && memo_generation == g_rootmap_generation &&
            !leptris_elem_has_namebp(cur))
            return g_memo_doc;
        LeptrisElement parent = leptris_elem_parent(cur);
        if (!parent) break;
        cur = parent;
    }
    for (int i = 0; i < 1000000; i++) {
        LeptrisElement parent = leptris_elem_parent(cur);
        if (!parent) break;
        cur = parent;
    }
    struct leptris_document* d = leptris_root_doc_lookup(cur);
    if (d) {
        g_memo_root = cur;
        g_memo_doc = d;
        g_memo_generation = g_rootmap_generation;
        return d;
    }
    /* Round 21: unattached mutation elements carry their doc in the
     * name slot backpointer — a stateless fallback that replaced the
     * register-on-create / unregister-on-attach map pair. */
    if (cur->name && leptris_elem_has_namebp(cur)) {
        return leptris_elem_namebp_doc(cur);
    }
    return NULL;
}

LeptrisMemoryPool* leptris_element_get_pool(LeptrisElement elem) {
    struct leptris_document* d = leptris_element_get_document(elem);
    return d ? d->pool : NULL;
}
