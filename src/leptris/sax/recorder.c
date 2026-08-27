/* sax/recorder.c — chunked SAX event delivery (issue #585).
 *
 * A handler on the same streaming state machine as the callback API
 * (streaming.c is untouched): instead of dispatching per event, every
 * callback appends one fixed-size record and its strings into a
 * packed arena. The host drains records + arena in two bulk reads per
 * fed chunk — through FFI, per-event callback dispatch cost more
 * than the parse itself (leptris SAX measured SLOWER than Nokogiri
 * SAX despite the faster engine).
 *
 * Chunk protocol: feed() resets both buffers, parses the chunk, and
 * the accumulated events stay readable until the next feed. */
#include "../../include/leptris/sax/sax.h"
#include "sax_internal.h"
#include "../../include/leptris.h"
#include <stdlib.h>
#include <string.h>

typedef struct leptris_sax_recorder {
    LeptrisSAXParser* parser;
    LeptrisSAXHandler handler;
    LeptrisSaxEventRecord* recs;
    size_t rec_count, rec_cap;
    char* arena;
    size_t arena_len, arena_cap;
} leptris_sax_recorder;

static uint32_t arena_put(leptris_sax_recorder* r, const char* s,
                          size_t len, uint32_t* out_len) {
    if (r->arena_len + len + 1 > r->arena_cap) {
        size_t want = r->arena_cap ? r->arena_cap * 2 : 512;
        while (want < r->arena_len + len + 1) want *= 2;
        char* na = (char*)realloc(r->arena, want);
        if (!na) { *out_len = 0; return 0; }
        r->arena = na;
        r->arena_cap = want;
    }
    memcpy(r->arena + r->arena_len, s, len);
    r->arena[r->arena_len + len] = '\0';
    uint32_t off = (uint32_t)r->arena_len;
    r->arena_len += len + 1;
    *out_len = (uint32_t)len;
    return off;
}

static LeptrisSaxEventRecord* rec_new(leptris_sax_recorder* r) {
    if (r->rec_count == r->rec_cap) {
        size_t want = r->rec_cap ? r->rec_cap * 2 : 64;
        LeptrisSaxEventRecord* nr =
            (LeptrisSaxEventRecord*)realloc(r->recs, want * sizeof(*nr));
        if (!nr) return NULL;
        r->recs = nr;
        r->rec_cap = want;
    }
    LeptrisSaxEventRecord* rec = &r->recs[r->rec_count++];
    memset(rec, 0, sizeof(*rec));
    return rec;
}

static void cb_start_document(void* ud) {
    LeptrisSaxEventRecord* e = rec_new((leptris_sax_recorder*)ud);
    if (e) e->kind = LEPTRIS_SAX_EVENT_START_DOCUMENT;
}

static void cb_end_document(void* ud) {
    LeptrisSaxEventRecord* e = rec_new((leptris_sax_recorder*)ud);
    if (e) e->kind = LEPTRIS_SAX_EVENT_END_DOCUMENT;
}

static void cb_start_element(void* ud, const char* name,
                             const char** attrs) {
    leptris_sax_recorder* r = (leptris_sax_recorder*)ud;
    LeptrisSaxEventRecord* e = rec_new(r);
    if (!e) return;
    e->kind = LEPTRIS_SAX_EVENT_START_ELEMENT;
    uint32_t l = 0;
    e->name_off = arena_put(r, name, strlen(name), &l);
    e->name_len = l;
    /* Pack the attribute pairs first, then reference the block. */
    size_t pairs = 0;
    for (const char** a = attrs; a && *a; a += 2) pairs++;
    if (pairs) {
        size_t start = r->arena_len;
        for (size_t i = 0; i < pairs; i++) {
            uint32_t dl = 0;
            arena_put(r, attrs[2 * i], strlen(attrs[2 * i]), &dl);
            arena_put(r, attrs[2 * i + 1],
                      attrs[2 * i + 1] ? strlen(attrs[2 * i + 1]) : 0,
                      &dl);
        }
        e->attrs_off = (uint32_t)start;
        e->attr_count = (uint32_t)pairs;
    }
}

static void cb_end_element(void* ud, const char* name) {
    leptris_sax_recorder* r = (leptris_sax_recorder*)ud;
    LeptrisSaxEventRecord* e = rec_new(r);
    if (!e) return;
    e->kind = LEPTRIS_SAX_EVENT_END_ELEMENT;
    uint32_t l = 0;
    e->name_off = arena_put(r, name, strlen(name), &l);
    e->name_len = l;
}

static void cb_characters(void* ud, const char* text, size_t len) {
    leptris_sax_recorder* r = (leptris_sax_recorder*)ud;
    LeptrisSaxEventRecord* e = rec_new(r);
    if (!e) return;
    e->kind = LEPTRIS_SAX_EVENT_CHARACTERS;
    uint32_t l = 0;
    e->text_off = arena_put(r, text, len, &l);
    e->text_len = l;
}

