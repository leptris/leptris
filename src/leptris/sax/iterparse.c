/* sax/iterparse.c — bounded-memory incremental tree iteration
 * (TODO.bindings/02, issue #510 Tier 2).
 *
 * Rides the pull API: events build the current TOP-LEVEL subtree in
 * its own document; when the subtree completes it is handed to the
 * caller, and the next call releases the previous document — the
 * pool is destroyed and the memory reclaimed. Peak memory is bounded
 * by the largest subtree plus the open-element stack, not by the
 * document size.
 *
 * v1 limitation (documented in the header): element names are the
 * QName as written; namespace prefixes are not re-resolved. Use the
 * DOM path when namespace URIs matter. */
#include "../../include/leptris.h"
#include "../../include/leptris/sax/sax.h"
#include <stdlib.h>
#include <string.h>

#define IT_MAX_DEPTH 4096

struct leptris_iterparse {
    LeptrisPullParser pull;
    LeptrisDocument doc;          /* pool of the subtree in flight */
    LeptrisElement stack[IT_MAX_DEPTH];
    int depth;                    /* current element depth (root=0) */
    char* text_buf;
    size_t text_len, text_cap;
    LeptrisElement done;          /* completed subtree handed out */
    int exhausted;
};

static void it_flush_text(struct leptris_iterparse* it) {
    /* depth >= 2: inside a subtree (a parent element exists). Text
     * at root level (depth <= 1) has no materialized parent. */
    if (it->text_len == 0 || it->depth < 2) { it->text_len = 0; return; }
    it->text_buf[it->text_len] = '\0';
    LeptrisNodeRef t = leptris_text_node_create(it->doc, it->text_buf);
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

LEPTRIS_API LeptrisIterparse leptris_iterparse_new(const char* xml,
                                                    size_t len) {
    if (!xml || len == 0) return NULL;
    struct leptris_iterparse* it =
        (struct leptris_iterparse*)calloc(1, sizeof(*it));
    if (!it) return NULL;
    it->pull = leptris_pull_new(xml, len);
    if (!it->pull) { free(it); return NULL; }
    return it;
}

LEPTRIS_API LeptrisElement leptris_iterparse_next(LeptrisIterparse it) {
    if (!it || it->exhausted) return NULL;

    /* Release the subtree handed out last round. We are back
     * INSIDE the document root (depth 1); the next top-level
     * element starts a fresh subtree document. */
    if (it->done) {
        it->done = NULL;
        if (it->doc) { leptris_document_free(it->doc); it->doc = NULL; }
        it->depth = 1;
    }

    const LeptrisPullEvent* ev;
    while ((ev = leptris_pull_next(it->pull)) != NULL) {
        switch (ev->type) {
            case LEPTRIS_PULL_START_ELEMENT: {
                it_flush_text(it);
                if (it->depth == 0) {
                    /* The document root element: never materialized —
                     * its children are yielded one subtree at a time. */
                    it->depth = 1;
                    break;
                }
                if (it->depth == 1) {
                    /* New top-level child: fresh pool for this
                     * subtree, released on the next yield. */
                    it->doc = leptris_document_create();
                    if (!it->doc) { it->exhausted = 1; return NULL; }
                }
                LeptrisElement e = leptris_element_create(it->doc, ev->name);
                if (!e) { it->exhausted = 1; return NULL; }
                size_t na = leptris_pull_attr_count(it->pull);
                for (size_t i = 0; i < na; i++) {
                    leptris_element_set_attribute(
                        e, leptris_pull_attr_name(it->pull, i),
                        leptris_pull_attr_value(it->pull, i));
                }
                if (it->depth >= 2) {
                    leptris_element_append_child(it->stack[it->depth - 1], e);
                }
                if (it->depth < IT_MAX_DEPTH) it->stack[it->depth] = e;
                it->depth++;
                break;
            }
            case LEPTRIS_PULL_TEXT:
                it_append_text(it, ev->text, ev->text_len);
                break;
            case LEPTRIS_PULL_END_ELEMENT:
                it_flush_text(it);
                it->depth--;
                if (it->depth == 1) {
                    /* A top-level child of the document root
                     * completed (stack[0] is the root itself). */
                    it->done = it->stack[1];
                    return it->done;
                }
                break;
            default:
                /* comments/CDATA/PI/END_DOCUMENT/ERROR: flush pending
                 * text; keep going until a subtree completes or the
                 * input is exhausted. */
                it_flush_text(it);
                if (ev->type == LEPTRIS_PULL_ERROR ||
                    ev->type == LEPTRIS_PULL_END_DOCUMENT) {
                    it->exhausted = 1;
                    return NULL;
                }
                break;
        }
    }
    it->exhausted = 1;
    return NULL;
}

LEPTRIS_API void leptris_iterparse_free(LeptrisIterparse it) {
    if (!it) return;
    if (it->pull) leptris_pull_free(it->pull);
    if (it->doc) leptris_document_free(it->doc);
    free(it->text_buf);
    free(it);
}
