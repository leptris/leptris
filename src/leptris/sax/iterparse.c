/* sax/iterparse.c — bounded-memory incremental tree iteration
 * (TODO.bindings/02, issue #510 Tier 2; issue #586 v2).
 *
 * Rides the pull API: events build one in-flight document. v1 yields
 * each TOP-LEVEL child of the document root. v2 can yield EVERY
 * element as it completes (post-order): child before parent, with the
 * completed subtree still attached to the parent. Each yielded handle
 * is valid until the next next()/free call; the old handle may be
 * detached/released as the iterator advances. Peak memory is bounded
 * by the live open tree + one yielded subtree, not the whole input.
 *
 * Namespace support comes from the pull parser's prefix-mapping
 * events. The iterator keeps an in-scope prefix stack and snapshots
 * the scope for the last yielded element (bulk lookups for FFI hosts). */
#include "../../include/leptris.h"
#include "../../include/leptris/sax/sax.h"
#include "../dom/element.h"   /* leptris_element_set_prefix (internal) */
#include "../memory/arena.h"  /* subtree arena reuse (#563) */
#include <stdlib.h>
#include <string.h>

extern struct leptris_document* leptris_document_create_on_arena(
    LeptrisArena*);

#define IT_MAX_DEPTH 4096
#define IT_SUBTREE_ARENA_BYTES 16384

typedef struct ns_binding {
    char* prefix;                 /* "" for default */
    char* uri;
    int depth;                    /* element depth where declared */
    struct ns_binding* prev;
} ns_binding;

typedef struct ns_snapshot {
    char* prefix;
    char* uri;
} ns_snapshot;

struct leptris_iterparse {
    LeptrisPullParser pull;
    LeptrisDocument doc;          /* pool of the current top-level subtree */
    LeptrisDocument root_doc;     /* full mode: the root element's own pool */
    LeptrisArena* sub_arena;      /* reused across subtrees (#563) */
    LeptrisElement stack[IT_MAX_DEPTH];
    int depth;                    /* open element depth (root=1) */
    LeptrisIterparseMode mode;
    char* text_buf;
    size_t text_len, text_cap;
    LeptrisElement done;          /* completed element handed out */
    int done_depth;
    int exhausted;
    char* error_msg;
    ns_binding* ns;
    ns_snapshot* snap;
    size_t snap_count, snap_cap;
};

static char* it_strdup_n(const char* s, size_t n) {
    char* c = (char*)malloc(n + 1);
    if (!c) return NULL;
    memcpy(c, s, n);
    c[n] = '\0';
    return c;
}

static void it_clear_snapshot(struct leptris_iterparse* it) {
    for (size_t i = 0; i < it->snap_count; i++) {
        free(it->snap[i].prefix);
        free(it->snap[i].uri);
    }
    it->snap_count = 0;
}

static int snap_seen(struct leptris_iterparse* it, const char* prefix) {
    for (size_t i = 0; i < it->snap_count; i++)
        if (strcmp(it->snap[i].prefix, prefix) == 0) return 1;
    return 0;
}

static void it_snapshot_scope(struct leptris_iterparse* it) {
    it_clear_snapshot(it);
    for (ns_binding* b = it->ns; b; b = b->prev) {
        if (snap_seen(it, b->prefix)) continue;
        if (it->snap_count == it->snap_cap) {
            size_t cap = it->snap_cap ? it->snap_cap * 2 : 8;
            ns_snapshot* s = (ns_snapshot*)realloc(it->snap,
                                                   cap * sizeof(*s));
            if (!s) return;
            it->snap = s;
            it->snap_cap = cap;
        }
        it->snap[it->snap_count].prefix =
            it_strdup_n(b->prefix, strlen(b->prefix));
        it->snap[it->snap_count].uri = it_strdup_n(b->uri, strlen(b->uri));
        it->snap_count++;
    }
}

static const char* scope_lookup(struct leptris_iterparse* it,
                                const char* prefix) {
    const char* p = prefix ? prefix : "";
    for (ns_binding* b = it->ns; b; b = b->prev)
        if (strcmp(b->prefix, p) == 0) return b->uri;
    return NULL;
}

