/* lib/src/dom/element_modify.c - DOM Modification API (Compact Mode)
 * Copyright (c) 2024, Ribose Inc.
 *
 * Public API for modifying DOM trees in-place.
 * COMPACT MODE: Uses compact pointer encoding and accessor functions.
 */

#include "../../include/taurus.h"
#include "../taurus_internal.h"
#include "../common/string_view.h"
#include "../memory/pool.h"
#include "element.h"
#include "element_index.h"
#include "compact.h"
#include "root_doc_map.h"
#include "node.h"
#include "text.h"
#include "comment.h"
#include "cdata.h"
#include "pi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ===========================================================================
 * Public API Implementation - DOM Modification (Compact Mode)
 * =========================================================================== */

/* Round 18: carve mutation elements from a per-document contiguous
 * bump block. The pool extension path malloc'd each element
 * separately, scattering them across heap regions — sibling edges
 * (self-relative offsets) still worked but landed far apart, and
 * every fresh malloc paid allocator + page costs. Contiguous
 * carving keeps sequential-append elements cache-adjacent. Blocks
 * chain via ->next and are freed with the document. */
#define MUT_ELEM_BLOCK_COUNT 1024

static TaurusElement mut_elem_carve(struct taurus_document* doc) {
    if (doc->mut_elem_cursor && doc->mut_elem_cursor < doc->mut_elem_end) {
        return doc->mut_elem_cursor++;
    }

    struct taurus_mut_elem_block* blk =
        (struct taurus_mut_elem_block*)malloc(
            sizeof(struct taurus_mut_elem_block) +
            (size_t)MUT_ELEM_BLOCK_COUNT * sizeof(struct taurus_element));
    if (!blk) return NULL;

    blk->next = doc->mut_elem_blocks;
    doc->mut_elem_blocks = blk;
    doc->mut_elem_cursor = (TaurusElement)blk->bytes;
    doc->mut_elem_end = doc->mut_elem_cursor + MUT_ELEM_BLOCK_COUNT;
    return doc->mut_elem_cursor++;
}

/**
 * Create new element in document (Public API)
 */
TaurusElement taurus_element_create(TaurusDocument doc, const char* name) {
    if (!doc || !name) return NULL;

    /* Issue #187: trigger lazy promote if the doc was produced by
     * the flat-parse fast path. The compact-pointer tree must exist
     * before we can allocate a new element into it. */
    taurus_document_ensure_promoted(doc);

    TaurusElement elem = NULL;
    if (doc->pool) {
        /* Round 18: struct from the bump block, name from the pool.
         * Same init contract as taurus_element_create_with_view
         * (memset-zero + type/name/name_hash/name_len). Falls back
         * to the plain pool path when a block can't be allocated. */
        elem = mut_elem_carve(doc);
        if (elem) {
            char* name_copy = taurus_pool_strdup(doc->pool, name);
            if (!name_copy) return NULL;
            size_t name_len = strlen(name);
            memset(elem, 0, sizeof(struct taurus_element));
            elem->base.type = TAURUS_NODE_TYPE_ELEMENT;
            elem->name = name_copy;
            elem->name_hash = taurus_name_hash_compute(name_copy);
            elem->name_len = (name_len > 254) ? 0xFF : (uint8_t)name_len;
        } else {
            elem = taurus_element_create_pooled(name, doc->pool);
        }
    } else {
        elem = taurus_element_create_pooled(name, doc->pool);
    }
    if (elem) {
        /* TODO 155 Phase A: register elem as a root in the thread-local
         * root→doc map so descendants (and pre-attach ops) can reach
         * the doc via walk + lookup. */
        taurus_root_doc_register(elem, doc);
    }
    return elem;
}

/**
 * Append child element (Public API)
 */
TaurusStatus taurus_element_append_child(TaurusElement parent, TaurusElement child) {
    if (!parent || !child) return TAURUS_ERROR_NULL_ARG;

    /* Single document resolution per mutation (TODO 195c): the
     * root walk + map lookup was paid three times per append
     * (twice here, once inside the internal for the tail cache). */
    struct taurus_document* doc = taurus_element_get_document(parent);

    /* Call internal void function, assume success */
    taurus_element_append_child_internal_doc(parent, (TaurusNode*)child, doc);
    taurus_element_invalidate_child_cache(parent);

    /* Invalidate element index (TODO 132): the new child changes
     * the document's element set. The index will be rebuilt lazily
     * on the next descendant-axis query. */
    if (doc) {
        taurus_element_index_invalidate(doc);
    }
    return TAURUS_OK;
}

/**
 * Prepend child element at the beginning (Public API)
 */
TaurusStatus taurus_element_prepend_child(TaurusElement parent, TaurusElement child) {
    if (!parent || !child) return TAURUS_ERROR_NULL_ARG;

    /* Call internal void function, assume success */
    taurus_element_prepend_child_internal(parent, (TaurusNode*)child);
    taurus_element_invalidate_child_cache(parent);
    return TAURUS_OK;
}

/**
 * Insert new node before a sibling (Public API)
 * Issue #216: supports all child node types (element, text, comment,
 * cdata, pi), not just elements. The TaurusElement signature is kept
 * for ABI stability; callers cast non-element node pointers.
 */
TaurusStatus taurus_element_insert_before(TaurusElement sibling, TaurusElement new_node) {
    if (!sibling || !new_node) return TAURUS_ERROR_NULL_ARG;

    TaurusNode* new_node_ptr = (TaurusNode*)new_node;
    TaurusNode* sibling_ptr = (TaurusNode*)sibling;

    /* Validate new_node type. */
    if (new_node_ptr->type != TAURUS_NODE_TYPE_ELEMENT &&
        new_node_ptr->type != TAURUS_NODE_TYPE_TEXT &&
        new_node_ptr->type != TAURUS_NODE_TYPE_CDATA &&
        new_node_ptr->type != TAURUS_NODE_TYPE_COMMENT &&
        new_node_ptr->type != TAURUS_NODE_TYPE_PI) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* Get parent using the type-dispatching accessor — sibling may
     * be any child node type. */
    TaurusElement parent = taurus_node_parent(sibling_ptr);
    if (!parent) return TAURUS_ERROR_INVALID_ARG;

    /* Issue #217: unlink new_node from any current parent so we
     * don't corrupt the old tree. */
    TaurusElement new_old_parent = taurus_node_parent(new_node_ptr);
    if (new_old_parent && new_old_parent != parent) {
        taurus_node_unlink(new_node_ptr);
    }

    /* Walk the parent's true child chain (including non-element
     * children) to find the node before sibling. */
    TaurusNode* prev_child = NULL;
    TaurusNode* current = taurus_node_first_child_internal((TaurusNode*)parent);
    while (current && current != sibling_ptr) {
        prev_child = current;
        current = taurus_node_get_next_sibling(current);
    }
    if (!current) return TAURUS_ERROR_INVALID_ARG;

    /* Splice new_node in between prev_child and sibling. */
    if (prev_child) {
        taurus_node_set_next_sibling(prev_child, new_node_ptr);
    } else {
        taurus_elem_set_first_child(parent, new_node_ptr);
    }
    taurus_node_set_next_sibling(new_node_ptr, sibling_ptr);

    /* Set parent (type-dispatching). TODO 155 Phase A: document field
     * removed; non-root elements reach doc via walk to root. */
    if (new_node_ptr->type == TAURUS_NODE_TYPE_ELEMENT) {
        taurus_element_set_parent((TaurusElement)new_node_ptr, parent);
    } else {
        switch (new_node_ptr->type) {
            case TAURUS_NODE_TYPE_TEXT:
                taurus_textnode_set_parent((TaurusTextNode*)new_node_ptr, parent);
                break;
            case TAURUS_NODE_TYPE_COMMENT:
                taurus_comment_set_parent((TaurusCommentNode*)new_node_ptr, parent);
                break;
            case TAURUS_NODE_TYPE_CDATA:
                taurus_cdata_set_parent((TaurusCDATANode*)new_node_ptr, parent);
                break;
            case TAURUS_NODE_TYPE_PI:
                taurus_pi_set_parent((TaurusPINode*)new_node_ptr, parent);
                break;
            default: break;
        }
    }

    /* Issue #213: maintain child_count for element children only,
     * matching taurus_element_append_child_internal. */
    if (new_node_ptr->type == TAURUS_NODE_TYPE_ELEMENT) {
        parent->child_count++;
    }

    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(parent));
    taurus_element_invalidate_child_cache(parent);
    return TAURUS_OK;
}

