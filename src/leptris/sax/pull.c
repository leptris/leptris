/* sax/pull.c — host-driven pull (StAX-style) API over the streaming
 * SAX core (TODO.bindings/04, issue #510 Tier 2).
 *
 * C->host callbacks cost ~a microsecond each through FFI and eat the
 * streaming advantage; here the host drives: leptris_pull_next()
 * returns the next event on demand. Events are fed into an internal
 * queue in bounded input slices (256 bytes), so memory stays bounded
 * by the slice, not the document. All event strings are owned by the
 * puller and remain valid until the NEXT leptris_pull_next call. */
#include "../../include/leptris/sax/sax.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PULL_SLICE 256

typedef struct pull_event {
    LeptrisPullEventType type;
    char* name;      /* owned; element name or PI target */
    char* text;      /* owned; text/comment/CDATA/PI data or error msg */
    size_t text_len;
    /* START_ELEMENT attributes: owned flat copies [n1,v1,n2,v2...] */
    char** attrs;
    size_t attr_count;
} pull_event;

struct leptris_pull_parser {
    LeptrisSAXParser* sax;
    LeptrisSAXHandler handler;   /* the parser keeps this pointer */
    const char* input;
    size_t len;
    size_t pos;
    /* File source (TODO.engine/01): when set, input chunks are read
     * from disk into file_buf — no whole-document buffer. */
    FILE* file;
    char file_buf[PULL_SLICE];
    int finished;    /* input fully fed */
    int failed;
    pull_event* queue;
    size_t head, tail, cap;   /* ring buffer; head==tail = empty */
    pull_event current;       /* the event handed out by _next */
    /* Staged batch (#589): drained events live here — strings in one
     * arena, valid until the next batch/_next/free call. current may
     * point into the arena (current_staged=1) — queue_reset_event
     * must not free arena pointers. */
    char* stage_arena;
    size_t stage_arena_len, stage_arena_cap;
    int current_staged;
    char** stage_last_attrs;      /* most recent START's mirror */
    size_t stage_last_attr_count;
    char** mirror_attrs;          /* persistent mirror block (#589) */
    size_t mirror_cap;
};

static void queue_reset_event(pull_event* e) {
    free(e->name); free(e->text);
    if (e->attrs) {
        for (size_t i = 0; i < e->attr_count * 2; i++) free(e->attrs[i]);
        free(e->attrs);
    }
    memset(e, 0, sizeof(*e));
}

static char* pull_strdup_n(const char* s, size_t n) {
    char* c = (char*)malloc(n + 1);
    if (!c) return NULL;
    memcpy(c, s, n);
    c[n] = '\0';
    return c;
}

static pull_event* queue_push(struct leptris_pull_parser* p) {
    size_t used = (p->tail + p->cap - p->head) % p->cap;
    if (used + 1 == p->cap) {   /* full: grow */
        size_t newcap = p->cap * 2;
        pull_event* q = (pull_event*)calloc(newcap, sizeof(pull_event));
        if (!q) return NULL;
        for (size_t i = 0; i < used; i++)
            q[i] = p->queue[(p->head + i) % p->cap];
        free(p->queue);
        p->queue = q;
        p->head = 0;
        p->tail = used;
        p->cap = newcap;
    }
    pull_event* e = &p->queue[p->tail];
    memset(e, 0, sizeof(*e));
    p->tail = (p->tail + 1) % p->cap;
    return e;
}

static int queue_empty(struct leptris_pull_parser* p) {
    return p->head == p->tail;
}

/* ---- SAX callbacks: copy everything into the queue ---- */

static void cb_start_element(void* ud, const char* name, const char** attrs) {
    struct leptris_pull_parser* p = (struct leptris_pull_parser*)ud;
    pull_event* e = queue_push(p);
    if (!e) return;
    e->type = LEPTRIS_PULL_START_ELEMENT;
    e->name = pull_strdup_n(name, strlen(name));
    if (attrs) {
        size_t n = 0;
        while (attrs[n]) n += 2;   /* flat [name, value, ..., NULL] */
        e->attr_count = n / 2;
        e->attrs = (char**)calloc(n + 1, sizeof(char*));
        if (e->attrs) {
            for (size_t i = 0; i < n; i++)
                e->attrs[i] = pull_strdup_n(attrs[i] ? attrs[i] : "",
                                            attrs[i] ? strlen(attrs[i]) : 0);
        }
    }
}

