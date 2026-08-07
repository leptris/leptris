/* flat/bench_flat_parse.c — Flat document buffer benchmark (TODO 139 Phase F).
 *
 * Compares three paths for parsing a 5 KB-ish plain-XML document:
 *
 *   1. parse_only_flat  — taurus_parse_string + free, no promote.
 *                         Hits the flat fast path. The compact-pointer
 *                         tree is never built.
 *
 *   2. parse_promote    — taurus_parse_string + document_root + free.
 *                         Hits the flat fast path AND triggers promote
 *                         on first access. Equivalent to legacy parse
 *                         cost.
 *
 *   3. parse_legacy     — taurus_parse_string on input that contains
 *                         "&amp;" so it routes through the legacy
 *                         parser. Same final tree as parse_promote.
 *
 * The perf gap between (1) and (2) is the lazy-promote win. The gap
 * between (2) and (3) is the flat-parser-is-faster-than-legacy win
 * (smaller per-node records, simpler hot loop).
 *
 * Document sizes: micro (300 B), small (1 KB), medium (5 KB), large
 * (20 KB). Run with the standard bench harness.
 */
#include "../common/benchmark.h"
#include "../common/test_data.h"
#include <taurus.h>
#include "flat_fast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------------- *
 * Test documents.
 *
 * All four are plain XML (no DTD, no xmlns, no entity references) so
 * they hit the flat fast path. Adding a single "&amp;" to any of them
 * forces the legacy parser; we use that trick to measure parse_legacy.
 * ----------------------------------------------------------------------- */

/* Returns a freshly malloc'd XML string of approximately the requested
 * size. Caller frees. The `force_legacy` flag inserts "&amp;" once so
 * the dispatcher routes through the legacy parser. */
static char* make_doc(size_t target_bytes, int force_legacy) {
    /* Each book is ~80 bytes; pick count to hit the target. */
    size_t n = target_bytes / 80;
    if (n < 4) n = 4;
    size_t cap = target_bytes + 256;
    char* buf = (char*)malloc(cap);
    if (!buf) return NULL;
    size_t off = 0;
    off += snprintf(buf + off, cap - off, "<catalog>");
    for (size_t i = 0; i < n; i++) {
        off += snprintf(buf + off, cap - off,
            "<book id='b%zu'><title>Book %zu</title>"
            "<author>Author %zu</author></book>",
            i, i, i);
    }
    /* Force legacy path with one entity reference. */
    if (force_legacy) {
        off += snprintf(buf + off, cap - off, "<leg>&amp;</leg>");
    }
    off += snprintf(buf + off, cap - off, "</catalog>");
    return buf;
}

typedef struct {
    char* xml;
    size_t len;
} doc_buf_t;

static doc_buf_t make(size_t target, int force_legacy) {
    doc_buf_t d;
    d.xml = make_doc(target, force_legacy);
    d.len = d.xml ? strlen(d.xml) : 0;
    return d;
}

static void free_doc(doc_buf_t* d) {
    free(d->xml);
    d->xml = NULL;
}

/* ----------------------------------------------------------------------- *
 * Benchmarks.
 * ----------------------------------------------------------------------- */

/* parse_only: parse + free, never access root. flat_doc is built but
 * never promoted. */
static void bench_parse_only_micro(void* p) {
    doc_buf_t* d = (doc_buf_t*)p;
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(d->xml, d->len, &st);
    if (doc) taurus_document_free(doc);
}

/* parse_promote: parse + access root (triggers promote) + free. */
static void bench_parse_promote_micro(void* p) {
    doc_buf_t* d = (doc_buf_t*)p;
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(d->xml, d->len, &st);
    if (doc) {
        (void)taurus_document_root(doc);
        taurus_document_free(doc);
    }
}

/* parse_legacy: same as parse_promote but on a doc that contains "&amp;",
 * forcing the legacy parser path. */
static void bench_parse_legacy_micro(void* p) {
    doc_buf_t* d = (doc_buf_t*)p;
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(d->xml, d->len, &st);
    if (doc) {
        (void)taurus_document_root(doc);
        taurus_document_free(doc);
    }
}