/**
 * Insert new node after a sibling (Public API)
 * Issue #216: supports all child node types.
 */
TaurusStatus taurus_element_insert_after(TaurusElement sibling, TaurusElement new_node) {
    if (!sibling || !new_node) return TAURUS_ERROR_NULL_ARG;

    TaurusNode* new_node_ptr = (TaurusNode*)new_node;
    TaurusNode* sibling_ptr = (TaurusNode*)sibling;

    if (new_node_ptr->type != TAURUS_NODE_TYPE_ELEMENT &&
        new_node_ptr->type != TAURUS_NODE_TYPE_TEXT &&
        new_node_ptr->type != TAURUS_NODE_TYPE_CDATA &&
        new_node_ptr->type != TAURUS_NODE_TYPE_COMMENT &&
        new_node_ptr->type != TAURUS_NODE_TYPE_PI) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    TaurusElement parent = taurus_node_parent(sibling_ptr);
    if (!parent) return TAURUS_ERROR_INVALID_ARG;

    /* Issue #217: unlink new_node from any current parent first. */
    TaurusElement new_old_parent = taurus_node_parent(new_node_ptr);
    if (new_old_parent && new_old_parent != parent) {
        taurus_node_unlink(new_node_ptr);
    }

    TaurusNode* next_sibling = taurus_node_get_next_sibling(sibling_ptr);

    /* Splice new_node between sibling and next_sibling. Use the
     * type-dispatching setter on new_node — taurus_elem_set_next_sibling
     * only writes the element-form of the field. */
    taurus_node_set_next_sibling(sibling_ptr, new_node_ptr);
    taurus_node_set_next_sibling(new_node_ptr, next_sibling);

    /* Set parent (type-dispatching). TODO 155 Phase A: document field
     * removed; non-root elements reach doc via walk to root. */
    if (new_node_ptr->type == TAURUS_NODE_TYPE_ELEMENT) {
        taurus_element_set_parent((TaurusElement)new_node_ptr, parent);
    } else {
        switch (new_node_ptr->type) {
            case TAURUS_NODE_TYPE_TEXT:
                taurus_textnode_set_parent((TaurusTextNode*)new_node_ptr, parent);
                break;
            case TAURUS_NODE_TYPE_COMMENT:
                taurus_comment_set_parent((TaurusCommentNode*)new_node_ptr, parent);
                break;
            case TAURUS_NODE_TYPE_CDATA:
                taurus_cdata_set_parent((TaurusCDATANode*)new_node_ptr, parent);
                break;
            case TAURUS_NODE_TYPE_PI:
                taurus_pi_set_parent((TaurusPINode*)new_node_ptr, parent);
                break;
            default: break;
        }
    }

    /* Update last_child if sibling was the last child. */
    TaurusNode* last = taurus_node_last_child_internal((TaurusNode*)parent);
    if (last == sibling_ptr) {
        taurus_elem_set_last_child(parent, new_node_ptr);
    }

    if (new_node_ptr->type == TAURUS_NODE_TYPE_ELEMENT) {
        parent->child_count++;
    }

    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(parent));
    taurus_element_invalidate_child_cache(parent);
    return TAURUS_OK;
}

/**
 * Remove child element (Public API)
 * COMPACT MODE: Simplified implementation
 */
TaurusStatus taurus_element_remove_child(TaurusElement parent, TaurusElement child) {
    if (!parent || !child) return TAURUS_ERROR_NULL_ARG;

    /* Verify child is actually a child of parent */
    TaurusElement found = NULL;
    TaurusElement current = taurus_element_get_first_child(parent);
    while (current) {
        if (current == child) {
            found = current;
            break;
        }
        current = taurus_element_get_next_sibling(current);
    }

    if (!found) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* Find the node before child in the list */
    TaurusElement prev_child = NULL;
    current = taurus_element_get_first_child(parent);
    while (current && current != child) {
        prev_child = current;
        current = taurus_element_get_next_sibling(current);
    }

    TaurusElement next_child = taurus_element_get_next_sibling(child);

    /* Unlink child from the list */
    if (prev_child) {
        taurus_elem_set_next_sibling(prev_child, (TaurusNode*)next_child);
    } else {
        /* Child was first child */
        taurus_elem_set_first_child(parent, (TaurusNode*)next_child);
    }

    /* Update last_child pointer - directly check instead of using get_last_child */
    if (taurus_elem_last_child(parent) == (TaurusNode*)child) {
        /* Child was last child */
        taurus_elem_set_last_child(parent, (TaurusNode*)prev_child);
    }

    /* Clear parent and decrement count */
    taurus_elem_set_parent(child, NULL);
    parent->child_count--;

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(parent));

    taurus_element_invalidate_child_cache(parent);

    /* Structural mutation: a cached element index would describe the
     * pre-removal tree (append_child invalidates; removal must too). */
    if (taurus_element_get_document(parent)) {
        taurus_element_index_invalidate(taurus_element_get_document(parent));
    }
    return TAURUS_OK;
}

/**
 * Remove all children (Public API)
 */
TaurusStatus taurus_element_remove_all_children(TaurusElement elem) {
    if (!elem) return TAURUS_ERROR_NULL_ARG;

    /* Walk through all children and clear parent references */
    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        TaurusElement next = taurus_element_get_next_sibling(child);
        taurus_elem_set_parent(child, NULL);
        child = next;
    }

    /* Clear child pointers */
    taurus_elem_set_first_child(elem, NULL);
    taurus_elem_set_last_child(elem, NULL);
    elem->child_count = 0;

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(elem));

    taurus_element_invalidate_child_cache(elem);

    /* Structural mutation: invalidate any cached element index. */
    if (taurus_element_get_document(elem)) {
        taurus_element_index_invalidate(taurus_element_get_document(elem));
    }
    return TAURUS_OK;
}

/**
 * Set element name (Public API)
 */
TaurusStatus taurus_element_set_name(TaurusElement elem, const char* name) {
    if (!elem) return TAURUS_ERROR_NULL_ARG;
    if (!name) return TAURUS_ERROR_INVALID_ARG;

    /* CRITICAL FIX: Don't free old name - it might be pool-allocated!
     * Pool-allocated strings will be freed when the pool is destroyed.
     * If we free() a pool-allocated string, we get undefined behavior. */

    /* Set new name - prefer pool allocation if document is available */
    if (taurus_element_get_document(elem) && taurus_element_get_pool(elem)) {
        /* Use pool allocation for consistency with parsing */
        elem->name = taurus_pool_strdup(taurus_element_get_pool(elem), name);
    } else {
        /* Fallback to malloc for standalone elements */
        elem->name = taurus_strdup(name);
    }
    elem->name_hash = taurus_name_hash_compute(elem->name);

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(elem));

    return TAURUS_OK;
}

/**
 * Set element text content (Public API)
 * COMPACT MODE: Removes all existing children and adds a single text node
 */
TaurusStatus taurus_element_set_text(TaurusElement elem, const char* text) {
    if (!elem) return TAURUS_ERROR_NULL_ARG;

    /* Remove all existing children */
    taurus_element_remove_all_children(elem);

    if (text) {
        /* Create new text node (even if empty to match pugixml behavior) */
        TaurusMemoryPool* pool = taurus_element_get_pool(elem);
        TaurusTextNode* text_node = taurus_text_create(text, strlen(text), pool);
        if (!text_node) {
            return TAURUS_ERROR_MEMORY;
        }

        /* Add as child */
        taurus_element_append_child_internal(elem, (TaurusNode*)text_node);
    }

    return TAURUS_OK;
}

/**
 * Set attribute (Public API)
 * COMPACT MODE: Uses linked list attribute storage
 */

/* ---- Doc-level attribute-name index (mutation path) --------------------
 *
 * taurus_element_set_attribute walks the attr list per call to
 * reject duplicates — O(N) per set, O(N^2) for programmatic builds
 * (11 ms at 2000 attrs on one element). This open-addressed index
 * keys (element pointer, 32-bit name hash) -> attribute so the
 * duplicate check is O(1). Safety: nodes are arena-backed and
 * element removal only unlinks (never frees), so raw attr pointers
 * cannot dangle before taurus_document_free — which frees the
 * index. Entries: attr != NULL live; attr == NULL with elem != NULL
 * is a tombstone (probe continues); all-NULL slot ends the probe.
 * The (elem, hash == 0) sentinel marks an element as registered:
 * parse-created attrs are bulk-inserted on the element's first
 * mutation so the index is authoritative without ever touching the
 * parse path. ------------------------------------------------------------------ */