static void cb_comment(void* ud, const char* comment) {
    leptris_sax_recorder* r = (leptris_sax_recorder*)ud;
    LeptrisSaxEventRecord* e = rec_new(r);
    if (!e) return;
    e->kind = LEPTRIS_SAX_EVENT_COMMENT;
    uint32_t l = 0;
    e->text_off = arena_put(r, comment, strlen(comment), &l);
    e->text_len = l;
}

static void cb_cdata(void* ud, const char* cdata) {
    leptris_sax_recorder* r = (leptris_sax_recorder*)ud;
    LeptrisSaxEventRecord* e = rec_new(r);
    if (!e) return;
    e->kind = LEPTRIS_SAX_EVENT_CDATA;
    uint32_t l = 0;
    e->text_off = arena_put(r, cdata, strlen(cdata), &l);
    e->text_len = l;
}

static void cb_pi(void* ud, const char* target, const char* data) {
    leptris_sax_recorder* r = (leptris_sax_recorder*)ud;
    LeptrisSaxEventRecord* e = rec_new(r);
    if (!e) return;
    e->kind = LEPTRIS_SAX_EVENT_PI;
    uint32_t l = 0;
    e->name_off = arena_put(r, target, strlen(target), &l);
    e->name_len = l;
    e->text_off = arena_put(r, data ? data : "", data ? strlen(data) : 0, &l);
    e->text_len = l;
}

static void cb_start_prefix(void* ud, const char* prefix, const char* uri) {
    leptris_sax_recorder* r = (leptris_sax_recorder*)ud;
    LeptrisSaxEventRecord* e = rec_new(r);
    if (!e) return;
    e->kind = LEPTRIS_SAX_EVENT_START_PREFIX;
    uint32_t l = 0;
    e->name_off = arena_put(r, prefix, strlen(prefix), &l);
    e->name_len = l;
    e->text_off = arena_put(r, uri, strlen(uri), &l);
    e->text_len = l;
}

static void cb_end_prefix(void* ud, const char* prefix) {
    leptris_sax_recorder* r = (leptris_sax_recorder*)ud;
    LeptrisSaxEventRecord* e = rec_new(r);
    if (!e) return;
    e->kind = LEPTRIS_SAX_EVENT_END_PREFIX;
    uint32_t l = 0;
    e->name_off = arena_put(r, prefix, strlen(prefix), &l);
    e->name_len = l;
}

static void cb_error(void* ud, const char* message, int line, int column) {
    leptris_sax_recorder* r = (leptris_sax_recorder*)ud;
    LeptrisSaxEventRecord* e = rec_new(r);
    if (!e) return;
    e->kind = LEPTRIS_SAX_EVENT_ERROR;
    uint32_t l = 0;
    e->text_off = arena_put(r, message, strlen(message), &l);
    e->text_len = l;
    e->line = (uint32_t)line;
    e->column = (uint32_t)column;
}

LEPTRIS_API LeptrisSaxRecorder leptris_sax_recorder_new(void) {
    leptris_sax_recorder* r =
        (leptris_sax_recorder*)calloc(1, sizeof(*r));
    if (!r) return NULL;
    /* The recording callbacks double as the handler struct; the
     * parser owns no state beyond what streaming.c manages. */
    r->handler.start_document = cb_start_document;
    r->handler.end_document = cb_end_document;
    r->handler.start_element = cb_start_element;
    r->handler.end_element = cb_end_element;
    r->handler.characters = cb_characters;
    r->handler.comment = cb_comment;
    r->handler.cdata = cb_cdata;
    r->handler.processing_instruction = cb_pi;
    r->handler.start_prefix_mapping = cb_start_prefix;
    r->handler.end_prefix_mapping = cb_end_prefix;
    r->handler.error = cb_error;
    r->parser = leptris_sax_parser_create(&r->handler, r);
    if (!r->parser) { free(r); return NULL; }
    /* Streaming mode: events emit per chunk, memory bounded by
     * depth — the recorder is for chunked hosts by construction. */
    leptris_sax_parser_set_streaming(r->parser, 1);
    return r;
}

LEPTRIS_API int leptris_sax_recorder_feed(LeptrisSaxRecorder r,
                                          const char* xml, size_t len,
                                          int is_final) {
    if (!r || !xml) return -1;
    /* New chunk: the host drained the previous one (or chose not
     * to). Both buffers start empty. */
    r->rec_count = 0;
    r->arena_len = 0;
    return leptris_sax_parser_feed(r->parser, xml, len, is_final);
}

LEPTRIS_API const LeptrisSaxEventRecord* leptris_sax_recorder_records(
        LeptrisSaxRecorder r, size_t* count) {
    if (!r) { if (count) *count = 0; return NULL; }
    if (count) *count = r->rec_count;
    return r->recs;
}

LEPTRIS_API const char* leptris_sax_recorder_arena(LeptrisSaxRecorder r,
                                                   size_t* len) {
    if (!r) { if (len) *len = 0; return NULL; }
    if (len) *len = r->arena_len;
    return r->arena;
}

LEPTRIS_API void leptris_sax_recorder_free(LeptrisSaxRecorder r) {
    if (!r) return;
    leptris_sax_parser_free(r->parser);
    free(r->recs);
    free(r->arena);
    free(r);
}
