#include "../common/benchmark.h"
#include "../common/test_data.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <stdio.h>
#include <string.h>

/* Context for XPath benchmarks */
typedef struct {
    xmlDocPtr doc;
    xmlXPathContextPtr xpath_ctx;
    const char* expr;
} xpath_ctx_t;

/* Generic XPath benchmark function */
void bench_xpath_query(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    xmlXPathObjectPtr result = xmlXPathEvalExpression(
        (const xmlChar*)xctx->expr, xctx->xpath_ctx);
    if (result) {
        xmlXPathFreeObject(result);
    }
}

int main(void) {
    const size_t ITERATIONS = 1000;

    /* Initialize libxml2 */
    LIBXML_TEST_VERSION

    /* Parse document once */
    xmlDocPtr doc = xmlReadMemory(BENCH_XML_MEDIUM, (int)strlen(BENCH_XML_MEDIUM),
                                   NULL, NULL, 0);
    if (!doc) {
        fprintf(stderr, "Failed to parse test XML\n");
        return 1;
    }

    /* Create XPath context */
    xmlXPathContextPtr xpath_ctx = xmlXPathNewContext(doc);
    if (!xpath_ctx) {
        fprintf(stderr, "Failed to create XPath context\n");
        xmlFreeDoc(doc);
        return 1;
    }

    printf("\n");
    printf("================================================================\n");
    printf("libxml2 XPath Benchmarks\n");
    printf("================================================================\n");
    printf("Test Data: Medium XML (~10KB)\n");
    printf("Iterations: %zu per benchmark\n", ITERATIONS);
    printf("================================================================\n");

    /* Benchmark 1: Simple Path - //book */
    const char* expr1 = "//book";
    xpath_ctx_t ctx1 = { doc, xpath_ctx, expr1 };
    BenchResult r1 = bench_run("Simple Path (//book)", bench_xpath_query, &ctx1, ITERATIONS);
    bench_print_result(&r1);

    /* Benchmark 2: Predicate - //book[@id='101'] */
    const char* expr2 = "//book[@id='101']";
    xpath_ctx_t ctx2 = { doc, xpath_ctx, expr2 };
    BenchResult r2 = bench_run("Predicate ([@id='101'])", bench_xpath_query, &ctx2, ITERATIONS);
    bench_print_result(&r2);

    /* Benchmark 3: Function - count(//book) */
    const char* expr3 = "count(//book)";
    xpath_ctx_t ctx3 = { doc, xpath_ctx, expr3 };
    BenchResult r3 = bench_run("Function (count())", bench_xpath_query, &ctx3, ITERATIONS);
    bench_print_result(&r3);

    /* Benchmark 4: Complex Query - //book[price > 30]/title */
    const char* expr4 = "//book[number(price) > 30]/title";
    xpath_ctx_t ctx4 = { doc, xpath_ctx, expr4 };
    BenchResult r4 = bench_run("Complex Query", bench_xpath_query, &ctx4, ITERATIONS);
    bench_print_result(&r4);

    /* Benchmark 5: Union - //book | //magazine */
    const char* expr5 = "//book | //magazine";
    xpath_ctx_t ctx5 = { doc, xpath_ctx, expr5 };
    BenchResult r5 = bench_run("Union (//book | //magazine)", bench_xpath_query, &ctx5, ITERATIONS);
    bench_print_result(&r5);

    /* Print summary */
    BenchResult results[] = { r1, r2, r3, r4, r5 };
    bench_print_summary("libxml2 XPath", results, 5);

    /* Cleanup */
    xmlXPathFreeContext(xpath_ctx);
    xmlFreeDoc(doc);
    xmlCleanupParser();

    printf("\n");
    return 0;
}