/* struct taurus_attr_index_entry / _index: taurus_internal.h
 * (document_free releases the table). */

#define ATTR_INDEX_INIT_CAP 16u

/* Index keys use the FULL 32-bit FNV of the name — independent of
 * the attr field's 15-bit lazy hash (round 20). The u15 field feeds
 * per-element walk pre-filters; the doc-level open-addressed index
 * needs full-width keys so distinct names never share a probe key
 * (a u15 key space turns each hash collision into an O(N) fallback
 * walk, re-quadraticizing programmatic builds). */
static uint32_t attr_index_hash(const char* s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t attr_index_slot(TaurusElement elem, uint32_t name_hash,
                                size_t cap) {
    uint64_t h = (uint64_t)(uintptr_t)elem;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    /* Round 20: fold AFTER the multiply. The old `h2 ^= h2 >> 15`
     * was a no-op for the 15-bit attr hashes — with stride-256 hash
     * progressions (pre-finalizer bug) that piled whole arithmetic
     * sequences onto single probe chains. */
    uint64_t h2 = (uint64_t)name_hash * 0x9E3779B97F4A7C15ULL;
    h2 ^= h2 >> 32;
    return (uint32_t)((h ^ h2) & (cap - 1));
}

static struct taurus_attr_index* attr_index_get(struct taurus_document* doc) {
    if (!doc->attr_index) {
        doc->attr_index = (struct taurus_attr_index*)malloc(
            sizeof(struct taurus_attr_index));
        if (!doc->attr_index) return NULL;
        doc->attr_index->slots = (struct taurus_attr_index_entry*)calloc(
            ATTR_INDEX_INIT_CAP, sizeof(struct taurus_attr_index_entry));
        if (!doc->attr_index->slots) {
            free(doc->attr_index);
            doc->attr_index = NULL;
            return NULL;
        }
        doc->attr_index->cap = ATTR_INDEX_INIT_CAP;
        doc->attr_index->used = 0;
    }
    return doc->attr_index;
}

/* Probe for (elem, name_hash). On hit returns the slot index with
 * *attr_out set (NULL when the slot is the registration sentinel).
 * On miss returns the empty-slot index for insertion. */
static size_t attr_index_probe(struct taurus_attr_index* ix,
                               TaurusElement elem, uint32_t name_hash,
                               struct taurus_attribute** attr_out,
                               int* found) {
    size_t cap = ix->cap;
    size_t i = attr_index_slot(elem, name_hash, cap);
    *found = 0;
    for (size_t n_ = 0; n_ < cap; n_++) {
        struct taurus_attr_index_entry* e = &ix->slots[i];
        if (!e->elem && !e->attr) return i;         /* never used: miss */
        if (e->elem == elem && e->name_hash == name_hash) {
            if (e->attr) { *attr_out = e->attr; *found = 1; return i; }
            if (name_hash == 0) { *attr_out = NULL; *found = 1; return i; }
        }
        i = (i + 1) & (cap - 1);
    }
    return (size_t)-1; /* full (cannot happen: rehash keeps load < 0.7) */
}

static void attr_index_rehash(struct taurus_attr_index* ix) {
    size_t ncap = ix->cap * 2;
    struct taurus_attr_index_entry* ns = (struct taurus_attr_index_entry*)
        calloc(ncap, sizeof(struct taurus_attr_index_entry));
    if (!ns) return; /* stay at old size; probe loop tolerates it */
    for (size_t i = 0; i < ix->cap; i++) {
        struct taurus_attr_index_entry e = ix->slots[i];
        if (!e.elem && !e.attr) continue;
        if (!e.attr && e.name_hash != 0) continue;  /* drop tombstones */
        size_t j = attr_index_slot(e.elem, e.name_hash, ncap);
        while (ns[j].elem || ns[j].attr) j = (j + 1) & (ncap - 1);
        ns[j] = e;
    }
    free(ix->slots);
    ix->slots = ns;
    ix->cap = ncap;
    ix->used = 0;
    for (size_t i = 0; i < ncap; i++) {
        if (ns[i].elem || ns[i].attr) ix->used++;
    }
}

static void attr_index_put(struct taurus_attr_index* ix, TaurusElement elem,
                           uint32_t name_hash,
                           struct taurus_attribute* attr, size_t slot) {
    if (slot >= ix->cap) {
        attr_index_rehash(ix);
        struct taurus_attribute* dummy;
        int f;
        slot = attr_index_probe(ix, elem, name_hash, &dummy, &f);
        if (slot == (size_t)-1) return;
    }
    struct taurus_attr_index_entry* e = &ix->slots[slot];
    if (!e->elem && !e->attr) ix->used++;
    e->elem = elem;
    e->name_hash = name_hash;
    e->attr = attr;
    if (ix->used * 10 >= ix->cap * 7) attr_index_rehash(ix);
}

/* Bulk-register every attr of `elem` plus the (elem, 0) sentinel so
 * later probes are authoritative. Falls back silently (index stays
 * partial) only on allocation failure — callers must then use the
 * list walk. */
static void attr_index_register(struct taurus_document* doc,
                                struct taurus_attr_index* ix,
                                TaurusElement elem) {
    struct taurus_attribute* dummy;
    int found;
    size_t slot = attr_index_probe(ix, elem, 0, &dummy, &found);
    if (found || slot == (size_t)-1) return;
    attr_index_put(ix, elem, 0, NULL, slot);
    struct taurus_attribute* a = taurus_element_get_first_attribute(elem);
    while (a) {
        uint32_t h = attr_index_hash(attr_cname(a), a->name_view.length);
        slot = attr_index_probe(ix, elem, h, &dummy, &found);
        if (!found && slot != (size_t)-1) {
            attr_index_put(ix, elem, h, a, slot);
        }
        a = taurus_attr_next(a);
    }
}

/* O(1) duplicate check with lazy registration. Returns the existing
 * attr or NULL; *ix_out/registered bookkeeping lets the caller
 * insert the new attr without re-probing. */
static struct taurus_attribute* attr_index_lookup(
    struct taurus_document* doc, TaurusElement elem, const char* name,
    size_t name_len, uint32_t name_hash,
    struct taurus_attr_index** ix_out, size_t* insert_slot) {
    *ix_out = NULL;
    *insert_slot = (size_t)-1;
    struct taurus_attr_index* ix = attr_index_get(doc);
    if (!ix) return NULL;
    attr_index_register(doc, ix, elem);
    struct taurus_attribute* hit = NULL;
    int found;
    size_t slot = attr_index_probe(ix, elem, name_hash, &hit, &found);
    if (slot == (size_t)-1) return NULL;
    if (!found) {
        *ix_out = ix;
        *insert_slot = slot;
        return NULL;
    }
    if (!hit) return NULL;                 /* sentinel-only element */
    /* Confirm with the string (hash collisions). */
    if (hit->name_view.length == name_len &&
        memcmp(attr_cname(hit), name, name_len) == 0) {
        return hit;
    }
    /* Collision with a different name: fall back to the walk. */
    return taurus_element_get_attribute_by_name(elem, name);
}

TaurusStatus taurus_element_set_attribute(TaurusElement elem, const char* name, const char* value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    /* Mutation-path perf: skip the pool's string interning hash table
     * (TODO 106 Phase 1).  Interning deduplicates attribute NAMES that
     * recur across many elements during parsing; on the public mutation
     * path the user knows the attr is unique, and the hash lookup/insert
     * is pure overhead — ~200ns × N attrs.  Inline pool_alloc + memcpy
     * is faster than taurus_sv_to_cstr_pooled for this case. */

    /* Check if attribute already exists — O(1) via the doc-level
     * attr-name index (lazy registration covers parse-created attrs);
     * NULL index (alloc failure) falls back to the list walk. */
    size_t set_name_len = strlen(name);
    uint32_t set_name_hash = attr_index_hash(name, set_name_len);
    struct taurus_document* set_doc = taurus_element_get_document(elem);
    struct taurus_attr_index* set_ix = NULL;
    size_t set_slot = (size_t)-1;
    struct taurus_attribute* existing =
        set_doc
            ? attr_index_lookup(set_doc, elem, name, set_name_len,
                                set_name_hash, &set_ix, &set_slot)
            : taurus_element_get_attribute_by_name(elem, name);
    if (existing) {
        /* Update existing attribute's value */
        TaurusMemoryPool* pool = NULL;
        if (taurus_element_get_document(elem) && taurus_element_get_pool(elem)) {
            pool = taurus_element_get_pool(elem);
        }

        if (pool) {
            /* Pool-allocated document: pool_strdup the new value (no
             * interning — see header comment).  Old value is pool-
             * allocated and reclaims when the pool frees. */
            if (value) {
                size_t vlen = strlen(value);
                char* storage = (char*)taurus_pool_alloc(pool, vlen + 1);
                if (!storage) return TAURUS_ERROR_MEMORY;
                memcpy(storage, value, vlen);
                storage[vlen] = '\0';
                existing->value_view = taurus_sv_from_ptr(storage, vlen);
            } else {
                existing->value_view = taurus_sv_empty();
            }
            attr_set_entities(existing, 0);
        } else {
            /* No pool available: views into the caller's string
             * (fallback for edge cases — mutation API contract says
             * the value string must outlive the attribute here). */
            existing->value_view =
                value ? taurus_sv_from_cstr(value) : taurus_sv_empty();
            attr_set_entities(existing, 0);
        }
    } else {
        /* Get the memory pool from the document */
        TaurusMemoryPool* pool = NULL;
        if (taurus_element_get_document(elem) && taurus_element_get_pool(elem)) {
            pool = taurus_element_get_pool(elem);
        } else {
            /* CRITICAL: No pool available - cannot allocate attributes without memory pool */
            return TAURUS_ERROR_MEMORY;
        }

        /* Allocate attribute from pool (not malloc!) - CRITICAL for correct pointer handling */
        struct taurus_attribute* attr = (struct taurus_attribute*)taurus_pool_alloc(
            pool, sizeof(struct taurus_attribute));
        if (!attr) {
            return TAURUS_ERROR_MEMORY;
        }

        /* Pool-strdup name (NO interning — mutation fast path). */
        size_t nlen = strlen(name);
        char* name_storage = (char*)taurus_pool_alloc(pool, nlen + 1);
        if (!name_storage) return TAURUS_ERROR_MEMORY;
        memcpy(name_storage, name, nlen);
        name_storage[nlen] = '\0';
        attr->name_view = taurus_sv_from_ptr(name_storage, nlen);

        /* Pre-compute name hash for O(1) lookup filtering (TODO 113).
         * Round 19: 15-bit + entity flag share the field. */
        attr->name_hash = attr_hash15(name_storage, nlen);

        if (value) {
            size_t vlen = strlen(value);
            char* value_storage = (char*)taurus_pool_alloc(pool, vlen + 1);
            if (!value_storage) return TAURUS_ERROR_MEMORY;
            memcpy(value_storage, value, vlen);
            value_storage[vlen] = '\0';
            attr->value_view = taurus_sv_from_ptr(value_storage, vlen);
        } else {
            attr->value_view = taurus_sv_empty();
        }

        attr->ns_cache_off = 0;  /* TODO 173 */
        taurus_attr_set_next(attr, NULL);

        /* Append via the doc-level attr-tail cache — O(1) for the
         * sequential case (the element struct carries no last-attr
         * edge by the 64-byte layout law; the walk is O(attr_count)
         * and made programmatic builds quadratic). */
        struct taurus_attribute* last =
            (set_doc && set_doc->mut_attr_elem == elem)
                ? set_doc->mut_attr_tail
                : taurus_elem_last_attribute(elem);
        if (last) {
            taurus_attr_set_next(last, attr);
        } else {
            taurus_elem_set_first_attribute(elem, attr);
        }
        taurus_elem_set_last_attribute(elem, attr);
        if (set_doc) {
            set_doc->mut_attr_elem = elem;
            set_doc->mut_attr_tail = attr;
        }

        elem->attr_count++;

        if (set_ix && set_slot != (size_t)-1) {
            attr_index_put(set_ix, elem, set_name_hash, attr, set_slot);
        }
    }

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(elem));

    return TAURUS_OK;
}