static void push_ns(struct leptris_iterparse* it,
                    const char* prefix, const char* uri) {
    ns_binding* b = (ns_binding*)calloc(1, sizeof(*b));
    if (!b) return;
    b->prefix = it_strdup_n(prefix ? prefix : "", prefix ? strlen(prefix) : 0);
    b->uri = it_strdup_n(uri ? uri : "", uri ? strlen(uri) : 0);
    b->depth = it->depth + 1;  /* applies to the upcoming start element */
    b->prev = it->ns;
    it->ns = b;
}

static void pop_ns_depth(struct leptris_iterparse* it, int depth) {
    while (it->ns && it->ns->depth >= depth) {
        ns_binding* b = it->ns;
        it->ns = b->prev;
        free(b->prefix);
        free(b->uri);
        free(b);
    }
}

static void set_error(struct leptris_iterparse* it, const char* msg) {
    if (it->error_msg) return;
    it->error_msg = it_strdup_n(msg ? msg : "parse error",
                                msg ? strlen(msg) : 11);
}

static void it_flush_text(struct leptris_iterparse* it) {
    /* Text must be created in its PARENT's pool. At depth 1 the
     * parent is the document root (root_doc, full mode only); in v1
     * mode there is no materialized parent — the text is dropped,
     * exactly as before. */
    int root_level = (it->depth == 1);
    if (it->text_len == 0 || it->depth < 1 ||
        (root_level && !it->root_doc)) {
        it->text_len = 0;
        return;
    }
    LeptrisDocument pool_doc = root_level ? it->root_doc : it->doc;
    it->text_buf[it->text_len] = '\0';
    LeptrisNodeRef t = leptris_text_node_create(pool_doc, it->text_buf);
    if (t) {
        leptris_element_append_child(it->stack[it->depth - 1],
                                     (LeptrisElement)t);
    }
    it->text_len = 0;
}

static void it_append_text(struct leptris_iterparse* it,
                           const char* text, size_t len) {
    if (it->text_len + len + 1 > it->text_cap) {
        size_t cap = it->text_cap ? it->text_cap * 2 : 128;
        while (cap < it->text_len + len + 1) cap *= 2;
        char* grown = (char*)realloc(it->text_buf, cap);
        if (!grown) return;
        it->text_buf = grown;
        it->text_cap = cap;
    }
    memcpy(it->text_buf + it->text_len, text, len);
    it->text_len += len;
}

static LeptrisIterparse it_new_common(LeptrisPullParser pull,
                                      LeptrisIterparseMode mode) {
    if (!pull) return NULL;
    struct leptris_iterparse* it =
        (struct leptris_iterparse*)calloc(1, sizeof(*it));
    if (!it) { leptris_pull_free(pull); return NULL; }
    it->pull = pull;
    it->mode = mode;
    it->sub_arena = leptris_arena_create(IT_SUBTREE_ARENA_BYTES);
    return it;
}

LEPTRIS_API LeptrisIterparse leptris_iterparse_new(const char* xml,
                                                    size_t len) {
    return leptris_iterparse_new_ex(xml, len, LEPTRIS_ITERPARSE_TOP_LEVEL);
}

LEPTRIS_API LeptrisIterparse leptris_iterparse_new_ex(
        const char* xml, size_t len, LeptrisIterparseMode mode) {
    if (!xml || len == 0) return NULL;
    return it_new_common(leptris_pull_new(xml, len), mode);
}

LEPTRIS_API LeptrisIterparse leptris_iterparse_new_file(const char* path) {
    return leptris_iterparse_new_file_ex(path, LEPTRIS_ITERPARSE_TOP_LEVEL);
}

LEPTRIS_API LeptrisIterparse leptris_iterparse_new_file_ex(
        const char* path, LeptrisIterparseMode mode) {
    if (!path || !*path) return NULL;
    return it_new_common(leptris_pull_new_file(path), mode);
}

/* Advance past the last yield. When a completed TOP-LEVEL child was
 * handed out, its whole subtree document is released here — the v1
 * bounded-memory contract: peak = largest top-level subtree. */
