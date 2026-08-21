#include "../common/benchmark.h"
#include "../common/test_data.h"
#include <leptris.h>
#include <stdio.h>
#include <string.h>
#include <string.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    LeptrisDocument doc;
    LeptrisElement context;
} xpath_ctx_t;

/* ============================================================================
 * Child Axis Tests (3 variants)
 * ============================================================================ */

void bench_child_simple(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "child::*");
    leptris_xpath_result_free(result);
}

void bench_child_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "child::book");
    leptris_xpath_result_free(result);
}

void bench_child_predicate(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "child::*[@id]");
    leptris_xpath_result_free(result);
}

/* ============================================================================
 * Descendant Axis Tests (3 variants)
 * ============================================================================ */

void bench_descendant_simple(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "descendant::*");
    leptris_xpath_result_free(result);
}

void bench_descendant_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "descendant::title");
    leptris_xpath_result_free(result);
}

void bench_descendant_predicate(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "descendant::*[@id]");
    leptris_xpath_result_free(result);
}

/* ============================================================================
 * Descendant-or-Self Axis Tests (3 variants)
 * ============================================================================ */

void bench_descendant_or_self_simple(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "descendant-or-self::*");
    leptris_xpath_result_free(result);
}

void bench_descendant_or_self_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "descendant-or-self::book");
    leptris_xpath_result_free(result);
}

void bench_descendant_or_self_predicate(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "descendant-or-self::*[@id]");
    leptris_xpath_result_free(result);
}

/* ============================================================================
 * Parent Axis Tests (3 variants)
 * ============================================================================ */

void bench_parent_simple(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "parent::*");
    leptris_xpath_result_free(result);
}

void bench_parent_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "parent::catalog");
    leptris_xpath_result_free(result);
}

void bench_parent_predicate(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "parent::*[@version]");
    leptris_xpath_result_free(result);
}

/* ============================================================================
 * Ancestor Axis Tests (3 variants)
 * ============================================================================ */

void bench_ancestor_simple(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "ancestor::*");
    leptris_xpath_result_free(result);
}

void bench_ancestor_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "ancestor::catalog");
    leptris_xpath_result_free(result);
}

void bench_ancestor_predicate(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "ancestor::*[@version]");
    leptris_xpath_result_free(result);
}

/* ============================================================================
 * Ancestor-or-Self Axis Tests (3 variants)
 * ============================================================================ */

void bench_ancestor_or_self_simple(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "ancestor-or-self::*");
    leptris_xpath_result_free(result);
}

void bench_ancestor_or_self_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "ancestor-or-self::book");
    leptris_xpath_result_free(result);
}

void bench_ancestor_or_self_predicate(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "ancestor-or-self::*[@id]");
    leptris_xpath_result_free(result);
}

/* ============================================================================
 * Following-Sibling Axis Tests (3 variants)
 * ============================================================================ */

void bench_following_sibling_simple(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "following-sibling::*");
    leptris_xpath_result_free(result);
}

void bench_following_sibling_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "following-sibling::book");
    leptris_xpath_result_free(result);
}

void bench_following_sibling_predicate(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "following-sibling::*[@id]");
    leptris_xpath_result_free(result);
}

/* ============================================================================
 * Preceding-Sibling Axis Tests (3 variants)
 * ============================================================================ */

void bench_preceding_sibling_simple(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "preceding-sibling::*");
    leptris_xpath_result_free(result);
}

void bench_preceding_sibling_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "preceding-sibling::book");
    leptris_xpath_result_free(result);
}

void bench_preceding_sibling_predicate(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "preceding-sibling::*[@id]");
    leptris_xpath_result_free(result);
}

/* ============================================================================
 * Following Axis Tests (3 variants)
 * ============================================================================ */

void bench_following_simple(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "following::*");
    leptris_xpath_result_free(result);
}

void bench_following_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "following::title");
    leptris_xpath_result_free(result);
}

void bench_following_predicate(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "following::*[@id]");
    leptris_xpath_result_free(result);
}

