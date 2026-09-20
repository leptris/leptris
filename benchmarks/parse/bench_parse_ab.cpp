/* bench_parse_ab.cpp — the interleaved-parse lane's gate (#1222).
 *
 * Same fixtures, sizes, and shapes for leptris and pugixml,
 * best-of-5 each. The CI Benchmark legs run this on quiet
 * runners; the interleaved lane's slices are gated on THESE
 * numbers, never a loaded dev machine (2026-09-20: a Mac at load
 * 340 measured a 472 MB/s build at 46-85).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "leptris.h"
#ifdef HAVE_PUGIXML
#include "pugixml.hpp"
using namespace pugi;
#endif

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static size_t gen(char* out, size_t target, int text_shape) {
    size_t len = 0;
    len += (size_t)sprintf(out + len, "<root>");
    int i = 0;
    if (text_shape) {
        while (len < target)
            len += (size_t)sprintf(out + len,
                "<p>The quick brown fox jumps over the lazy dog %d.</p>", i++);
    } else {
        while (len < target)
            len += (size_t)sprintf(out + len,
                "<item id=\"%d\" class=\"c%d\" state=\"on\" ref=\"%d\">v %d</item>",
                i, i % 7, i * 3, i++);
    }
    len += (size_t)sprintf(out + len, "</root>");
    return len;
}

int main(void) {
    const size_t sizes[3] = {1500 * 1024, 5000 * 1024, 13000 * 1024};
    const char* names[2] = {"attr", "text"};
    printf("%-6s %9s %12s %12s\n", "shape", "bytes", "leptris", "pugixml");
    for (int s = 0; s < 2; s++) {
        for (size_t si = 0; si < 3; si++) {
            char* doc = (char*)malloc(sizes[si] + 2048);
            if (!doc) return 1;
            size_t len = gen(doc, sizes[si], s);
            double best = 1e18;
            for (int r = 0; r < 5; r++) {
                double a = now_us();
                LeptrisStatus st = LEPTRIS_OK;
                LeptrisDocument d = leptris_parse_string(doc, len, &st);
                double b = now_us();
                if (!d) { printf("PARSE FAILED\n"); return 1; }
                leptris_document_free(d);
                if (b - a < best) best = b - a;
            }
            double mbps = len / 1048576.0 / (best / 1e6);
#ifdef HAVE_PUGIXML
            double pbest = 1e18;
            for (int r = 0; r < 5; r++) {
                double a = now_us();
                xml_document xdoc;
                xml_parse_result res = xdoc.load_buffer(doc, len);
                double b = now_us();
                if (!res) { printf("PUGIXML FAILED: %s\n", res.description()); return 1; }
                if (b - a < pbest) pbest = b - a;
            }
            double pmbps = len / 1048576.0 / (pbest / 1e6);
            printf("%-6s %9zu %10.1f %10.1f  MB/s  (pugixml %.2fx)\n",
                   names[s], len, mbps, pmbps, pmbps / mbps);
#else
            printf("%-6s %9zu %10.1f %10s\n", names[s], len, mbps, "-");
#endif
            free(doc);
        }
    }
    return 0;
}