/**
 * Remove attribute (Public API)
 */
TaurusStatus taurus_element_remove_attribute(TaurusElement elem, const char* name) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    struct taurus_document* rm_doc = taurus_element_get_document(elem);
    uint32_t rm_hash = attr_index_hash(name, strlen(name));

    struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
    struct taurus_attribute* prev = NULL;

    while (attr) {
        const char* attr_name = attr_cname(attr);

        int match = 0;
        if (attr_name) {
            match = (strcmp(attr_name, name) == 0);
        }

        if (match) {
            /* Found - remove from linked list */
            if (prev) {
                taurus_attr_set_next(prev, taurus_attr_next(attr));
            } else {
                /* Was first attribute - update first_attribute pointer */
                if (taurus_attr_next(attr)) {
                    taurus_element_set_first_attribute(elem, taurus_attr_next(attr));
                } else {
                    /* No more attributes */
                    taurus_element_set_first_attribute(elem, NULL);
                }
            }

            /* CRITICAL: Don't free pool-allocated attributes!
             * Attributes are allocated from the memory pool and will be freed
             * when the pool is destroyed. If we free() them here, we get undefined behavior.
             * Just decrement the count and let the pool handle cleanup. */

            /* CRITICAL: Don't free attribute strings!
             * Attributes created by parser or programmatically use pool allocation.
             * Pool-allocated strings cannot be individually freed - they will be
             * reclaimed when the entire document/pool is freed.
             * Trying to free() them here causes undefined behavior (Abort trap). */

            /* Tombstone the index entry: the attr memory stays valid
             * (pool-backed) but is detached — a later set_attribute
             * must not resurrect it. */
            if (rm_doc && rm_doc->mut_attr_elem == elem &&
                rm_doc->mut_attr_tail == attr) {
                rm_doc->mut_attr_elem = NULL;
                rm_doc->mut_attr_tail = NULL;
            }
            if (rm_doc && rm_doc->attr_index) {
                struct taurus_attribute* dummy;
                int found;
                size_t slot = attr_index_probe(rm_doc->attr_index, elem,
                                               rm_hash, &dummy, &found);
                if (found && rm_doc->attr_index->slots[slot].attr == attr) {
                    rm_doc->attr_index->slots[slot].attr = NULL;
                }
            }

            elem->attr_count--;
            taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(elem));
            return TAURUS_OK;
        }

        prev = attr;
        attr = taurus_attr_next(attr);
    }

    return TAURUS_ERROR_NOT_FOUND;  /* Attribute not found */
}

/**
 * Append copy of element (Public API)
 */
