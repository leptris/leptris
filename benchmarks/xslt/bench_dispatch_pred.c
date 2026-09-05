#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "leptris.h"
#include "utils.h"
int main(void) {
    enum { N = 120, ITEMS = 2400 };
    char* xml = malloc(ITEMS * 48 + 32);
    size_t len = sprintf(xml, "<root>");
    for (int i = 0; i < ITEMS; i++)
        len += sprintf(xml + len, "<item k='%d' v='v%d'/>", i % N, i);
    len += sprintf(xml + len, "</root>");
    char* xsl = malloc(N * 200 + 256);
    size_t xl = sprintf(xsl, "<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform' version='1.0'>");
    for (int i = 0; i < N; i++)
        xl += sprintf(xsl + xl,
            "<xsl:template match=\"item[@k='%d']\"><o%d><xsl:value-of select='@v'/></o%d></xsl:template>", i, i, i);
    xl += sprintf(xsl + xl, "</xsl:stylesheet>");
    LeptrisDocument d = leptris_parse_string(xml, len, NULL);
    LeptrisXslt x = leptris_xslt_parse(xsl, xl);
    if (!d || !x) { printf("setup failed\n"); return 1; }
    char* out = leptris_xslt_apply_string(x, d);
    leptris_free_string(out);
    double best = 1e18;
    for (int r = 0; r < 9; r++) {
        double t0 = benchmark_time_us();
        out = leptris_xslt_apply_string(x, d);
        double us = benchmark_time_us() - t0;
        leptris_free_string(out);
        if (us < best) best = us;
    }
    printf("pred-pattern dispatch %d templates / %d items: %.2f ms (best of 9)\n", N, ITEMS, best / 1000.0);
    return 0;
}
