/* #624 fixture shape: 499-book catalog, select-heavy for-each. */
#include "../common/benchmark.h"
#include <leptris.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* build_books(size_t books) {
    size_t cap = books * 128 + 64;
    char* s = (char*)malloc(cap);
    size_t n = (size_t)snprintf(s, 64, "<catalog>");
    for (size_t i = 0; i < books; i++) {
        n += (size_t)snprintf(s + n, cap - n,
            "<book id='%zu' price='%zu'><title>Book %zu</title></book>",
            i, i % 200, i);
    }
    snprintf(s + n, cap - n, "</catalog>");
    return s;
}

static const char* XSL =
    "<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
    " version='1.0'><xsl:template match='/'>"
    "<out><xsl:for-each select=\"//book[@price > 100]\">"
    "<b><xsl:value-of select='@id'/></b></xsl:for-each></out>"
    "</xsl:template></xsl:stylesheet>";

typedef struct { LeptrisXslt x; LeptrisDocument d; } xf_t;
static void run_apply(void* v) {
    xf_t* c = (xf_t*)v;
    char* o = leptris_xslt_apply_string(c->x, c->d);
    leptris_free_string(o);
}

int main(void) {
    enum { BOOKS = 499, ITERS = 400 };
    char* xml = build_books(BOOKS);
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), NULL);
    LeptrisXslt x = leptris_xslt_parse(XSL, strlen(XSL));
    if (!d || !x) { fprintf(stderr, "setup failed\n"); return 1; }
    xf_t c = { x, d };
    BenchResult r = bench_run("xslt apply //book[@price>100]", run_apply, &c, ITERS);
    bench_print_result(&r);
    leptris_xslt_free(x);
    leptris_document_free(d);
    free(xml);
    return 0;
}