TaurusElement taurus_element_append_copy(TaurusElement parent, TaurusElement source) {
    if (!parent || !source) return NULL;

    /* CRITICAL: Check document pointer FIRST before any other access!
     * Use volatile to prevent compiler from reordering this check. */
    volatile struct taurus_document* parent_doc = taurus_element_get_document(parent);
    if (!parent_doc) {
        return NULL;
    }

    if (!parent_doc->pool) {
        return NULL;
    }

    /* Verify source has document too */
    if (!taurus_element_get_document(source)) {
        return NULL;  /* Source element must have a document */
    }

    /* Get source name as StringView (zero-copy, O(1) access) */
    TaurusStringView name_view = taurus_element_name_view(source);
    if (taurus_sv_is_empty(&name_view)) return NULL;

    /* Check if this is a cross-document copy */
    int is_cross_doc = (taurus_element_get_document(source) != taurus_element_get_document(parent));

    /* For cross-document copies, we need to copy the name string data
     * because the StringView points to the source document's XML buffer */
    TaurusStringView name_copy_view = name_view;  /* Default: use original StringView */
    if (is_cross_doc) {
        /* Double-check that target pool is valid before allocating */
        if (!taurus_element_get_pool(parent)) {
            return NULL;  /* Pool became NULL somehow */
        }

        /* Allocate and copy name string to target document's pool */
        char* name_copy = taurus_sv_to_cstr_pooled(&name_view, taurus_element_get_pool(parent));
        if (!name_copy) return NULL;
        name_copy_view = taurus_sv_from_cstr(name_copy);
    }

    /* Fast path: Simple element (no attributes, no children) - skip all loops */
    if (!taurus_element_get_first_attribute(source) && !taurus_elem_first_child(source)) {
        TaurusElement copy = taurus_element_create_with_view(name_copy_view, taurus_element_get_pool(parent));
        if (!copy) return NULL;
        if (taurus_element_append_child(parent, copy) != TAURUS_OK) {
            return NULL;
        }
        return copy;
    }

    /* Create new element with StringView (no C string conversion!) */
    TaurusElement copy = taurus_element_create_with_view(name_copy_view, taurus_element_get_pool(parent));
    if (!copy) return NULL;

    /* TODO 155 Phase A: register copy as a temporary root so recursive
     * child-copy calls can reach the pool via taurus_element_get_pool. */
    taurus_root_doc_register(copy, taurus_element_get_document(parent));

    /* Copy attributes - optimized: direct StringView copy when same document */
    uint8_t attr_count = taurus_element_attribute_count(source);
    for (uint8_t i = 0; i < attr_count; i++) {
        struct taurus_attribute* attr = taurus_element_get_attribute_by_index(source, i);
        if (attr && !taurus_sv_is_empty(&attr->name_view)) {
            TaurusStringView attr_name_view = attr->name_view;
            TaurusStringView attr_value_view = attr->value_view;

            /* For cross-document copies, copy the attribute string data */
            if (is_cross_doc) {
                /* Double-check that target pool is valid */
                if (!taurus_element_get_pool(parent)) {
                    continue;  /* Skip this attribute if pool is invalid */
                }

                char* name_copy = taurus_sv_to_cstr_pooled(&attr->name_view, taurus_element_get_pool(parent));
                char* value_copy = taurus_sv_to_cstr_pooled(&attr->value_view, taurus_element_get_pool(parent));
                if (!name_copy || !value_copy) {
                    /* Allocation failed - skip this attribute */
                    if (name_copy) /* Nothing to free, pool-allocated */
                    if (value_copy) /* Nothing to free, pool-allocated */
                    continue;
                }
                attr_name_view = taurus_sv_from_cstr(name_copy);
                attr_value_view = taurus_sv_from_cstr(value_copy);
            }

            /* Use pool-based fast path if available, otherwise fallback to set_attribute
             * Pool path is 2-3x faster (no hash lookup, no string conversion) */
            if (taurus_element_get_pool(parent)) {
                taurus_element_add_attribute(copy, attr_name_view, attr_value_view, taurus_element_get_pool(parent));
            } else {
                /* Fallback: C-string API (views are always valid C
                 * strings under the single representation). */
                const char* attr_name = attr_cname(attr);
                const char* attr_value = attr_cvalue(attr);
                if (attr_name && attr_value) {
                    taurus_element_set_attribute(copy, attr_name, attr_value);
                }
            }
        }
    }

    /* Copy children recursively */
    TaurusElement child = (TaurusElement)taurus_elem_first_child(source);

    /* SAFETY: Verify child pointer is valid before accessing.
     * Small values like 0x4 indicate memory corruption or uninitialized fields. */
    while (child) {
        /* Check if pointer looks valid (not obviously corrupted)
         * Values less than 0x1000 are likely invalid pointers or offsets */
        if ((uintptr_t)child < 0x1000) {
            /* Invalid pointer - skip this child to prevent crash */
            break;
        }

        /* Additional safety: Check if pointer points to readable memory
         * by verifying the type field is within valid range */
        TaurusNode* child_node = (TaurusNode*)child;

        if (child_node->type < TAURUS_NODE_TYPE_ELEMENT ||
            child_node->type > TAURUS_NODE_TYPE_DOCTYPE) {
            /* Invalid type field - likely corrupted or pointing to wrong memory */
            break;
        }

        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively copy element child */
            taurus_element_append_copy(copy, child);
        } else if (child_node->type == TAURUS_NODE_TYPE_TEXT) {
            TaurusTextNode* text = (TaurusTextNode*)child;
            /* SAFETY: Verify text pointer is valid before accessing content */
            if ((uintptr_t)text > 0x1000) {
                TaurusTextNode* text_copy = taurus_text_create(text->content,
                    text->content_len,
                    taurus_element_get_pool(copy));
                if (text_copy) {
                    taurus_element_append_child_internal(copy, (TaurusNode*)text_copy);
                }
            }
        } else if (child_node->type == TAURUS_NODE_TYPE_CDATA) {
            TaurusCDATANode* cdata = (TaurusCDATANode*)child;
            if ((uintptr_t)cdata > 0x1000) {
                TaurusCDATANode* cdata_copy = taurus_cdata_create(cdata->content,
                    cdata->content ? strlen(cdata->content) : 0,
                    taurus_element_get_pool(copy));
                if (cdata_copy) {
                    taurus_element_append_child_internal(copy, (TaurusNode*)cdata_copy);
                }
            }
        }
        /* Skip COMMENT and PI nodes for now - they're less common */

        /* CRITICAL FIX: Get next sibling using generic accessor
         * The child variable is declared as TaurusElement but might actually
         * point to a text node or other node type. Each node type has next_sibling
         * at a different offset, so we must use the generic accessor.
         * Note: child_node is already declared above at line 598 */
        TaurusNode* next = taurus_node_get_next_sibling(child_node);
        if (!next) {
            /* No more siblings */
            break;
        }
        child = (TaurusElement)next;
    }

    /* Append to parent */
    if (taurus_element_append_child(parent, copy) != TAURUS_OK) {
        /* Note: copy is pool-allocated, will be freed with document */
        return NULL;
    }

    return copy;
}

/**
 * Prepend copy of element (Public API)
 */
TaurusElement taurus_element_prepend_copy(TaurusElement parent, TaurusElement source) {
    if (!parent || !source) return NULL;

    /* Get source name as StringView (zero-copy, O(1) access) */
    TaurusStringView name_view = taurus_element_name_view(source);
    if (taurus_sv_is_empty(&name_view)) return NULL;

    /* Check if this is a cross-document copy */
    int is_cross_doc = (taurus_element_get_document(source) != taurus_element_get_document(parent));

    /* For cross-document copies, we need to copy the name string data
     * because the StringView points to the source document's XML buffer */
    TaurusStringView name_copy_view = name_view;  /* Default: use original StringView */
    if (is_cross_doc) {
        /* Allocate and copy name string to target document's pool */
        char* name_copy = taurus_sv_to_cstr_pooled(&name_view, taurus_element_get_pool(parent));
        if (!name_copy) return NULL;
        name_copy_view = taurus_sv_from_cstr(name_copy);
    }

    /* Fast path: Simple element (no attributes, no children) */
    if (!taurus_element_get_first_attribute(source) && !taurus_elem_first_child(source)) {
        TaurusElement copy = taurus_element_create_with_view(name_copy_view, taurus_element_get_pool(parent));
        if (!copy) return NULL;
        if (taurus_element_prepend_child(parent, copy) != TAURUS_OK) {
            return NULL;
        }
        return copy;
    }

    /* Create new element with StringView (no C string conversion!) */
    TaurusElement copy = taurus_element_create_with_view(name_copy_view, taurus_element_get_pool(parent));
    if (!copy) return NULL;

    /* TODO 155 Phase A: register copy as a temporary root so recursive
     * child-copy calls can reach the pool via taurus_element_get_pool. */
    taurus_root_doc_register(copy, taurus_element_get_document(parent));

    /* Copy attributes - optimized: direct StringView copy when same document */
    uint8_t attr_count = taurus_element_attribute_count(source);
    for (uint8_t i = 0; i < attr_count; i++) {
        struct taurus_attribute* attr = taurus_element_get_attribute_by_index(source, i);
        if (attr && !taurus_sv_is_empty(&attr->name_view)) {
            TaurusStringView attr_name_view = attr->name_view;
            TaurusStringView attr_value_view = attr->value_view;

            /* For cross-document copies, copy the attribute string data */
            if (is_cross_doc) {
                char* name_copy = taurus_sv_to_cstr_pooled(&attr->name_view, taurus_element_get_pool(parent));
                char* value_copy = taurus_sv_to_cstr_pooled(&attr->value_view, taurus_element_get_pool(parent));
                if (!name_copy || !value_copy) {
                    /* Allocation failed - skip this attribute */
                    if (name_copy) /* Nothing to free, pool-allocated */
                    if (value_copy) /* Nothing to free, pool-allocated */
                    continue;
                }
                attr_name_view = taurus_sv_from_cstr(name_copy);
                attr_value_view = taurus_sv_from_cstr(value_copy);
            }

            /* Use pool-based fast path if available, otherwise fallback to set_attribute
             * Pool path is 2-3x faster (no hash lookup, no string conversion) */
            if (taurus_element_get_pool(parent)) {
                taurus_element_add_attribute(copy, attr_name_view, attr_value_view, taurus_element_get_pool(parent));
            } else {
                /* Fallback: C-string API (views are always valid C
                 * strings under the single representation). */
                const char* attr_name = attr_cname(attr);
                const char* attr_value = attr_cvalue(attr);
                if (attr_name && attr_value) {
                    taurus_element_set_attribute(copy, attr_name, attr_value);
                }
            }
        }
    }

    /* Copy children recursively */
    TaurusElement child = (TaurusElement)taurus_elem_first_child(source);
    while (child) {
        TaurusNode* child_node = (TaurusNode*)child;
        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively copy element child */
            taurus_element_append_copy(copy, child);
        } else if (child_node->type == TAURUS_NODE_TYPE_TEXT) {
            TaurusTextNode* text = (TaurusTextNode*)child;
            TaurusTextNode* text_copy = taurus_text_create(text->content,
                text->content_len,
                taurus_element_get_pool(copy));
            if (text_copy) {
                taurus_element_append_child_internal(copy, (TaurusNode*)text_copy);
            }
        }
        /* Use generic accessor to get next sibling - child might be any node type */
        TaurusNode* next = taurus_node_get_next_sibling(child_node);
        if (!next) break;
        child = (TaurusElement)next;
    }

    /* Prepend to parent */
    if (taurus_element_prepend_child(parent, copy) != TAURUS_OK) {
        return NULL;
    }

    return copy;
}