static void cb_end_element(void* ud, const char* name) {
    struct leptris_pull_parser* p = (struct leptris_pull_parser*)ud;
    pull_event* e = queue_push(p);
    if (!e) return;
    e->type = LEPTRIS_PULL_END_ELEMENT;
    e->name = pull_strdup_n(name, strlen(name));
}

static void cb_characters(void* ud, const char* text, size_t len) {
    struct leptris_pull_parser* p = (struct leptris_pull_parser*)ud;
    pull_event* e = queue_push(p);
    if (!e) return;
    e->type = LEPTRIS_PULL_TEXT;
    e->text = pull_strdup_n(text, len);
    e->text_len = len;
}

static void cb_comment(void* ud, const char* comment) {
    struct leptris_pull_parser* p = (struct leptris_pull_parser*)ud;
    pull_event* e = queue_push(p);
    if (!e) return;
    e->type = LEPTRIS_PULL_COMMENT;
    e->text = pull_strdup_n(comment, strlen(comment));
    e->text_len = strlen(comment);
}

static void cb_cdata(void* ud, const char* cdata) {
    struct leptris_pull_parser* p = (struct leptris_pull_parser*)ud;
    pull_event* e = queue_push(p);
    if (!e) return;
    e->type = LEPTRIS_PULL_CDATA;
    e->text = pull_strdup_n(cdata, strlen(cdata));
    e->text_len = strlen(cdata);
}

static void cb_pi(void* ud, const char* target, const char* data) {
    struct leptris_pull_parser* p = (struct leptris_pull_parser*)ud;
    pull_event* e = queue_push(p);
    if (!e) return;
    e->type = LEPTRIS_PULL_PI;
    e->name = pull_strdup_n(target, strlen(target));
    if (data) {
        e->text = pull_strdup_n(data, strlen(data));
        e->text_len = strlen(data);
    }
}

static void cb_end_document(void* ud) {
    struct leptris_pull_parser* p = (struct leptris_pull_parser*)ud;
    pull_event* e = queue_push(p);
    if (!e) return;
    e->type = LEPTRIS_PULL_END_DOCUMENT;
}

static void cb_start_prefix(void* ud, const char* prefix, const char* uri) {
    struct leptris_pull_parser* p = (struct leptris_pull_parser*)ud;
    pull_event* e = queue_push(p);
    if (!e) return;
    e->type = LEPTRIS_PULL_START_PREFIX;
    e->name = pull_strdup_n(prefix ? prefix : "",
                            prefix ? strlen(prefix) : 0);
    e->text = pull_strdup_n(uri ? uri : "", uri ? strlen(uri) : 0);
    e->text_len = uri ? strlen(uri) : 0;
}

static void cb_end_prefix(void* ud, const char* prefix) {
    struct leptris_pull_parser* p = (struct leptris_pull_parser*)ud;
    pull_event* e = queue_push(p);
    if (!e) return;
    e->type = LEPTRIS_PULL_END_PREFIX;
    e->name = pull_strdup_n(prefix ? prefix : "",
                            prefix ? strlen(prefix) : 0);
}

static void cb_error(void* ud, const char* message, int line, int column) {
    (void)line; (void)column;
    struct leptris_pull_parser* p = (struct leptris_pull_parser*)ud;
    pull_event* e = queue_push(p);
    if (!e) return;
    e->type = LEPTRIS_PULL_ERROR;
    e->text = pull_strdup_n(message, strlen(message));
    p->failed = 1;
}

/* ---- Public API ---- */

static struct leptris_pull_parser* pull_alloc(void) {
    struct leptris_pull_parser* p =
        (struct leptris_pull_parser*)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->cap = 32;
    p->queue = (pull_event*)calloc(p->cap, sizeof(pull_event));
    if (!p->queue) { free(p); return NULL; }

    p->handler.start_element = cb_start_element;
    p->handler.end_element = cb_end_element;
    p->handler.characters = cb_characters;
    p->handler.comment = cb_comment;
    p->handler.cdata = cb_cdata;
    p->handler.processing_instruction = cb_pi;
    p->handler.start_prefix_mapping = cb_start_prefix;
    p->handler.end_prefix_mapping = cb_end_prefix;
    p->handler.end_document = cb_end_document;
    p->handler.error = cb_error;

    p->sax = leptris_sax_parser_create(&p->handler, p);
    if (!p->sax) { free(p->queue); free(p); return NULL; }
    /* Events must fire per feed() call, not buffered to is_final. */
    leptris_sax_parser_set_streaming(p->sax, 1);
    return p;
}

/* TODO.engine/01: shared construction — memory source. */

