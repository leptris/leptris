/* Scalar/compiled eval micro-benchmarks (issues #645b, #610):
 * per-eval cost of scalar XPath through leptris_xpath_eval vs a
 * pre-compiled leptris_xpath_compiled_eval, and the parent-axis
 * string-vs-compiled A/B. */
#include "../common/benchmark.h"
#include <leptris.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ~2 MB catalog in the ruby fixture's shape: N <item> children. */
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
    LeptrisDocument doc;
    LeptrisElement ctx;
    const char* expr;
    LeptrisXPathCompiled compiled;
} eval_ctx_t;

static void run_string(void* v) {
    eval_ctx_t* c = (eval_ctx_t*)v;
    LeptrisXPathResult r = leptris_xpath_eval(c->doc, c->ctx, c->expr);
    leptris_xpath_result_free(r);
}

static void run_compiled(void* v) {
    eval_ctx_t* c = (eval_ctx_t*)v;
    LeptrisXPathResult r =
        leptris_xpath_compiled_eval(c->compiled, c->doc, c->ctx);
    leptris_xpath_result_free(r);
}

int main(int argc, char** argv) {
    enum { ITEMS = 20000, PARENT_ITERS = 5000 };
    int WARM = argc > 1 ? atoi(argv[1]) : 200;
    char* xml = build_catalog(ITEMS);
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), NULL);
    if (!doc) { fprintf(stderr, "parse failed\n"); return 1; }

    LeptrisXPathResult first =
        leptris_xpath_eval(doc, NULL, "//item[5000]");
    LeptrisElement ctx = first
        ? (LeptrisElement)leptris_xpath_result_get_node(first, 0)
        : NULL;

    printf("=== #645b scalar eval: string(//item[1]), warm ===\n");
    eval_ctx_t sc = { doc, NULL, "string(//item[1])", NULL };
    sc.compiled = leptris_xpath_compile("string(//item[1])");
    BenchResult a = bench_run("scalar string eval", run_string, &sc, WARM);
    BenchResult b = bench_run("scalar compiled eval", run_compiled, &sc, WARM);
    bench_print_result(&a);
    bench_print_result(&b);

    if (ctx) {
        printf("\n=== #610 parent axis: .. x%d ===\n", PARENT_ITERS);
        eval_ctx_t pc = { doc, ctx, "..", NULL };
        pc.compiled = leptris_xpath_compile("..");
        /* interleaved A/B so machine state cancels */
        for (int i = 0; i < 3; i++) {
            a = bench_run("parent .. string eval", run_string, &pc,
                          PARENT_ITERS);
            b = bench_run("parent .. compiled eval", run_compiled, &pc,
                          PARENT_ITERS);
        }
        bench_print_result(&a);
        bench_print_result(&b);
    }

    leptris_xpath_result_free(first);
    leptris_document_free(doc);
    free(xml);
    return 0;
}