/* ============================================================================
 * Preceding Axis Tests (3 variants)
 * ============================================================================ */

void bench_preceding_simple(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "preceding::*");
    leptris_xpath_result_free(result);
}

void bench_preceding_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "preceding::title");
    leptris_xpath_result_free(result);
}

void bench_preceding_predicate(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "preceding::*[@id]");
    leptris_xpath_result_free(result);
}

/* ============================================================================
 * Attribute Axis Tests (3 variants)
 * ============================================================================ */

void bench_attribute_simple(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "attribute::*");
    leptris_xpath_result_free(result);
}

void bench_attribute_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "attribute::id");
    leptris_xpath_result_free(result);
}

void bench_attribute_predicate(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "@*[string-length() > 0]");
    leptris_xpath_result_free(result);
}

/* ============================================================================
 * Namespace Axis Tests (3 variants)
 * ============================================================================ */

void bench_namespace_simple(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "namespace::*");
    leptris_xpath_result_free(result);
}

void bench_namespace_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "namespace::xml");
    leptris_xpath_result_free(result);
}

void bench_namespace_predicate(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "namespace::*[local-name() != 'xml']");
    leptris_xpath_result_free(result);
}

/* ============================================================================
 * Self Axis Tests (3 variants)
 * ============================================================================ */

void bench_self_simple(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "self::*");
    leptris_xpath_result_free(result);
}

void bench_self_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "self::book");
    leptris_xpath_result_free(result);
}

void bench_self_predicate(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    LeptrisXPathResult result = leptris_xpath_eval(xctx->doc, xctx->context, "self::*[@id]");
    leptris_xpath_result_free(result);
}

/* ============================================================================
 * Main Benchmark Runner
 * ============================================================================ */

