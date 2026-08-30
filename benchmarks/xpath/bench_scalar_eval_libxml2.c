/* libxml2 twin of bench_scalar_eval (issues #645b, #610): the same
 * catalog, the same expressions, xmlXPathEval — same-machine A/B. */
#include "../common/benchmark.h"
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* build_catalog(size_t items) {
    size_t cap = items * 96 + 64;
    char* s = (char*)malloc(cap);
    size_t n = (size_t)snprintf(s, 64, "<catalog>");
    for (size_t i = 0; i < items; i++) {
        n += (size_t)snprintf(s + n, cap - n,
            "<item id='%zu'><name>item %zu</name>"
            "<price>%zu.99</price></item>",
            i, i, i % 100);
    }
    snprintf(s + n, cap - n, "</catalog>");
    return s;
}

typedef struct {
    xmlDocPtr doc;
    xmlNodePtr ctx;
    const char* expr;
} eval_ctx_t;

static void run_eval(void* v) {
    eval_ctx_t* c = (eval_ctx_t*)v;
    xmlXPathContextPtr xc = xmlXPathNewContext(c->doc);
    xc->node = c->ctx;
    xmlXPathObjectPtr o = xmlXPathEvalExpression(
        (const xmlChar*)c->expr, xc);
    if (o) xmlXPathFreeObject(o);
    xmlXPathFreeContext(xc);
}

int main(void) {
    enum { ITEMS = 20000, WARM = 200, PARENT_ITERS = 5000 };
    char* xml = build_catalog(ITEMS);
    xmlDocPtr doc = xmlReadMemory(xml, (int)strlen(xml), NULL, NULL, 0);
    if (!doc) { fprintf(stderr, "parse failed\n"); return 1; }

    xmlXPathContextPtr xc = xmlXPathNewContext(doc);
    xmlXPathObjectPtr f =
        xmlXPathEvalExpression((const xmlChar*)"//item[5000]", xc);
    xmlNodePtr ctx = (f && f->nodesetval && f->nodesetval->nodeNr > 0)
        ? f->nodesetval->nodeTab[0] : NULL;
    xmlXPathFreeObject(f);
    xmlXPathFreeContext(xc);

    printf("=== #645b scalar eval: string(//item[1]), warm ===\n");
    eval_ctx_t sc = { doc, NULL, "string(//item[1])" };
    BenchResult a = bench_run("libxml2 scalar eval", run_eval, &sc, WARM);
    bench_print_result(&a);

    if (ctx) {
        printf("\n=== #610 parent axis: .. x%d ===\n", PARENT_ITERS);
        eval_ctx_t pc = { doc, ctx, ".." };
        for (int i = 0; i < 3; i++)
            a = bench_run("libxml2 parent ..", run_eval, &pc, PARENT_ITERS);
        bench_print_result(&a);
    }

    xmlFreeDoc(doc);
    free(xml);
    return 0;
}