static void release_done(struct leptris_iterparse* it) {
    if (!it->done) return;
    if (it->done_depth <= 2) {
        it->done = NULL;
        if (it->doc) {
            leptris_document_free(it->doc);
            it->doc = NULL;
            /* #563: bump-reset the reused subtree arena (or grow it
             * when the child nearly filled the span). */
            if (it->sub_arena) {
                if (leptris_arena_remaining(it->sub_arena) <
                        it->sub_arena->size / 4) {
                    size_t want = it->sub_arena->size * 2;
                    leptris_arena_destroy(it->sub_arena);
                    it->sub_arena = leptris_arena_create(want);
                } else {
                    leptris_arena_reset(it->sub_arena);
                }
            }
        }
        it->depth = 1;
        return;
    }
    it->done = NULL;
}

static void attach_namespaces(struct leptris_iterparse* it, LeptrisElement e,
                              const char* prefix) {
    if (prefix) {
        const char* uri = scope_lookup(it, prefix);
        if (uri) leptris_element_add_namespace_definition(e, prefix, uri);
    } else {
        const char* duri = scope_lookup(it, "");
        if (duri) leptris_element_set_default_namespace(e, duri);
    }
    /* Declare the element's OWN xmlns attributes so the yielded
     * element carries its declarations (visibility for consumers). */
    for (ns_binding* b = it->ns; b; b = b->prev) {
        if (b->depth != it->depth + 1) continue;
        if (b->prefix[0])
            leptris_element_add_namespace_definition(e, b->prefix, b->uri);
        else
            leptris_element_set_default_namespace(e, b->uri);
    }
}

static LeptrisElement handle_start(struct leptris_iterparse* it,
                                   const LeptrisPullEvent* ev) {
    it_flush_text(it);
    /* Document-root element: materialized ONLY in full mode, in
     * its OWN pool — top-level children come and go in separate
     * subtree documents, so the root must outlive them all (it
     * yields childless at document end). v1 keeps the root
     * immaterial, exactly as before. */
    if (it->depth == 0) {
        if (it->mode != LEPTRIS_ITERPARSE_FULL_DOCUMENT) {
            it->depth = 1;   /* root marker, stack[0] unused (v1) */
            return NULL;
        }
        if (!it->root_doc) {
            it->root_doc = leptris_document_create();
            if (!it->root_doc) { it->exhausted = 1; return NULL; }
        }
    }
    /* depth==1: a new top-level child — a fresh document over the
     * REUSED arena (bump-reset at release; grown when a child nearly
     * fills the span). Arena creation failure falls back to the
     * plain 32 KB-page document. */
    if (it->depth == 1 && !it->doc) {
        it->doc = it->sub_arena
            ? leptris_document_create_on_arena(it->sub_arena)
            : leptris_document_create();
        if (!it->doc) { it->exhausted = 1; return NULL; }
    }
    /* QName split: create with the LOCAL name; the prefix is set
     * after (pool-copied by the setter), matching the DOM parser's
     * dp_split_hash_name semantics. */
    const char* qn = ev->name ? ev->name : "";
    const char* colon = strchr(qn, ':');
    const char* local = colon ? colon + 1 : qn;
    char prefix_buf[128];
    const char* prefix = NULL;
    if (colon) {
        size_t plen = (size_t)(colon - qn);
        if (plen >= sizeof(prefix_buf)) plen = sizeof(prefix_buf) - 1;
        memcpy(prefix_buf, qn, plen);
        prefix_buf[plen] = '\0';
        prefix = prefix_buf;
    }
    LeptrisDocument target =
        (it->depth == 0) ? it->root_doc : it->doc;
    LeptrisElement e = leptris_element_create(target, local);
    if (!e) { it->exhausted = 1; return NULL; }
    if (prefix) leptris_element_set_prefix(e, prefix);
    attach_namespaces(it, e, prefix);
    size_t na = leptris_pull_attr_count(it->pull);
    for (size_t i = 0; i < na; i++) {
        leptris_element_set_attribute(e, leptris_pull_attr_name(it->pull, i),
                                      leptris_pull_attr_value(it->pull, i));
    }
    if (it->depth == 0) {
        leptris_document_set_root(it->root_doc, e);
    } else if (it->depth == 1) {
        /* A top-level child is the ROOT of its own subtree document —
         * never attached to the document root (that would link two
         * pools; freeing the subtree would leave the root's child
         * chain dangling — the ASAN UAF in PR #590). */
        leptris_document_set_root(it->doc, e);
    } else {
        leptris_element_append_child(it->stack[it->depth - 1], e);
    }
    if (it->depth < IT_MAX_DEPTH) it->stack[it->depth] = e;
    it->depth++;
    return e;
}