int main(void) {
    const size_t ITERATIONS = 1000;

    /* Parse test document */
    LeptrisDocument doc = leptris_parse_string(BENCH_XML_MEDIUM, strlen(BENCH_XML_MEDIUM), NULL);
    if (!doc) {
        fprintf(stderr, "Failed to parse test document\n");
        return 1;
    }

    /* Get root element for context */
    LeptrisElement root = leptris_document_root(doc);
    xpath_ctx_t ctx = { doc, root };

    /* Allocate results array (39 tests) */
    BenchResult results[39];
    size_t idx = 0;

    printf("\n");
    printf("================================================================\n");
    printf("Leptris XPath Axes Benchmark\n");
    printf("================================================================\n");
    printf("Iterations: %zu\n", ITERATIONS);
    printf("Test count: 39 (13 axes × 3 variants)\n");
    printf("================================================================\n\n");

    /* Run all axis benchmarks */

    /* Child axis */
    results[idx++] = bench_run("child::*", bench_child_simple, &ctx, ITERATIONS);
    results[idx++] = bench_run("child::book", bench_child_name, &ctx, ITERATIONS);
    results[idx++] = bench_run("child::*[@id]", bench_child_predicate, &ctx, ITERATIONS);

    /* Descendant axis */
    results[idx++] = bench_run("descendant::*", bench_descendant_simple, &ctx, ITERATIONS);
    results[idx++] = bench_run("descendant::title", bench_descendant_name, &ctx, ITERATIONS);
    results[idx++] = bench_run("descendant::*[@id]", bench_descendant_predicate, &ctx, ITERATIONS);

    /* Descendant-or-self axis */
    results[idx++] = bench_run("descendant-or-self::*", bench_descendant_or_self_simple, &ctx, ITERATIONS);
    results[idx++] = bench_run("descendant-or-self::book", bench_descendant_or_self_name, &ctx, ITERATIONS);
    results[idx++] = bench_run("descendant-or-self::*[@id]", bench_descendant_or_self_predicate, &ctx, ITERATIONS);

    /* Parent axis */
    results[idx++] = bench_run("parent::*", bench_parent_simple, &ctx, ITERATIONS);
    results[idx++] = bench_run("parent::catalog", bench_parent_name, &ctx, ITERATIONS);
    results[idx++] = bench_run("parent::*[@version]", bench_parent_predicate, &ctx, ITERATIONS);

    /* Ancestor axis */
    results[idx++] = bench_run("ancestor::*", bench_ancestor_simple, &ctx, ITERATIONS);
    results[idx++] = bench_run("ancestor::catalog", bench_ancestor_name, &ctx, ITERATIONS);
    results[idx++] = bench_run("ancestor::*[@version]", bench_ancestor_predicate, &ctx, ITERATIONS);

    /* Ancestor-or-self axis */
    results[idx++] = bench_run("ancestor-or-self::*", bench_ancestor_or_self_simple, &ctx, ITERATIONS);
    results[idx++] = bench_run("ancestor-or-self::book", bench_ancestor_or_self_name, &ctx, ITERATIONS);
    results[idx++] = bench_run("ancestor-or-self::*[@id]", bench_ancestor_or_self_predicate, &ctx, ITERATIONS);

    /* Following-sibling axis */
    results[idx++] = bench_run("following-sibling::*", bench_following_sibling_simple, &ctx, ITERATIONS);
    results[idx++] = bench_run("following-sibling::book", bench_following_sibling_name, &ctx, ITERATIONS);
    results[idx++] = bench_run("following-sibling::*[@id]", bench_following_sibling_predicate, &ctx, ITERATIONS);

    /* Preceding-sibling axis */
    results[idx++] = bench_run("preceding-sibling::*", bench_preceding_sibling_simple, &ctx, ITERATIONS);
    results[idx++] = bench_run("preceding-sibling::book", bench_preceding_sibling_name, &ctx, ITERATIONS);
    results[idx++] = bench_run("preceding-sibling::*[@id]", bench_preceding_sibling_predicate, &ctx, ITERATIONS);

    /* Following axis */
    results[idx++] = bench_run("following::*", bench_following_simple, &ctx, ITERATIONS);
    results[idx++] = bench_run("following::title", bench_following_name, &ctx, ITERATIONS);
    results[idx++] = bench_run("following::*[@id]", bench_following_predicate, &ctx, ITERATIONS);

    /* Preceding axis */
    results[idx++] = bench_run("preceding::*", bench_preceding_simple, &ctx, ITERATIONS);
    results[idx++] = bench_run("preceding::title", bench_preceding_name, &ctx, ITERATIONS);
    results[idx++] = bench_run("preceding::*[@id]", bench_preceding_predicate, &ctx, ITERATIONS);

    /* Attribute axis */
    results[idx++] = bench_run("attribute::*", bench_attribute_simple, &ctx, ITERATIONS);
    results[idx++] = bench_run("attribute::id", bench_attribute_name, &ctx, ITERATIONS);
    results[idx++] = bench_run("@*[string-length() > 0]", bench_attribute_predicate, &ctx, ITERATIONS);

    /* Namespace axis */
    results[idx++] = bench_run("namespace::*", bench_namespace_simple, &ctx, ITERATIONS);
    results[idx++] = bench_run("namespace::xml", bench_namespace_name, &ctx, ITERATIONS);
    results[idx++] = bench_run("namespace::*[local-name() != 'xml']", bench_namespace_predicate, &ctx, ITERATIONS);

    /* Self axis */
    results[idx++] = bench_run("self::*", bench_self_simple, &ctx, ITERATIONS);
    results[idx++] = bench_run("self::book", bench_self_name, &ctx, ITERATIONS);
    results[idx++] = bench_run("self::*[@id]", bench_self_predicate, &ctx, ITERATIONS);

    /* Print results */
    for (size_t i = 0; i < idx; i++) {
        bench_print_result(&results[i]);
    }

    /* Write JSON results */
    bench_write_json("leptris", results, idx, "results/xpath_axes_leptris.json");

    /* Cleanup */
    leptris_document_free(doc);

    printf("\n✅ Benchmark complete: 39 axes tests\n\n");
    return 0;
}