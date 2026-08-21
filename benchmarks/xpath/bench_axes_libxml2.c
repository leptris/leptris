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
    printf("libxml2 XPath Axes Benchmark\n");
    printf("================================================================\n");
    printf("Iterations: %zu\n", ITERATIONS);
    printf("Test count: 39 (13 axes × 3 variants)\n");
    printf("================================================================\n\n");

    /* Allocate results array (39 tests) */
    BenchResult results[39];
    size_t idx = 0;

    /* Define all test expressions matching Leptris benchmark */
    const char* expressions[] = {
        /* Child axis */
        "child::*", "child::book", "child::*[@id]",
        /* Descendant axis */
        "descendant::*", "descendant::title", "descendant::*[@id]",
        /* Descendant-or-self axis */
        "descendant-or-self::*", "descendant-or-self::book", "descendant-or-self::*[@id]",
        /* Parent axis */
        "parent::*", "parent::catalog", "parent::*[@version]",
        /* Ancestor axis */
        "ancestor::*", "ancestor::catalog", "ancestor::*[@version]",
        /* Ancestor-or-self axis */
        "ancestor-or-self::*", "ancestor-or-self::book", "ancestor-or-self::*[@id]",
        /* Following-sibling axis */
        "following-sibling::*", "following-sibling::book", "following-sibling::*[@id]",
        /* Preceding-sibling axis */
        "preceding-sibling::*", "preceding-sibling::book", "preceding-sibling::*[@id]",
        /* Following axis */
        "following::*", "following::title", "following::*[@id]",
        /* Preceding axis */
        "preceding::*", "preceding::title", "preceding::*[@id]",
        /* Attribute axis */
        "attribute::*", "attribute::id", "@*[string-length() > 0]",
        /* Namespace axis */
        "namespace::*", "namespace::xml", "namespace::*[local-name() != 'xml']",
        /* Self axis */
        "self::*", "self::book", "self::*[@id]"
    };

    /* Run all benchmarks */
    for (size_t i = 0; i < 39; i++) {
        xpath_ctx_t ctx = { doc, xpath_ctx, expressions[i] };
        results[idx++] = bench_run(expressions[i], bench_xpath_query, &ctx, ITERATIONS);
        bench_print_result(&results[idx - 1]);
    }

    /* Write JSON results */
    bench_write_json("libxml2", results, idx, "results/xpath_axes_libxml2.json");

    /* Cleanup */
    xmlXPathFreeContext(xpath_ctx);
    xmlFreeDoc(doc);
    xmlCleanupParser();

    printf("\n✅ Benchmark complete: 39 axes tests\n\n");
    return 0;
}