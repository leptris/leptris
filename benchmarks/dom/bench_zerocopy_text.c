/* benchmarks/dom/bench_zerocopy_text.c — SOTA #2053: the zero-copy
 * text lever verdict. The parser's text lanes carve borrowed views
 * out of the doc-owned buffer (TODO 115, direct-parse round 8); the
 * copy lane exists for entity-laden spans.
 *
 * VERDICT (2026-11-16, ABBA x2, medians of 4, 200 iters/run,
 * 520KB text-heavy corpus, RelWithDebInfo, apple-silicon):
 *   parse      borrowed 215.8us vs copy 573.9us — borrow wins 2.66x
 *   walk+read  borrowed 288.8us vs copy 689.3us — borrow wins 2.39x
 * ADOPTED — the lever wins BOTH rows; the copy lane stays for the
 * entity/mutation spans that require it, and the re-proposal loop is
 * closed with these numbers.
 *
 * Reproducing the copy face: patch (scratch worktree, never commit)
 * `leptris_text_create_borrowed` and `dp_text_create` to delegate to
 * `leptris_text_create(content, len, pool, doc)` under a
 * LEPTRIS_FORCE_COPY_TEXT define, then define it on both
 * leptris_objects and this target. */
#include "benchmark.h"
#include <leptris.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LEPTRIS_FORCE_COPY_TEXT
#define LEPTRIS_FORCE_COPY_TEXT 0
#endif

#define PARAGRAPH_COUNT 4000

static const char PARAGRAPH[] =
    "<p>The quick brown fox jumps over the lazy dog. "
    "Pack my box with five dozen liquor jugs. "
    "Sphinx of black quartz, judge my vow.</p>";

static char* g_xml;
static size_t g_len;

static void build_doc(void) {
    size_t cap = PARAGRAPH_COUNT * (sizeof PARAGRAPH + 8) + 64;
    g_xml = (char*)malloc(cap);
    if (!g_xml) exit(2);
    size_t off = (size_t)snprintf(g_xml, cap, "<article>");
    for (int i = 0; i < PARAGRAPH_COUNT; i++)
        off += (size_t)snprintf(g_xml + off, cap - off, "%s", PARAGRAPH);
    off += (size_t)snprintf(g_xml + off, cap - off, "</article>");
    g_len = off;
}

/* The walk+read row: parse, then read EVERY text node's content —
 * the reads must touch the text (sum the lengths) or a dead-code
 * fold would zero the row. */
static size_t walk_read(void) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(g_xml, g_len, &st);
    if (!doc) return 0;
    LeptrisElement root = leptris_document_root(doc);
    if (!root) { leptris_document_free(doc); return 0; }
    LeptrisXPathResult r = leptris_xpath_eval(doc, root, "//p");
    if (!r) { leptris_document_free(doc); return 0; }
    size_t n = leptris_xpath_result_count(r);
    size_t total = 0;
    for (size_t i = 0; i < n; i++) {
        LeptrisElement p = leptris_xpath_result_get(r, i);
        LeptrisNodeRef c = leptris_node_first_child(
            leptris_element_as_node(p));
        while (c) {
            if (leptris_node_get_type(c) == LEPTRIS_NODE_TYPE_TEXT) {
                const char* t = leptris_text_node_get_content(c);
                if (t) total += strlen(t);
            }
            c = leptris_node_next_sibling(c);
        }
    }
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
    return total;
}

/* Parse-only row. */
static void bench_parse(void* unused) {
    (void)unused;
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(g_xml, g_len, &st);
    if (doc) leptris_document_free(doc);
}

static void bench_walk(void* unused) {
    (void)unused;
    size_t total = walk_read();
    /* The read must have touched ~every paragraph's text; a fold to
     * zero means the row measured nothing. */
    if (total < 100) {
        fprintf(stderr, "walk+read folded away (total=%zu)\n", total);
        exit(3);
    }
}

int main(int argc, char** argv) {
    const char* face = LEPTRIS_FORCE_COPY_TEXT ? "copy" : "borrowed";
    int iters = argc > 1 ? atoi(argv[1]) : 200;
    build_doc();
    bench_set_payload_size_kb((double)g_len / 1024.0);
    printf("# zero-copy text verdict face=%s iters=%d bytes=%zu\n",
           face, iters, g_len);

    BenchResult rp = bench_run("parse-only", bench_parse, NULL,
                               (size_t)iters);
    printf("row=parse face=%s mean_us=%.1f min_us=%.1f\n",
           face, rp.mean_us, rp.min_us);
    BenchResult rw = bench_run("walk+read", bench_walk, NULL,
                               (size_t)iters);
    printf("row=walk_read face=%s mean_us=%.1f min_us=%.1f\n",
           face, rw.mean_us, rw.min_us);
    free(g_xml);
    return 0;
}