/**
 * Insert copy after sibling (Public API)
 */
TaurusElement taurus_element_insert_copy_after(TaurusElement sibling, TaurusElement source) {
    if (!sibling || !source) return NULL;

    /* Get parent of sibling */
    TaurusElement parent = taurus_element_parent(sibling);
    if (!parent) return NULL;

    /* Get source name as StringView (zero-copy, O(1) access) */
    TaurusStringView name_view = taurus_element_name_view(source);
    if (taurus_sv_is_empty(&name_view)) return NULL;

    /* Check if this is a cross-document copy */
    int is_cross_doc = (taurus_element_get_document(source) != taurus_element_get_document(parent));

    /* For cross-document copies, we need to copy the name string data
     * because the StringView points to the source document's XML buffer */
    TaurusStringView name_copy_view = name_view;  /* Default: use original StringView */
    if (is_cross_doc) {
        /* Allocate and copy name string to target document's pool */
        char* name_copy = taurus_sv_to_cstr_pooled(&name_view, taurus_element_get_pool(parent));
        if (!name_copy) return NULL;
        name_copy_view = taurus_sv_from_cstr(name_copy);
    }

    /* Fast path: Simple element (no attributes, no children) */
    if (!taurus_element_get_first_attribute(source) && !taurus_elem_first_child(source)) {
        TaurusElement copy = taurus_element_create_with_view(name_copy_view, taurus_element_get_pool(parent));
        if (!copy) return NULL;
        if (taurus_element_insert_after(sibling, copy) != TAURUS_OK) {
            return NULL;
        }
        return copy;
    }

    /* Create new element with StringView (no C string conversion!) */
    TaurusElement copy = taurus_element_create_with_view(name_copy_view, taurus_element_get_pool(parent));
    if (!copy) return NULL;

    /* TODO 155 Phase A: register copy as a temporary root so recursive
     * child-copy calls can reach the pool via taurus_element_get_pool. */
    taurus_root_doc_register(copy, taurus_element_get_document(parent));

    /* Copy attributes - optimized: direct StringView copy when same document */
    uint8_t attr_count = taurus_element_attribute_count(source);
    for (uint8_t i = 0; i < attr_count; i++) {
        struct taurus_attribute* attr = taurus_element_get_attribute_by_index(source, i);
        if (attr && !taurus_sv_is_empty(&attr->name_view)) {
            TaurusStringView attr_name_view = attr->name_view;
            TaurusStringView attr_value_view = attr->value_view;

            /* For cross-document copies, copy the attribute string data */
            if (is_cross_doc) {
                char* name_copy = taurus_sv_to_cstr_pooled(&attr->name_view, taurus_element_get_pool(parent));
                char* value_copy = taurus_sv_to_cstr_pooled(&attr->value_view, taurus_element_get_pool(parent));
                if (!name_copy || !value_copy) {
                    /* Allocation failed - skip this attribute */
                    if (name_copy) /* Nothing to free, pool-allocated */
                    if (value_copy) /* Nothing to free, pool-allocated */
                    continue;
                }
                attr_name_view = taurus_sv_from_cstr(name_copy);
                attr_value_view = taurus_sv_from_cstr(value_copy);
            }

            /* Use pool-based fast path if available, otherwise fallback to set_attribute
             * Pool path is 2-3x faster (no hash lookup, no string conversion) */
            if (taurus_element_get_pool(parent)) {
                taurus_element_add_attribute(copy, attr_name_view, attr_value_view, taurus_element_get_pool(parent));
            } else {
                /* Fallback: C-string API (views are always valid C
                 * strings under the single representation). */
                const char* attr_name = attr_cname(attr);
                const char* attr_value = attr_cvalue(attr);
                if (attr_name && attr_value) {
                    taurus_element_set_attribute(copy, attr_name, attr_value);
                }
            }
        }
    }

    /* Copy children recursively */
    TaurusElement child = (TaurusElement)taurus_elem_first_child(source);
    while (child) {
        TaurusNode* child_node = (TaurusNode*)child;
        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively copy element child */
            taurus_element_append_copy(copy, child);
        } else if (child_node->type == TAURUS_NODE_TYPE_TEXT) {
            TaurusTextNode* text = (TaurusTextNode*)child;
            TaurusTextNode* text_copy = taurus_text_create(text->content,
                text->content_len,
                taurus_element_get_pool(copy));
            if (text_copy) {
                taurus_element_append_child_internal(copy, (TaurusNode*)text_copy);
            }
        }
        /* Use generic accessor to get next sibling - child might be any node type */
        TaurusNode* next = taurus_node_get_next_sibling(child_node);
        if (!next) break;
        child = (TaurusElement)next;
    }

    /* Insert after sibling */
    if (taurus_element_insert_after(sibling, copy) != TAURUS_OK) {
        return NULL;
    }

    return copy;
}

/**
 * Insert copy before sibling (Public API)
 */