/* count_via_promote: count <book> elements the expensive way —
 * promote then walk XPath result. */
static void bench_count_via_promote(void* p) {
    doc_buf_t* d = (doc_buf_t*)p;
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(d->xml, d->len, &st);
    if (!doc) return;
    TaurusElement root = taurus_document_root(doc);  /* triggers promote */
    TaurusXPathResult r = taurus_xpath_eval(doc, root, "count(//book)");
    (void)taurus_xpath_result_number(r);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

/* count_via_flat: same query via Phase E fast path — no promote.
 * Walks the FlatDoc array directly. */
static void bench_count_via_flat(void* p) {
    doc_buf_t* d = (doc_buf_t*)p;
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(d->xml, d->len, &st);
    if (!doc) return;
    (void)flat_fast_count_elements_named(doc, "book");
    taurus_document_free(doc);
}

/* serialize_via_flat: parse + serialize from FlatDoc directly.
 * Phase 2 of TODO 145 — no promote. */
static void bench_serialize_via_flat(void* p) {
    doc_buf_t* d = (doc_buf_t*)p;
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(d->xml, d->len, &st);
    if (!doc) return;
    extern char* flat_serialize_document(struct taurus_document*, int, int, const char*);
    char* out = flat_serialize_document(doc, 0, 0, NULL);
    if (out) taurus_free_string(out);
    taurus_document_free(doc);
}

/* serialize_via_compact: parse + serialize via the compact tree.
 * Triggers promote. */
static void bench_serialize_via_compact(void* p) {
    doc_buf_t* d = (doc_buf_t*)p;
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(d->xml, d->len, &st);
    if (!doc) return;
    char* out = taurus_document_serialize(doc, NULL);
    if (out) taurus_free_string(out);
    taurus_document_free(doc);
}

/* Run a comparison at one doc size. */
static void run_group(const char* title, size_t target_bytes) {
    doc_buf_t flat_doc = make(target_bytes, 0);
    doc_buf_t leg_doc = make(target_bytes, 1);

    printf("\n--- %s (actual size: %zu bytes) ---\n", title, flat_doc.len);

    BenchResult r;
    const size_t iters_small = 5000;
    const size_t iters_medium = 2000;
    const size_t iters_large = 500;
    size_t iters = target_bytes <= 1024 ? iters_small
                  : target_bytes <= 5120 ? iters_medium
                  : iters_large;

    r = bench_run("parse_only (flat, no promote)",
                  bench_parse_only_micro, &flat_doc, iters);
    bench_print_result(&r);

    r = bench_run("parse_promote (flat + lazy promote)",
                  bench_parse_promote_micro, &flat_doc, iters);
    bench_print_result(&r);

    r = bench_run("parse_legacy (forced via &amp;)",
                  bench_parse_legacy_micro, &leg_doc, iters);
    bench_print_result(&r);

    /* Phase E demonstration: same count query, two paths. */
    r = bench_run("count(//book) via XPath + promote",
                  bench_count_via_promote, &flat_doc, iters);
    bench_print_result(&r);

    r = bench_run("count(//book) via flat fast path (no promote)",
                  bench_count_via_flat, &flat_doc, iters);
    bench_print_result(&r);

    /* Phase 2: serialize comparison. */
    r = bench_run("serialize via flat (no promote)",
                  bench_serialize_via_flat, &flat_doc, iters);
    bench_print_result(&r);

    r = bench_run("serialize via compact (triggers promote)",
                  bench_serialize_via_compact, &flat_doc, iters);
    bench_print_result(&r);

    free_doc(&flat_doc);
    free_doc(&leg_doc);
}

int main(void) {
    printf("=== Flat document buffer benchmark (TODO 139 Phase F) ===\n");
    printf("Library: %s\n", taurus_version());

    run_group("Micro (~300 B)",  300);
    run_group("Small (~1 KB)",   1024);
    run_group("Medium (~5 KB)",  5120);
    run_group("Large (~20 KB)",  20480);

    return 0;
}
