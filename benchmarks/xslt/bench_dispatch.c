/* benchmarks/xslt/bench_dispatch.c — lane 13 release scorecard
 * (#682): the template-dispatch fixture. 2000 books, one
 * apply-templates walk, AVT attribute, value-of selects.
 * Protocol: best of 9 x 200 — MEAN timing hides GC/allocator
 * churn; compare against in-process lxml/libxslt on the same
 * machine (benchmarks/README.md). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "leptris.h"
#include "utils.h"

int main(void) {
    int books = 2000;
    size_t cap = (size_t)books * 80 + 64;
    char* xml = (char*)malloc(cap);
    if (!xml) return 1;
    size_t len = 0;
    len += (size_t)snprintf(xml + len, 64, "<catalog>");
    for (int i = 0; i < books; i++)
        len += (size_t)snprintf(xml + len, 80,
            "<book id='%d'><title>t</title><author>a</author></book>", i);
    len += (size_t)snprintf(xml + len, 64, "</catalog>");
    const char* XSL =
        "<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform' version='1.0'>"
        "<xsl:template match='/'><out>"
        "<xsl:apply-templates select='//book'/></out></xsl:template>"
        "<xsl:template match='book[title]'><b id='{@id}'>"
        "<xsl:apply-templates select='*'/></b></xsl:template>"
        "<xsl:template match='title'><t><xsl:value-of select='.'/></t></xsl:template>"
        "<xsl:template match='author'><a><xsl:value-of select='.'/></a></xsl:template>"
        "</xsl:stylesheet>";
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
    printf("dispatch %d books: %.2f ms (best of 9)\n", books, best);
    leptris_xslt_free(x);
    leptris_document_free(d);
    free(xml);
    return 0;
}
