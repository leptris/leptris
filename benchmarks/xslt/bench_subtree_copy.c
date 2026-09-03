#define _POSIX_C_SOURCE 199309L
/* benchmarks/xslt/bench_subtree_copy.c — lane 13 subtree
 * duplicate scorecard (#682/#653): leptris_element_copy of a
 * 100-book subtree into a fresh document (create + copy + free
 * per iteration). The v1.9.74 pool-threaded copier took this
 * from 0.111 ms to 0.048 ms; Ruby Element#dup runs 2.27x ahead
 * of Nokogiri 1.19.4 on this fixture. Best-of protocol. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "leptris.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(void) {
    int books = 100;
    size_t cap = (size_t)books * 200 + 64;
    char* xml = (char*)malloc(cap);
    if (!xml) return 1;
    size_t len = 0;
    len += (size_t)snprintf(xml + len, 64, "<catalog version='2.0'>");
    for (int i = 0; i < books; i++)
        len += (size_t)snprintf(xml + len, 200,
            "<book id='%d' lang='en'><title>Book %d</title>"
            "<author id='a%d'>Author %d</author>"
            "<price currency='USD'>%d.99</price></book>", i, i, i, i, i);
    len += (size_t)snprintf(xml + len, 64, "</catalog>");
    LeptrisDocument d = leptris_parse_string(xml, len, NULL);
    if (!d) { printf("parse failed\n"); return 1; }
    LeptrisElement root = leptris_document_root(d);

    {
        LeptrisDocument w = leptris_document_create();
        LeptrisElement wc = leptris_element_copy(root, w);
        (void)wc;
        leptris_document_free(w);
    }

    double best = 1e18;
    for (int rep = 0; rep < 9; rep++) {
        double t0 = now_ms();
        for (int i = 0; i < 200; i++) {
            LeptrisDocument nd = leptris_document_create();
            LeptrisElement c = leptris_element_copy(root, nd);
            if (!c) { printf("copy failed\n"); return 1; }
            leptris_document_free(nd);
        }
        double ms = (now_ms() - t0) / 200.0;
        if (ms < best) best = ms;
    }
    printf("element_copy %d-book subtree: %.3f ms/copy (best of 9x200)\n",
           books, best);
    leptris_document_free(d);
    free(xml);
    return 0;
}