/* TODO.engine/01: shared construction — memory source. */
LEPTRIS_API LeptrisPullParser leptris_pull_new(const char* xml, size_t len) {
    if (!xml || len == 0) return NULL;
    struct leptris_pull_parser* p = pull_alloc();
    if (!p) return NULL;
    p->input = xml;
    p->len = len;
    return p;
}

/* TODO.engine/01: file source — chunks stream off disk. */
LEPTRIS_API LeptrisPullParser leptris_pull_new_file(const char* path) {
    if (!path || !*path) return NULL;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    struct leptris_pull_parser* p = pull_alloc();
    if (!p) { fclose(f); return NULL; }
    p->file = f;
    /* The memory path is unused; keep the invariant pos==len==0. */
    return p;
}

static void stage_reset(struct leptris_pull_parser* p);

LEPTRIS_API const LeptrisPullEvent* leptris_pull_next(LeptrisPullParser pull) {
    if (!pull) return NULL;
    /* A staged batch may still hold current — drop that reference
     * WITHOUT clearing the stage (a batch loop drives _next; the
     * arena must live across the whole batch). */
    if (pull->current_staged) {
        memset(&pull->current, 0, sizeof(pull->current));
        pull->current_staged = 0;
    }

    /* Feed more input while the queue is empty and input remains. */
    while (queue_empty(pull) && !pull->finished && !pull->failed) {
        const char* chunk_ptr;
        size_t chunk;
        int is_final;

        if (pull->file) {
            /* File source (TODO.engine/01): stream the next slice. */
            chunk = fread(pull->file_buf, 1, PULL_SLICE, pull->file);
            chunk_ptr = pull->file_buf;
            is_final = (chunk < PULL_SLICE);
        } else {
            chunk = pull->len - pull->pos;
            if (chunk > PULL_SLICE) chunk = PULL_SLICE;
            chunk_ptr = pull->input + pull->pos;
            pull->pos += chunk;
            is_final = pull->pos == pull->len;
        }
        int rc = leptris_sax_parser_feed(pull->sax, chunk_ptr,
                                         chunk, is_final);
        if (is_final) pull->finished = 1;
        if (rc != 0) break;
        if (pull->failed) break;
    }

    if (queue_empty(pull)) return NULL;   /* exhausted */

    queue_reset_event(&pull->current);
    pull->current = pull->queue[pull->head];
    memset(&pull->queue[pull->head], 0, sizeof(pull_event));
    pull->head = (pull->head + 1) % pull->cap;
    return (const LeptrisPullEvent*)&pull->current;
}

LEPTRIS_API size_t leptris_pull_attr_count(LeptrisPullParser pull) {
    if (!pull || pull->current.type != LEPTRIS_PULL_START_ELEMENT)
        return 0;
    return pull->current.attr_count;
}

LEPTRIS_API const char* leptris_pull_attr_name(LeptrisPullParser pull,
                                               size_t index) {
    if (!pull || pull->current.type != LEPTRIS_PULL_START_ELEMENT ||
        index >= pull->current.attr_count)
        return NULL;
    return pull->current.attrs[index * 2];
}

LEPTRIS_API const char* leptris_pull_attr_value(LeptrisPullParser pull,
                                                size_t index) {
    if (!pull || pull->current.type != LEPTRIS_PULL_START_ELEMENT ||
        index >= pull->current.attr_count)
        return NULL;
    return pull->current.attrs[index * 2 + 1];
}

/* ---- Batched delivery (#589) ---- */


static char* stage_put(struct leptris_pull_parser* p, const char* s,
                       size_t len) {
    if (p->stage_arena_len + len + 1 > p->stage_arena_cap) {
        size_t want = p->stage_arena_cap ? p->stage_arena_cap * 2 : 256;
        while (want < p->stage_arena_len + len + 1) want *= 2;
        char* grown = (char*)realloc(p->stage_arena, want);
        if (!grown) return NULL;
        p->stage_arena = grown;
        p->stage_arena_cap = want;
    }
    if (!s || len == 0) {
        p->stage_arena[p->stage_arena_len] = '\0';
        return p->stage_arena + p->stage_arena_len++;
    }
    memcpy(p->stage_arena + p->stage_arena_len, s, len);
    char* at = p->stage_arena + p->stage_arena_len;
    p->stage_arena_len += len;
    p->stage_arena[p->stage_arena_len++] = '\0';
    return at;
}

