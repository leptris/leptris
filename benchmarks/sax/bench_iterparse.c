/* sax/bench_iterparse.c — Leptris incremental iterparse benchmarks.
 *
 * Issue #1178: the ruby binding measured a ~6.9µs fixed cost per
 * iterparse call that no engine-side bench could reproduce — there
 * was no iterparse row anywhere in benchmarks/. This bench pins the
 * per-iteration (per-next-call) cost of the pull iterator on the
 * same payloads the SAX/DOM rows use, so binding-side overhead can
 * be separated from engine cost by subtraction.
 *
 * Rows:
 *   - Iterparse top-level (small/medium): new + next*-loop + free
 *   - Iterparse full-document (medium): same, FULL_DOCUMENT mode
 *   - DOM one-shot (medium): parse + free, the zero-fixed-cost floor
 * A per-event line (mean_us / events) is printed after each row —
 * the number the binding-side trace should match.
 */

#include "../common/benchmark.h"
#include "../common/test_data.h"

#include "leptris.h"
#include "leptris/sax/sax.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char* xml;
    size_t len;
    LeptrisIterparseMode mode;
    size_t events;   /* events per iteration, counted once */
    int counted;
} iter_ctx_t;

static size_t count_events(const char* xml, size_t len,
                           LeptrisIterparseMode mode) {
    LeptrisIterparse it = leptris_iterparse_new_ex(xml, len, mode);
    if (!it) return 0;
    size_t n = 0;
    while (leptris_iterparse_next(it)) n++;
    leptris_iterparse_free(it);
    return n;
}

static void bench_iterparse(void* ctx) {
    iter_ctx_t* c = (iter_ctx_t*)ctx;
    if (!c->counted) {
        c->events = count_events(c->xml, c->len, c->mode);
        c->counted = 1;
    }
    LeptrisIterparse it = leptris_iterparse_new_ex(c->xml, c->len, c->mode);
    while (leptris_iterparse_next(it)) {
        /* drain: the sink is the iterator itself; next() releases
         * the previous subtree as part of its contract. */
    }
    leptris_iterparse_free(it);
}

static void bench_dom_oneshot(void* ctx) {
    iter_ctx_t* c = (iter_ctx_t*)ctx;
    LeptrisDocument d = leptris_parse_string(c->xml, c->len, NULL);
    if (d) leptris_document_free(d);
}

static void print_per_event(const char* label, const BenchResult* r,
                            size_t events) {
    if (events == 0) {
        fprintf(stderr, "note: %s produced 0 events\n", label);
        return;
    }
    printf("    %-26s %8.3f µs/event (%zu events)\n", label,
           r->mean_us / (double)events, events);
}

int main(void) {
    enum { ITERS_SMALL = 5000, ITERS_LARGE = 2000 };

    iter_ctx_t small = { BENCH_XML_SMALL, strlen(BENCH_XML_SMALL),
                         LEPTRIS_ITERPARSE_TOP_LEVEL, 0, 0 };
    iter_ctx_t medium = { BENCH_XML_MEDIUM, strlen(BENCH_XML_MEDIUM),
                          LEPTRIS_ITERPARSE_TOP_LEVEL, 0, 0 };
    iter_ctx_t medium_full = { BENCH_XML_MEDIUM, strlen(BENCH_XML_MEDIUM),
                               LEPTRIS_ITERPARSE_FULL_DOCUMENT, 0, 0 };

    bench_print_header("Leptris Iterparse");

    bench_set_payload_size_kb((double)small.len / 1024.0);
    BenchResult r1 = bench_run("Iterparse small (top-level)",
                               bench_iterparse, &small, ITERS_SMALL);
    bench_print_result(&r1);
    print_per_event("per event", &r1, small.events);

    bench_set_payload_size_kb((double)medium.len / 1024.0);
    BenchResult r2 = bench_run("Iterparse medium (top-level)",
                               bench_iterparse, &medium, ITERS_LARGE);
    bench_print_result(&r2);
    print_per_event("per event", &r2, medium.events);

    BenchResult r3 = bench_run("Iterparse medium (full-doc)",
                               bench_iterparse, &medium_full, ITERS_LARGE);
    bench_print_result(&r3);
    print_per_event("per event", &r3, medium_full.events);

    BenchResult r4 = bench_run("DOM one-shot medium",
                               bench_dom_oneshot, &medium, ITERS_LARGE);
    bench_print_result(&r4);
    print_per_event("per root child", &r4, medium.events);

    bench_set_payload_size_kb(0.0);

    BenchResult results[] = { r1, r2, r3, r4 };
    bench_print_summary("Leptris Iterparse", results, 4);
    bench_write_json("leptris", results, 4, "bench_iterparse.json");
    return 0;
}
