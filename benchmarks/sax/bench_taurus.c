/* sax/bench_taurus.c — Taurus SAX parser benchmarks.
 *
 * Measures SAX event throughput for documents of varying sizes. The
 * SAX path skips DOM construction entirely; the relevant cost is
 * lexer + event dispatch.  Results are directly comparable to
 * libxml2's SAX reader (bench_libxml2.c).
 *
 * CPU time and peak RSS are captured by the shared harness in
 * common/benchmark.c — see TODO 101.
 */

#include "../common/benchmark.h"
#include "../common/test_data.h"

#include "taurus.h"
#include "taurus/sax/sax.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Tally of events fired during one iteration.  Useful as a sink so the
 * compiler can't elide the SAX work; not asserted on. */
typedef struct {
    size_t elements;
    size_t chars;
} sax_counters_t;

static void on_start_element(void* ud, const char* name, const char** attrs) {
    (void)name; (void)attrs;
    ((sax_counters_t*)ud)->elements++;
}

static void on_characters(void* ud, const char* text, size_t len) {
    (void)text;
    ((sax_counters_t*)ud)->chars += len;
}

typedef struct {
    const char* xml;
    size_t len;
} sax_ctx_t;

static void bench_sax_small(void* ctx) {
    sax_ctx_t* c = (sax_ctx_t*)ctx;
    sax_counters_t ctr = {0, 0};
    TaurusSAXHandler h = {0};
    h.start_element = on_start_element;
    h.characters    = on_characters;
    taurus_sax_parse(c->xml, c->len, &h, &ctr);
    (void)ctr;
}

/* Same callback set, used by the file-based large benchmark below. */
typedef struct {
    const char* path;
    char* buf;
    size_t len;
} file_ctx_t;

static void bench_sax_file(void* ctx) {
    file_ctx_t* f = (file_ctx_t*)ctx;
    sax_counters_t ctr = {0, 0};
    TaurusSAXHandler h = {0};
    h.start_element = on_start_element;
    h.characters    = on_characters;
    taurus_sax_parse(f->buf, f->len, &h, &ctr);
    (void)ctr;
}

static char* slurp(const char* path, size_t* out_len) {
    FILE* fh = fopen(path, "rb");
    if (!fh) return NULL;
    fseek(fh, 0, SEEK_END);
    long sz = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    if (sz < 0) { fclose(fh); return NULL; }
    char* buf = (char*)malloc((size_t)sz);
    if (!buf) { fclose(fh); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, fh);
    fclose(fh);
    *out_len = rd;
    return buf;
}

int main(void) {
    enum { ITERS_SMALL = 5000, ITERS_LARGE = 200 };

    /* In-memory small/medium documents from test_data.c. */
    sax_ctx_t small_ctx  = { BENCH_XML_SMALL,  BENCH_XML_SMALL_LEN  };
    sax_ctx_t medium_ctx = { BENCH_XML_MEDIUM, BENCH_XML_MEDIUM_LEN };

    bench_print_header("Taurus SAX");

    bench_set_payload_size_kb((double)BENCH_XML_SMALL_LEN / 1024.0);
    BenchResult r1 = bench_run("SAX small",  bench_sax_small,  &small_ctx,  ITERS_SMALL);
    bench_print_result(&r1);

    bench_set_payload_size_kb((double)BENCH_XML_MEDIUM_LEN / 1024.0);
    BenchResult r2 = bench_run("SAX medium", bench_sax_small, &medium_ctx, ITERS_SMALL);
    bench_print_result(&r2);

    /* Large file benchmark — reads from benchmarks/fixtures/. */
    file_ctx_t large = { .path = "fixtures/large.xml" };
    large.buf = slurp(large.path, &large.len);
    if (large.buf) {
        bench_set_payload_size_kb((double)large.len / 1024.0);
        BenchResult r3 = bench_run("SAX large (file)", bench_sax_file, &large, ITERS_LARGE);
        bench_print_result(&r3);
        free(large.buf);

        BenchResult results[] = { r1, r2, r3 };
        bench_print_summary("Taurus SAX", results, 3);
        bench_write_json("taurus", results, 3, "bench_sax_taurus.json");
    } else {
        fprintf(stderr, "note: fixtures/large.xml not found; skipping large SAX benchmark.\n");
        BenchResult results[] = { r1, r2 };
        bench_print_summary("Taurus SAX", results, 2);
        bench_write_json("taurus", results, 2, "bench_sax_taurus.json");
    }

    bench_set_payload_size_kb(0.0);
    return 0;
}
