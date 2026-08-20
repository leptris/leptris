/* benchmark_sax — SAX-mode parse: taurus vs libxml2.
 *
 * pugixml has no SAX interface, so libxml2 (the reference streaming
 * parser) is the comparison. Both sides install no-op handlers so the
 * measurement is pure event-delivery cost: scan + callback dispatch,
 * no tree construction.
 *
 * Method: min-of-N per side, same buffer, entity-bearing corpus file.
 */
/* CLOCK_MONOTONIC under strict -std=c11 on glibc. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <taurus.h>
#include <taurus/sax/sax.h>

#include <libxml/parser.h>
#include <libxml/SAX2.h>

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static char* slurp(const char* path, size_t* len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* b = (char*)malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    b[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return b;
}

/* ---- no-op handlers (volatile sinks keep them from being folded) ---- */
static volatile size_t g_events;
static void t_start(void* u, const char* n, const char** a) { (void)u; (void)n; (void)a; g_events++; }
static void t_end(void* u, const char* n) { (void)u; (void)n; g_events++; }
static void t_chars(void* u, const char* t, size_t l) { (void)u; (void)t; (void)l; g_events++; }

static void x_start(void* u, const xmlChar* local, const xmlChar* pfx,
                    const xmlChar* uri, int n_ns, const xmlChar** ns,
                    int n_attr, int n_def, const xmlChar** attr) {
    (void)u; (void)local; (void)pfx; (void)uri; (void)n_ns; (void)ns;
    (void)n_attr; (void)n_def; (void)attr;
    g_events++;
}
static void x_end(void* u, const xmlChar* local, const xmlChar* pfx,
                  const xmlChar* uri) {
    (void)u; (void)local; (void)pfx; (void)uri;
    g_events++;
}
static void x_chars(void* u, const xmlChar* t, int l) { (void)u; (void)t; (void)l; g_events++; }

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "fixtures/small.xml";
    size_t len;
    char* buf = slurp(path, &len);
    if (!buf) { printf("cannot read %s\n", path); return 1; }

    TaurusSAXHandler handler;
    memset(&handler, 0, sizeof(handler));
    handler.start_element = t_start;
    handler.end_element = t_end;
    handler.characters = t_chars;

    double t_best = 1e18;
    for (int r = 0; r < 30; r++) {
        g_events = 0;
        double t0 = now_us();
        taurus_sax_parse(buf, len, &handler, NULL);
        double dt = now_us() - t0;
        if (dt < t_best) t_best = dt;
    }

    xmlSAXHandler sax;
    memset(&sax, 0, sizeof(sax));
    sax.initialized = XML_SAX2_MAGIC;
    sax.startElementNs = x_start;
    sax.endElementNs = x_end;
    sax.characters = x_chars;

    double x_best = 1e18;
    for (int r = 0; r < 30; r++) {
        g_events = 0;
        double t0 = now_us();
        xmlSAXUserParseMemory(&sax, NULL, buf, (int)len);
        double dt = now_us() - t0;
        if (dt < x_best) x_best = dt;
    }

    printf("SAX %-28s %6zuKB taurus=%7.1fus libxml2=%7.1fus ratio=%.2fx "
           "(taurus %.2f GB/s)\n",
           path, len / 1024, t_best, x_best, t_best / x_best,
           len / t_best / 1000.0);
    free(buf);
    return 0;
}
