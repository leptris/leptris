/* sax/bench_libxml2.c — libxml2 SAX reader benchmark, same shape as
 * bench_taurus.c for direct comparison. */

#include "../common/benchmark.h"
#include "../common/test_data.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t elements;
    size_t chars;
} sax_counters_t;

static void on_start_element(void* ud, const xmlChar* name, const xmlChar** attrs) {
    (void)name; (void)attrs;
    ((sax_counters_t*)ud)->elements++;
}

static void on_characters(void* ud, const xmlChar* text, int len) {
    ((sax_counters_t*)ud)->chars += (size_t)len;
}

static void init_handler(xmlSAXHandler* h) {
    memset(h, 0, sizeof(*h));
    h->startElement = (startElementSAXFunc)on_start_element;
    h->characters   = (charactersSAXFunc)on_characters;
}

typedef struct {
    const char* xml;
    size_t len;
} sax_ctx_t;

static void bench_sax(void* ctx) {
    sax_ctx_t* c = (sax_ctx_t*)ctx;
    sax_counters_t ctr = {0, 0};
    xmlSAXHandler h;
    init_handler(&h);
    xmlSAXUserParseMemory(&h, &ctr, c->xml, (int)c->len);
    (void)ctr;
}

typedef struct {
    char* buf;
    size_t len;
} file_ctx_t;

static void bench_sax_file(void* ctx) {
    file_ctx_t* f = (file_ctx_t*)ctx;
    sax_counters_t ctr = {0, 0};
    xmlSAXHandler h;
    init_handler(&h);
    xmlSAXUserParseMemory(&h, &ctr, f->buf, (int)f->len);
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

    xmlInitParser();

    sax_ctx_t small_ctx  = { BENCH_XML_SMALL,  strlen(BENCH_XML_SMALL)  };
    sax_ctx_t medium_ctx = { BENCH_XML_MEDIUM, strlen(BENCH_XML_MEDIUM) };

    bench_print_header("libxml2 SAX");

    bench_set_payload_size_kb((double)strlen(BENCH_XML_SMALL) / 1024.0);
    BenchResult r1 = bench_run("SAX small",  bench_sax, &small_ctx,  ITERS_SMALL);
    bench_print_result(&r1);

    bench_set_payload_size_kb((double)strlen(BENCH_XML_MEDIUM) / 1024.0);
    BenchResult r2 = bench_run("SAX medium", bench_sax, &medium_ctx, ITERS_SMALL);
    bench_print_result(&r2);

    file_ctx_t large = {0};
    large.buf = slurp("fixtures/large.xml", &large.len);
    if (large.buf) {
        bench_set_payload_size_kb((double)large.len / 1024.0);
        BenchResult r3 = bench_run("SAX large (file)", bench_sax_file, &large, ITERS_LARGE);
        bench_print_result(&r3);
        free(large.buf);

        BenchResult results[] = { r1, r2, r3 };
        bench_print_summary("libxml2 SAX", results, 3);
        bench_write_json("libxml2", results, 3, "bench_sax_libxml2.json");
    } else {
        fprintf(stderr, "note: fixtures/large.xml not found; skipping large SAX benchmark.\n");
        BenchResult results[] = { r1, r2 };
        bench_print_summary("libxml2 SAX", results, 2);
        bench_write_json("libxml2", results, 2, "bench_sax_libxml2.json");
    }

    bench_set_payload_size_kb(0.0);
    xmlCleanupParser();
    return 0;
}
