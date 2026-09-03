/* benchmarks/xslt/bench_valueof.c — lane 13 fragment-output
 * canary (#682): 50k top-level value-of texts exercise the
 * fragment tail caches (XsltExec.root_sib_tail / frag_tail). The
 * pre-fix walk was O(N) per append — 1424 ms; the cached tails
 * bring the same transform to ~10 ms. Best-of protocol. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "leptris.h"
#include "utils.h"

int main(void) {
    int n = 50000;
    char* xml = (char*)malloc((size_t)n * 48 + 16);
    if (!xml) return 1;
    size_t len = 0;
    len += (size_t)snprintf(xml + len, 64, "<r>");
    for (int i = 0; i < n; i++)
        len += (size_t)snprintf(xml + len, 48, "<t>abcdefgh</t>");
    len += (size_t)snprintf(xml + len, 64, "</r>");
    const char* XSL =
        "<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform' version='1.0'>"
        "<xsl:template match='/'>"
        "<xsl:for-each select='//t'>"
        "<xsl:value-of select='.'/>"
        "</xsl:for-each></xsl:template></xsl:stylesheet>";
    LeptrisDocument d = leptris_parse_string(xml, len, NULL);
    LeptrisXslt x = leptris_xslt_parse(XSL, strlen(XSL));
    if (!d || !x) { printf("setup failed\n"); return 1; }
    char* out = leptris_xslt_apply_string(x, d);
    leptris_free_string(out);
    double best = 1e18;
    for (int rep = 0; rep < 9; rep++) {
        double t0 = benchmark_time_us() / 1000.0;
        out = leptris_xslt_apply_string(x, d);
        double ms = benchmark_time_us() / 1000.0 - t0;
        leptris_free_string(out);
        if (ms < best) best = ms;
    }
    printf("value-of . x%d: %.2f ms (best of 9)\n", n, best);
    leptris_xslt_free(x);
    leptris_document_free(d);
    free(xml);
    return 0;
}