LEPTRIS_API LeptrisElement leptris_iterparse_next(LeptrisIterparse it) {
    if (!it || it->exhausted) return NULL;
    release_done(it);

    const LeptrisPullEvent* ev;
    while ((ev = leptris_pull_next(it->pull)) != NULL) {
        switch (ev->type) {
            case LEPTRIS_PULL_START_PREFIX:
                push_ns(it, ev->name, ev->text ? ev->text : "");
                break;
            case LEPTRIS_PULL_END_PREFIX:
                /* Pop by end-element depth instead; streaming.c emits
                 * end-prefix events after end_element, when the scope
                 * has already been yielded. */
                break;
            case LEPTRIS_PULL_START_ELEMENT:
                handle_start(it, ev);
                break;
            case LEPTRIS_PULL_TEXT:
            case LEPTRIS_PULL_CDATA:
                it_append_text(it, ev->text, ev->text_len);
                break;
            case LEPTRIS_PULL_END_ELEMENT: {
                it_flush_text(it);
                if (it->depth <= 0) break;
                if (it->depth == 1 && !it->root_doc) {
                    /* v1: the root was never materialized — nothing
                     * to yield or pop for its end tag. */
                    it->depth--;
                    break;
                }
                LeptrisElement complete = it->stack[it->depth - 1];
                int complete_depth = it->depth;
                it_snapshot_scope(it);
                it->depth--;
                pop_ns_depth(it, complete_depth);
                int yield = (it->mode == LEPTRIS_ITERPARSE_FULL_DOCUMENT) ||
                            (complete_depth == 2);
                if (yield) {
                    it->done = complete;
                    it->done_depth = complete_depth;
                    return it->done;
                }
                break;
            }
            case LEPTRIS_PULL_ERROR:
                set_error(it, ev->text);
                it->exhausted = 1;
                return NULL;
            case LEPTRIS_PULL_END_DOCUMENT:
                it->exhausted = 1;
                if (!it->error_msg && it->depth != 0)
                    set_error(it, "truncated XML document");
                return NULL;
            default:
                it_flush_text(it);
                break;
        }
    }
    it->exhausted = 1;
    if (!it->error_msg && it->depth != 0)
        set_error(it, "truncated XML document");
    return NULL;
}

LEPTRIS_API const char* leptris_iterparse_ns_uri(LeptrisIterparse it,
                                                 const char* prefix) {
    if (!it) return NULL;
    const char* p = prefix ? prefix : "";
    for (size_t i = 0; i < it->snap_count; i++)
        if (strcmp(it->snap[i].prefix, p) == 0) return it->snap[i].uri;
    return NULL;
}

LEPTRIS_API size_t leptris_iterparse_ns_count(LeptrisIterparse it) {
    return it ? it->snap_count : 0;
}

LEPTRIS_API const char* leptris_iterparse_error(LeptrisIterparse it) {
    return it ? it->error_msg : NULL;
}

LEPTRIS_API void leptris_iterparse_free(LeptrisIterparse it) {
    if (!it) return;
    if (it->pull) leptris_pull_free(it->pull);
    if (it->doc) leptris_document_free(it->doc);
    if (it->root_doc) leptris_document_free(it->root_doc);
    if (it->sub_arena) leptris_arena_destroy(it->sub_arena);
    free(it->text_buf);
    free(it->error_msg);
    it_clear_snapshot(it);
    free(it->snap);
    while (it->ns) {
        ns_binding* b = it->ns;
        it->ns = b->prev;
        free(b->prefix);
        free(b->uri);
        free(b);
    }
    free(it);
}
