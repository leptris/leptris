/* benchmarks/html/bench_html_parse.c — #659 lane: HTML parse
 * throughput vs libxml2. The fixture is a deterministic realistic
 * page (headings, paragraphs, tables, links, entities, classes)
 * generated inline — the ruby reference (nokogiri_html_parse.rb)
 * generates the byte-identical document. Protocol: best of N,
 * report MB/s. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "leptris.h"
#include "utils.h"

#define SECTIONS 400

static size_t gen_page(char* out) {
    size_t len = 0;
    len += (size_t)sprintf(out + len,
        "<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
        "<title>Benckmark &amp; Page</title>"
        "<link rel='stylesheet' href='/a.css'></head>"
        "<body><header class='site'><nav><ul>");
    for (int i = 0; i < 40; i++)
        len += (size_t)sprintf(out + len,
            "<li><a href='/item/%d' class='lnk'>Item&nbsp;%d</a></li>", i, i);
    len += (size_t)sprintf(out + len, "</ul></nav></header><main>");
    for (int s = 0; s < SECTIONS; s++) {
        len += (size_t)sprintf(out + len,
            "<section id='s%d'><h2>Section %d &mdash; &#8220;Notes&#8221;</h2>"
            "<p class='text'>Lorem <b>ipsum</b> &amp; <i>dolor</i> sit amet "
            "&lt;consectetur&gt; adipiscing elit &mdash; sed do eiusmod "
            "tempor incididunt ut labore.</p>"
            "<table class='data'><thead><tr><th>Id</th><th>Name</th>"
            "<th>Value</th></tr></thead><tbody>",
            s, s);
        for (int r = 0; r < 6; r++)
            len += (size_t)sprintf(out + len,
                "<tr><td>%d</td><td>row-%d-%d</td><td data-v='%d'>%d.%02d"
                "</td></tr>", r * 7 + s, s, r, r, s, r);
        len += (size_t)sprintf(out + len,
            "</tbody></table><ul class='items'>");
        for (int u = 0; u < 5; u++)
            len += (size_t)sprintf(out + len,
                "<li data-i='%d'><span class='k'>k%d</span>"
                "<span class='v'>v&amp;%d</span></li>", u, u, u);
        len += (size_t)sprintf(out + len, "</ul></section>");
    }
    len += (size_t)sprintf(out + len,
        "</main><footer><p>&copy; 2026 &#8212; bench</p></footer></body>"
        "</html>");
    return len;
}

int main(void) {
    size_t cap = 1u << 22;
    char* html = (char*)malloc(cap);
    if (!html) return 1;
    size_t len = gen_page(html);

    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument warm = leptris_parse_html_string(html, len, &st);
    if (!warm) { printf("parse failed\n"); return 1; }
    leptris_document_free(warm);

    int reps = getenv("HTML_BENCH_REPS") ? atoi(getenv("HTML_BENCH_REPS")) : 9;
    double best = 1e18;
    for (int i = 0; i < reps; i++) {
        double t0 = benchmark_time_us();
        LeptrisDocument d = leptris_parse_html_string(html, len, &st);
        double us = benchmark_time_us() - t0;
        leptris_document_free(d);
        if (us < best) best = us;
    }
    printf("leptris html parse: %zu bytes, %.0f us, %.1f MB/s (best of %d)\n",
           len, best, (double)len / best, reps);
    free(html);
    return 0;
}
