/* benchmarks/dom/bench_document_lifecycle.c — #1093: the
 * programmatic-build lifecycle shape (Document.create + element
 * factories + drop), where frees arrive in batches (the Ruby
 * binding's GC-owned lifetime model). Fresh documents pay arena
 * first-touch page faults because the previous batch's pools are
 * still alive when the next batch allocates. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "leptris.h"
#include "utils.h"

static double now_ns(void) {
    return benchmark_time_us() * 1000.0;
}

/* One moxml-shaped build: fresh document, two elements + one text,
 * attach under a root, drop the document (free deferred by caller
 * batching, mimicking GC sweeps). */
static LeptrisDocument build_one(void) {
    LeptrisDocument doc = leptris_document_create();
    if (!doc) return NULL;
    LeptrisElement root = leptris_element_create(doc, "root");
    LeptrisElement child = leptris_element_create(doc, "child");
    if (root && child) {
        leptris_element_set_text(child, "payload");
        leptris_element_append_child(root, child);
        leptris_document_set_root(doc, root);
    }
    return doc;
}

int main(void) {
    enum { BATCH = 64, BATCHES = 200, WARMUP = 20 };

    /* Warm-up batches (also the steady state the recycler serves). */
    for (int w = 0; w < WARMUP; w++) {
        LeptrisDocument docs[BATCH];
        for (int i = 0; i < BATCH; i++) docs[i] = build_one();
        for (int i = 0; i < BATCH; i++) leptris_document_free(docs[i]);
    }

    /* Batched create / batched free: frees of batch N complete only
     * after all of batch N exists — the GC-owned lifetime shape. */
    double best = 1e18;
    for (int r = 0; r < 5; r++) {
        double t0 = now_ns();
        for (int b = 0; b < BATCHES; b++) {
            LeptrisDocument docs[BATCH];
            for (int i = 0; i < BATCH; i++) docs[i] = build_one();
            for (int i = 0; i < BATCH; i++) leptris_document_free(docs[i]);
        }
        double el = now_ns() - t0;
        if (el < best) best = el;
    }
    double per = best / ((double)BATCH * BATCHES);
    printf("document lifecycle (batched GC shape): %.0f ns/doc (best of 5)\n", per);

    /* Tight create+free cycle — the warm-arena floor from the issue. */
    double t0 = now_ns();
    enum { TIGHT = 50000 };
    for (int i = 0; i < TIGHT; i++) {
        LeptrisDocument d = build_one();
        leptris_document_free(d);
    }
    double tight = (now_ns() - t0) / TIGHT;
    printf("document lifecycle (tight cycle):      %.0f ns/doc\n", tight);
    return 0;
}