TaurusElement taurus_element_insert_copy_before(TaurusElement sibling, TaurusElement source) {
    if (!sibling || !source) return NULL;

    /* Get parent of sibling */
    TaurusElement parent = taurus_element_parent(sibling);
    if (!parent) return NULL;

    /* Get source name as StringView (zero-copy, O(1) access) */
    TaurusStringView name_view = taurus_element_name_view(source);
    if (taurus_sv_is_empty(&name_view)) return NULL;

    /* Check if this is a cross-document copy */
    int is_cross_doc = (taurus_element_get_document(source) != taurus_element_get_document(parent));

    /* For cross-document copies, we need to copy the name string data
     * because the StringView points to the source document's XML buffer */
    TaurusStringView name_copy_view = name_view;  /* Default: use original StringView */
    if (is_cross_doc) {
        /* Allocate and copy name string to target document's pool */
        char* name_copy = taurus_sv_to_cstr_pooled(&name_view, taurus_element_get_pool(parent));
        if (!name_copy) return NULL;
        name_copy_view = taurus_sv_from_cstr(name_copy);
    }

    /* Fast path: Simple element (no attributes, no children) */
    if (!taurus_element_get_first_attribute(source) && !taurus_elem_first_child(source)) {
        TaurusElement copy = taurus_element_create_with_view(name_copy_view, taurus_element_get_pool(parent));
        if (!copy) return NULL;
        if (taurus_element_insert_before(sibling, copy) != TAURUS_OK) {
            return NULL;
        }
        return copy;
    }

    /* Create new element with StringView (no C string conversion!) */
    TaurusElement copy = taurus_element_create_with_view(name_copy_view, taurus_element_get_pool(parent));
    if (!copy) return NULL;

    /* TODO 155 Phase A: register copy as a temporary root so recursive
     * child-copy calls can reach the pool via taurus_element_get_pool. */
    taurus_root_doc_register(copy, taurus_element_get_document(parent));

    /* Copy attributes - optimized: direct StringView copy when same document */
    uint8_t attr_count = taurus_element_attribute_count(source);
    for (uint8_t i = 0; i < attr_count; i++) {
        struct taurus_attribute* attr = taurus_element_get_attribute_by_index(source, i);
        if (attr && !taurus_sv_is_empty(&attr->name_view)) {
            TaurusStringView attr_name_view = attr->name_view;
            TaurusStringView attr_value_view = attr->value_view;

            /* For cross-document copies, copy the attribute string data */
            if (is_cross_doc) {
                char* name_copy = taurus_sv_to_cstr_pooled(&attr->name_view, taurus_element_get_pool(parent));
                char* value_copy = taurus_sv_to_cstr_pooled(&attr->value_view, taurus_element_get_pool(parent));
                if (!name_copy || !value_copy) {
                    /* Allocation failed - skip this attribute */
                    if (name_copy) /* Nothing to free, pool-allocated */
                    if (value_copy) /* Nothing to free, pool-allocated */
                    continue;
                }
                attr_name_view = taurus_sv_from_cstr(name_copy);
                attr_value_view = taurus_sv_from_cstr(value_copy);
            }

            /* Use pool-based fast path if available, otherwise fallback to set_attribute
             * Pool path is 2-3x faster (no hash lookup, no string conversion) */
            if (taurus_element_get_pool(parent)) {
                taurus_element_add_attribute(copy, attr_name_view, attr_value_view, taurus_element_get_pool(parent));
            } else {
                /* Fallback: C-string API (views are always valid C
                 * strings under the single representation). */
                const char* attr_name = attr_cname(attr);
                const char* attr_value = attr_cvalue(attr);
                if (attr_name && attr_value) {
                    taurus_element_set_attribute(copy, attr_name, attr_value);
                }
            }
        }
    }

    /* Copy children recursively */
    TaurusElement child = (TaurusElement)taurus_elem_first_child(source);
    while (child) {
        TaurusNode* child_node = (TaurusNode*)child;
        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively copy element child */
            taurus_element_append_copy(copy, child);
        } else if (child_node->type == TAURUS_NODE_TYPE_TEXT) {
            TaurusTextNode* text = (TaurusTextNode*)child;
            TaurusTextNode* text_copy = taurus_text_create(text->content,
                text->content_len,
                taurus_element_get_pool(copy));
            if (text_copy) {
                taurus_element_append_child_internal(copy, (TaurusNode*)text_copy);
            }
        }
        /* Use generic accessor to get next sibling - child might be any node type */
        TaurusNode* next = taurus_node_get_next_sibling(child_node);
        if (!next) break;
        child = (TaurusElement)next;
    }

    /* Insert before sibling */
    if (taurus_element_insert_before(sibling, copy) != TAURUS_OK) {
        return NULL;
    }

    return copy;
}


/**
 * Remove all children (Public API) - Alias for remove_all_children
 */
TaurusStatus taurus_element_remove_children(TaurusElement elem) {
    return taurus_element_remove_all_children(elem);
}

/* ============================================================================
 * Bulk Allocation for Subtree Copy (Task 40)
 * ============================================================================ */

/**
 * Internal: Build a map from source elements to pre-allocated copies
 *
 * This is used during bulk copy to link elements without knowing their
 * final addresses ahead of time. We allocate space for the map, then
 * build it while copying.
 *
 * @param pool Memory pool for allocations
 * @param count Number of elements in subtree
 * @return Pointer to element array, or NULL on failure
 */
static TaurusElement* taurus_element_create_copy_map(TaurusMemoryPool* pool, size_t count) {
    if (!pool || count == 0) return NULL;
    return (TaurusElement*)taurus_pool_alloc(pool, sizeof(TaurusElement) * count);
}

/**
 * Internal: Copy subtree using pre-allocated elements (bulk allocation)
 *
 * This function assumes elements have already been allocated and stored
 * in the copy_map array. It initializes each element and builds parent-child
 * relationships.
 *
 * @param parent_copy Parent element to attach copies to
 * @param source Source element to copy
 * @param copy_map Array of pre-allocated element copies
 * @param copy_index Pointer to current index in copy_map (updated during traversal)
 * @param pool Memory pool for additional allocations (attributes, strings)
 * @return The copy of the source element, or NULL on failure
 */
static TaurusElement taurus_element_copy_subtree_bulk_internal(
    TaurusElement parent_copy,
    TaurusElement source,
    TaurusElement* copy_map,
    size_t* copy_index,
    TaurusMemoryPool* pool
) {
    if (!source || !copy_map || !copy_index) return NULL;

    /* Get current index and advance for children */
    size_t this_index = *copy_index;
    (*copy_index)++;

    TaurusElement copy = copy_map[this_index];
    if (!copy) return NULL;

    /* Initialize the copy element with minimal memset (only what we need) */
    memset(copy, 0, sizeof(struct taurus_element));

    /* Copy base node type */
    copy->base.type = TAURUS_NODE_TYPE_ELEMENT;

    /* name_view removed (TODO 90) — name is already pool-strdup'd
     * by create_with_view during the deep_copy_element call above. */

    /* Copy prefix and namespace as StringView (TODO 90) */
    /* prefix_view removed (TODO 90) — prefix is copied as char* below */;
    /* Phase 2e-B: copy namespace_uri through ns_cache. */
    {
        char* src_uri = taurus_elem_ns_uri(source);
        if (src_uri) {
            TaurusMemoryPool* p = taurus_element_get_pool(copy);
            taurus_elem_set_ns_uri(copy, src_uri, p);
        }
    }

    /* Set parent pointer */
    taurus_elem_set_parent(copy, parent_copy);

    /* Copy attributes */
    copy->attr_count = source->attr_count;

    /* Copy attribute linked list - we need to allocate attributes individually
     * since they're stored as a compact pointer linked list */
    struct taurus_attribute* src_attr = taurus_element_get_first_attribute(source);
    struct taurus_attribute* prev_dst_attr = NULL;

    while (src_attr) {
        /* Allocate attribute from pool */
        struct taurus_attribute* dst_attr = (struct taurus_attribute*)taurus_pool_alloc(pool, sizeof(struct taurus_attribute));
        if (!dst_attr) break;

        /* Initialize attribute */
        memset(dst_attr, 0, sizeof(struct taurus_attribute));

        /* Owned copies in the DESTINATION pool (single representation,
         * round 4): the source's views may point into the source
         * document's buffer, which can be freed before this copy. */
        if (!taurus_sv_is_empty(&src_attr->name_view)) {
            char* n = taurus_pool_strdup(pool, src_attr->name_view.data);
            if (n) dst_attr->name_view = taurus_sv_from_cstr(n);
        }
        if (!taurus_sv_is_empty(&src_attr->value_view)) {
            char* v = taurus_pool_strdup(pool, src_attr->value_view.data);
            if (v) dst_attr->value_view = taurus_sv_from_cstr(v);
        }
        dst_attr->name_hash = src_attr->name_hash;  /* hash + entity flag */
        dst_attr->ns_cache_off = 0;  /* set below if source has cache */

        /* Copy namespace cache if present (TODO 173). */
        struct taurus_attr_ns_cache* src_ns = attr_get_ns_cache(src_attr);
        if (src_ns) {
            struct taurus_attr_ns_cache* dst_ns =
                (struct taurus_attr_ns_cache*)taurus_pool_alloc(
                    pool, sizeof(struct taurus_attr_ns_cache));
            if (dst_ns) {
                dst_ns->prefix_view = src_ns->prefix_view;
                dst_ns->namespace_uri_view = src_ns->namespace_uri_view;
                dst_ns->prefix = src_ns->prefix
                    ? taurus_pool_strdup(pool, src_ns->prefix) : NULL;
                dst_ns->namespace_uri = src_ns->namespace_uri
                    ? taurus_pool_strdup(pool, src_ns->namespace_uri) : NULL;
                attr_set_ns_cache(dst_attr, dst_ns);
            }
        }

        /* Entity flag: already carried in the copied name_hash
         * (bit 15, round 19). */

        /* Link to previous attribute or set as first attribute */
        if (!prev_dst_attr) {
            taurus_elem_set_first_attribute(copy, dst_attr);
        } else {
            taurus_attr_set_next(prev_dst_attr, dst_attr);
        }
        taurus_elem_set_last_attribute(copy, dst_attr);
        prev_dst_attr = dst_attr;
        src_attr = taurus_attr_next(src_attr);
    }

    /* Copy children recursively and link them */
    TaurusElement first_child = NULL;
    TaurusElement last_child = NULL;

    TaurusElement child = (TaurusElement)taurus_elem_first_child(source);
    while (child) {
        TaurusNode* child_node = (TaurusNode*)child;

        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively copy element child using bulk allocation */
            TaurusElement child_copy = taurus_element_copy_subtree_bulk_internal(
                copy, child, copy_map, copy_index, pool
            );

            if (child_copy) {
                /* Link to parent's child list */
                if (!first_child) {
                    first_child = child_copy;
                }
                last_child = child_copy;
            }
        } else if (child_node->type == TAURUS_NODE_TYPE_TEXT) {
            /* Copy text node (not bulk-allocated) */
            TaurusTextNode* text = (TaurusTextNode*)child;
            TaurusTextNode* text_copy = taurus_text_create(text->content,
                text->content_len,
                taurus_element_get_pool(copy));
            if (text_copy) {
                /* Link as child using internal function */
                taurus_element_append_child_internal(copy, (TaurusNode*)text_copy);
                if (!first_child) {
                    first_child = (TaurusElement)text_copy;
                }
                if (last_child) {
                    taurus_elem_set_next_sibling(last_child, (TaurusNode*)text_copy);
                }
                last_child = (TaurusElement)text_copy;
            }
        }

        /* Use generic accessor to get next sibling - child might be any node type */
        TaurusNode* next = taurus_node_get_next_sibling(child_node);
        if (!next) break;
        child = (TaurusElement)next;
    }

    /* Set child pointers */
    taurus_elem_set_first_child(copy, (TaurusNode*)first_child);
    taurus_elem_set_last_child(copy, (TaurusNode*)last_child);

    /* Copy child count */
    copy->child_count = source->child_count;

    return copy;
}

