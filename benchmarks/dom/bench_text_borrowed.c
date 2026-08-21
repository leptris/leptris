/* benchmarks/dom/bench_text_borrowed.c
 *
 * Borrowed-text-node parse benchmark (TODO 115 acceptance).
 *
 * Exercises the parser's zero-copy text path: a text-heavy document
 * where each <p> has ~120 bytes of body text and no entities. The
 * parser must hand the text node a borrowed view into the writable
 * input buffer, with no per-node pool allocation for content.
 *
 * Run with the standard benchmark harness:
 *     ./build/benchmarks/bench_text_borrowed
 */
#include "../common/benchmark.h"
#include <leptris.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define DOC_CAPACITY 4096
#define PARAGRAPH_COUNT 8  /* ~1 KB after wrapping */

static const char PARAGRAPH[] =
    "<p>The quick brown fox jumps over the lazy dog. "
    "Pack my box with five dozen liquor jugs. "
    "Sphinx of black quartz, judge my vow.</p>";

typedef struct {
    char* xml;
    size_t len;
} text_ctx_t;

static void build_doc(text_ctx_t* ctx) {
    ctx->xml = (char*)malloc(DOC_CAPACITY);
    if (!ctx->xml) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    size_t off = 0;
    /* Clamp on truncation: snprintf returns the UNTRUNCATED length,
     * which would push `off` past DOC_CAPACITY and underflow the
     * next size argument (CodeQL cpp/overflowing-snprintf). */
    int r = snprintf(ctx->xml + off, DOC_CAPACITY - off, "<article>");
    off += (r > 0 && (size_t)r < DOC_CAPACITY - off) ? (size_t)r : 0;
    for (int i = 0; i < PARAGRAPH_COUNT; i++) {
        if (off >= DOC_CAPACITY - 1) break;
        r = snprintf(ctx->xml + off, DOC_CAPACITY - off, "%s", PARAGRAPH);
        off += (r > 0 && (size_t)r < DOC_CAPACITY - off) ? (size_t)r : 0;
    }
    if (off < DOC_CAPACITY - 1) {
        r = snprintf(ctx->xml + off, DOC_CAPACITY - off, "</article>");
        off += (r > 0 && (size_t)r < DOC_CAPACITY - off) ? (size_t)r : 0;
    }
    ctx->len = off;
}

static void bench_parse_text_heavy(void* c) {
    text_ctx_t* ctx = (text_ctx_t*)c;
    LeptrisDocument doc = leptris_parse_string(ctx->xml, ctx->len, NULL);
    if (doc) leptris_document_free(doc);
}

int main(void) {
    text_ctx_t ctx;
    build_doc(&ctx);
    bench_set_payload_size_kb((double)ctx.len / 1024.0);

    bench_print_header("Borrowed-text parse+free (TODO 115)");
    BenchResult r = bench_run("text-heavy 1KB", bench_parse_text_heavy, &ctx, 5000);
    bench_print_result(&r);

    free(ctx.xml);
    return 0;
}
