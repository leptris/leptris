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
    printf("libxml2 XPath Functions Benchmark\n");
    printf("================================================================\n");
    printf("Iterations: %zu\n", ITERATIONS);
    printf("Test count: 27 (all XPath 1.0 functions)\n");
    printf("================================================================\n\n");

    /* Allocate results array (27 tests) */
    BenchResult results[27];
    size_t idx = 0;

    /* Define all test expressions matching Leptris benchmark */
    const char* expressions[] = {
        /* String functions (10) */
        "string(//book[1]/title)",
        "concat('Hello', ' ', 'World')",
        "starts-with('Hello', 'He')",
        "contains('Hello World', 'World')",
        "substring('Hello', 2, 3)",
        "substring-before('Hello World', ' ')",
        "substring-after('Hello World', ' ')",
        "string-length('Hello')",
        "normalize-space('  Hello   World  ')",
        "translate('Hello', 'el', 'EL')",
        /* Number functions (5) */
        "number('123.45')",
        "sum(//book/price)",
        "floor(3.7)",
        "ceiling(3.2)",
        "round(3.5)",
        /* Boolean functions (5) */
        "boolean(//book)",
        "not(false())",
        "true()",
        "false()",
        "lang('en')",
        /* Nodeset functions (7) */
        "//book[last()]",
        "//book[position() = 1]",
        "count(//book)",
        "id('101')",
        "local-name(//book[1])",
        "namespace-uri(//book[1])",
        "name(//book[1])"
    };

    const char* names[] = {
        "string()", "concat()", "starts-with()", "contains()", "substring()",
        "substring-before()", "substring-after()", "string-length()",
        "normalize-space()", "translate()",
        "number()", "sum()", "floor()", "ceiling()", "round()",
        "boolean()", "not()", "true()", "false()", "lang()",
        "last()", "position()", "count()", "id()", "local-name()",
        "namespace-uri()", "name()"
    };

    /* Run all benchmarks */
    for (size_t i = 0; i < 27; i++) {
        xpath_ctx_t ctx = { doc, xpath_ctx, expressions[i] };
        results[idx++] = bench_run(names[i], bench_xpath_query, &ctx, ITERATIONS);
        bench_print_result(&results[idx - 1]);
    }

    /* Write JSON results */
    bench_write_json("libxml2", results, idx, "results/xpath_functions_libxml2.json");

    /* Cleanup */
    xmlXPathFreeContext(xpath_ctx);
    xmlFreeDoc(doc);
    xmlCleanupParser();

    printf("\n✅ Benchmark complete: 27 function tests\n\n");
    return 0;
}