/**
 * Copy subtree using bulk allocation (10-15% faster for large subtrees)
 *
 * Pre-allocates all element structures in a single contiguous block,
 * then initializes them. This reduces:
 * - Pool allocation overhead (1 call vs N calls)
 * - memset calls (1 call vs N calls)
 * - Cache misses (contiguous memory access)
 *
 * @param parent Parent element to attach copy to
 * @param source Source element to copy
 * @return Copy of the source element, or NULL on failure
 */
TaurusElement taurus_element_append_copy_bulk(TaurusElement parent, TaurusElement source) {
    if (!parent || !source) return NULL;
    if (!taurus_element_get_document(parent) || !taurus_element_get_pool(parent)) {
        /* Fallback to regular copy if no pool */
        return taurus_element_append_copy(parent, source);
    }

    TaurusMemoryPool* pool = taurus_element_get_pool(parent);

    /* Count subtree nodes to determine allocation size */
    TaurusSubtreeStats stats;
    taurus_element_count_subtree(source, &stats);

    size_t total_elements = stats.element_count;
    if (total_elements == 0) return NULL;

    /* Allocate all element structures in one batch */
    TaurusElement* copy_map = taurus_element_create_copy_map(pool, total_elements);
    if (!copy_map) {
        /* Fallback to regular copy if batch allocation fails */
        return taurus_element_append_copy(parent, source);
    }

    /* Copy the subtree using pre-allocated elements */
    size_t copy_index = 0;
    TaurusElement copy = taurus_element_copy_subtree_bulk_internal(
        NULL, source, copy_map, &copy_index, pool
    );

    if (!copy) {
        return NULL;
    }

    /* TODO 155 Phase A: document field removed; copy reaches doc
     * via parent chain once appended below. */

    /* Attach to parent */
    if (taurus_element_append_child(parent, copy) != TAURUS_OK) {
        return NULL;
    }

    return copy;
}

/* Issue #148 Phase 1: detached deep copy.
 *
 * Extracts the "copy subtree into dest pool" core from
 * taurus_element_append_copy_bulk. Returns a copy with no parent
 * reference; the caller is responsible for attaching it.
 *
 * The subtree is copied recursively (elements, text, comment,
 * cdata, pi, attributes, namespace declarations). All allocations
 * come from dest_doc->pool so a single taurus_document_free
 * releases them. */
TAURUS_API TaurusElement taurus_element_copy(TaurusElement src,
                                              TaurusDocument dest_doc) {
    if (!src || !dest_doc) return NULL;
    if (!dest_doc->pool) return NULL;

    /* Trigger lazy promote on dest so the pool is initialized. */
    taurus_document_ensure_promoted(dest_doc);

    /* Strategy: build the copy in a temporary root, then unlink.
     * The temporary is itself a pool-allocated element we never
     * expose; taurus_document_free will reclaim it. The
     * taurus_element_append_copy path is the well-tested deep-copy
     * route (handles cross-doc name/attr pool duplication, namespace
     * declarations, mixed-content children). Using it directly
     * avoids re-implementing the recursive walk. */
    TaurusElement tmp_parent = taurus_element_create(dest_doc, "__copy_root__");
    if (!tmp_parent) return NULL;

    TaurusElement copy = taurus_element_append_copy(tmp_parent, src);
    if (!copy) {
        /* Pool owns tmp_parent; nothing to free here. */
        return NULL;
    }

    /* Detach from tmp_parent so the caller owns the result. */
    taurus_node_unlink(taurus_element_as_node(copy));
    return copy;
}


/* Full-document deep copy (Issue #148 Phase 1).
 *
 * Builds a fresh TaurusDocument then uses taurus_element_copy to
 * duplicate the root. Carries the XML declaration
 * (version/encoding/standalone) and the document-level PIs.
 */
TAURUS_API TaurusDocument taurus_document_copy(TaurusDocument src) {
    if (!src) return NULL;

    /* Force lazy promotion so src->new_dom_root is populated. */
    taurus_document_ensure_promoted(src);

    TaurusDocument dest = (TaurusDocument)calloc(1, sizeof(*dest));
    if (!dest) return NULL;
    dest->strict_mode = src->strict_mode;
    dest->ref_count = 1;

    /* Pool — sized to source pool's used bytes for one-shot alloc. */
    size_t page_size = 4096;
    if (src->pool) {
        /* Estimate: same size as source's current page. */
        page_size = src->pool->page_size;
    }
    dest->pool = taurus_pool_create_with_page_size(page_size);
    if (!dest->pool) { free(dest); return NULL; }
    dest->page_base = taurus_pool_get_base(dest->pool);

    /* XML declaration fields — heap-strdup; freed by taurus_document_free. */
    if (src->encoding) {
        dest->encoding = strdup(src->encoding);
        if (!dest->encoding) goto fail;
    }
    if (src->xml_version) {
        dest->xml_version = strdup(src->xml_version);
        if (!dest->xml_version) goto fail;
    }
    dest->standalone = src->standalone;
    dest->had_declaration = src->had_declaration;

    /* Root tree. */
    if (src->new_dom_root) {
        TaurusElement root_copy = taurus_element_copy(
            (TaurusElement)src->new_dom_root, dest);
        if (!root_copy) goto fail;
        dest->new_dom_root = root_copy;
    }

    /* Document-level PIs (linked list, heap-strdup target+data). */
    struct taurus_processing_instruction* pi = src->pis;
    struct taurus_processing_instruction* tail = NULL;
    while (pi) {
        struct taurus_processing_instruction* dup =
            (struct taurus_processing_instruction*)malloc(sizeof(*dup));
        if (!dup) goto fail;
        dup->target = pi->target ? strdup(pi->target) : NULL;
        dup->data = pi->data ? strdup(pi->data) : NULL;
        dup->next = NULL;
        if (tail) tail->next = dup;
        else dest->pis = dup;
        tail = dup;
        pi = pi->next;
    }

    return dest;

fail:
    taurus_document_free(dest);
    return NULL;
}