static void stage_reset(struct leptris_pull_parser* p) {
    if (p->current_staged) {
        /* current points into the arena — clear without freeing. */
        memset(&p->current, 0, sizeof(p->current));
        p->current_staged = 0;
    }
    p->stage_arena_len = 0;
}

LEPTRIS_API size_t leptris_pull_next_batch(LeptrisPullParser pull,
                                           LeptrisPullEvent* out_events,
                                           size_t max_count) {
    if (!pull || !out_events || max_count == 0) return 0;
    stage_reset(pull);
    size_t n = 0;
    /* Attr accessors serve the batch's most recent START element —
     * remembered during the loop, re-pointed AFTER it (the internal
     * _next calls replace current each step). */
    LeptrisPullEvent* last_start_out = NULL;
    while (n < max_count) {
        const LeptrisPullEvent* e = leptris_pull_next(pull);
        if (!e) break;
        LeptrisPullEvent* out = &out_events[n];
        out->type = e->type;
        out->name = NULL;
        out->text = NULL;
        out->text_len = e->text_len;
        if (e->name) {
            char* staged = stage_put(pull, e->name, strlen(e->name));
            if (!staged) break;
            out->name = staged;
        }
        if (e->text) {
            char* staged = stage_put(pull, e->text, e->text_len);
            if (!staged) break;
            out->text = staged;
        }
        n++;
        /* Attr accessors serve the most recent START element of the
         * batch: mirror each start's attrs into the stage as it
         * passes (the previous mirror is replaced). */
        if (e->type == LEPTRIS_PULL_START_ELEMENT &&
            pull->current.attr_count) {
            /* Mirror into ONE persistent block (grown as needed, freed
             * with the parser — no per-start calloc/free churn). */
            size_t na = pull->current.attr_count;
            if (na * 2 + 1 > pull->mirror_cap) {
                size_t want = pull->mirror_cap ? pull->mirror_cap : 8;
                while (want < na * 2 + 1) want *= 2;
                char** grown = (char**)realloc(pull->mirror_attrs,
                                               want * sizeof(char*));
                if (grown) {
                    pull->mirror_attrs = grown;
                    pull->mirror_cap = want;
                }
            }
            if (na * 2 + 1 <= pull->mirror_cap) {
                int ok = 1;
                for (size_t i = 0; i < na * 2 && ok; i++) {
                    const char* a = pull->current.attrs[i];
                    char* st = a ? stage_put(pull, a, strlen(a)) : NULL;
                    if (a && !st) ok = 0;
                    pull->mirror_attrs[i] = st;
                }
                if (ok) {
                    last_start_out = out;
                    pull->stage_last_attrs = pull->mirror_attrs;
                    pull->stage_last_attr_count = na;
                }
            }
        }
    }
    if (last_start_out) {
        /* Free the last-drained event's owned strings before the
         * mirror overwrites current — they are queue-allocated
         * (Linux LSan, PR #597). */
        if (!pull->current_staged) queue_reset_event(&pull->current);
        memset(&pull->current, 0, sizeof(pull->current));
        pull->current.type = LEPTRIS_PULL_START_ELEMENT;
        pull->current.name = last_start_out->name;
        pull->current.attrs = pull->stage_last_attrs;
        pull->current.attr_count = pull->stage_last_attr_count;
        pull->current_staged = 1;
    }
    return n;
}

/* Flat attribute fetch (#562). */
LEPTRIS_API size_t leptris_pull_attrs(LeptrisPullParser pull,
                                      const char** attrs,
                                      size_t max_count) {
    if (!pull || pull->current.type != LEPTRIS_PULL_START_ELEMENT)
        return 0;
    size_t pairs = pull->current.attr_count;
    if (!attrs) return pairs;
    size_t copy = pairs < max_count ? pairs : max_count;
    for (size_t i = 0; i < copy; i++) {
        attrs[2 * i] = pull->current.attrs[2 * i];
        attrs[2 * i + 1] = pull->current.attrs[2 * i + 1];
    }
    return pairs;
}

LEPTRIS_API void leptris_pull_free(LeptrisPullParser pull) {
    if (!pull) return;
    if (pull->file) fclose(pull->file);
    if (pull->sax) leptris_sax_parser_free(pull->sax);
    if (!pull->current_staged) queue_reset_event(&pull->current);
    free(pull->stage_arena);
    free(pull->mirror_attrs);
    for (size_t i = pull->head; i != pull->tail; i = (i + 1) % pull->cap)
        queue_reset_event(&pull->queue[i]);
    free(pull->queue);
    free(pull);
}
