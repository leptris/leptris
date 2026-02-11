#include "../common/benchmark.h"
#include "../common/test_data.h"
#include <taurus.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    TaurusDocument doc;
    TaurusElement context;
} xpath_ctx_t;

/* ============================================================================
 * String Functions (10 tests)
 * ============================================================================ */

void bench_func_string(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "string(//book[1]/title)");
    taurus_xpath_result_free(result);
}

void bench_func_concat(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "concat('Hello', ' ', 'World')");
    taurus_xpath_result_free(result);
}

void bench_func_starts_with(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "starts-with('Hello', 'He')");
    taurus_xpath_result_free(result);
}

void bench_func_contains(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "contains('Hello World', 'World')");
    taurus_xpath_result_free(result);
}

void bench_func_substring(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "substring('Hello', 2, 3)");
    taurus_xpath_result_free(result);
}

void bench_func_substring_before(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "substring-before('Hello World', ' ')");
    taurus_xpath_result_free(result);
}

void bench_func_substring_after(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "substring-after('Hello World', ' ')");
    taurus_xpath_result_free(result);
}

void bench_func_string_length(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "string-length('Hello')");
    taurus_xpath_result_free(result);
}

void bench_func_normalize_space(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "normalize-space('  Hello   World  ')");
    taurus_xpath_result_free(result);
}

void bench_func_translate(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "translate('Hello', 'el', 'EL')");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Number Functions (5 tests)
 * ============================================================================ */

void bench_func_number(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "number('123.45')");
    taurus_xpath_result_free(result);
}

void bench_func_sum(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "sum(//book/price)");
    taurus_xpath_result_free(result);
}

void bench_func_floor(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "floor(3.7)");
    taurus_xpath_result_free(result);
}

void bench_func_ceiling(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "ceiling(3.2)");
    taurus_xpath_result_free(result);
}

void bench_func_round(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "round(3.5)");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Boolean Functions (5 tests)
 * ============================================================================ */

void bench_func_boolean(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "boolean(//book)");
    taurus_xpath_result_free(result);
}

void bench_func_not(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "not(false())");
    taurus_xpath_result_free(result);
}

void bench_func_true(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "true()");
    taurus_xpath_result_free(result);
}

void bench_func_false(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "false()");
    taurus_xpath_result_free(result);
}

void bench_func_lang(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "lang('en')");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Nodeset Functions (7 tests)
 * ============================================================================ */

void bench_func_last(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "//book[last()]");
    taurus_xpath_result_free(result);
}

void bench_func_position(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "//book[position() = 1]");
    taurus_xpath_result_free(result);
}

void bench_func_count(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "count(//book)");
    taurus_xpath_result_free(result);
}

void bench_func_id(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "id('101')");
    taurus_xpath_result_free(result);
}

void bench_func_local_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "local-name(//book[1])");
    taurus_xpath_result_free(result);
}

void bench_func_namespace_uri(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "namespace-uri(//book[1])");
    taurus_xpath_result_free(result);
}

void bench_func_name(void* ctx) {
    xpath_ctx_t* xctx = (xpath_ctx_t*)ctx;
    TaurusXPathResult result = taurus_xpath_eval(xctx->doc, xctx->context, "name(//book[1])");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Main Benchmark Runner
 * ============================================================================ */

int main(void) {
    const size_t ITERATIONS = 1000;

    /* Parse test document */
    TaurusDocument doc = taurus_parse_string(BENCH_XML_MEDIUM, BENCH_XML_MEDIUM_LEN, NULL);
    if (!doc) {
        fprintf(stderr, "Failed to parse test document\n");
        return 1;
    }

    /* Get root element for context */
    TaurusElement root = taurus_document_root(doc);
    xpath_ctx_t ctx = { doc, root };

    /* Allocate results array (27 tests - all XPath 1.0 functions) */
    BenchResult results[27];
    size_t idx = 0;

    printf("\n");
    printf("================================================================\n");
    printf("Taurus XPath Functions Benchmark\n");
    printf("================================================================\n");
    printf("Iterations: %zu\n", ITERATIONS);
    printf("Test count: 27 (all XPath 1.0 functions)\n");
    printf("================================================================\n\n");

    /* String functions (10 - all enabled) */
    results[idx++] = bench_run("string()", bench_func_string, &ctx, ITERATIONS);
    results[idx++] = bench_run("concat()", bench_func_concat, &ctx, ITERATIONS);
    results[idx++] = bench_run("starts-with()", bench_func_starts_with, &ctx, ITERATIONS);
    results[idx++] = bench_run("contains()", bench_func_contains, &ctx, ITERATIONS);
    results[idx++] = bench_run("substring()", bench_func_substring, &ctx, ITERATIONS);
    results[idx++] = bench_run("substring-before()", bench_func_substring_before, &ctx, ITERATIONS);
    results[idx++] = bench_run("substring-after()", bench_func_substring_after, &ctx, ITERATIONS);
    results[idx++] = bench_run("string-length()", bench_func_string_length, &ctx, ITERATIONS);
    results[idx++] = bench_run("normalize-space()", bench_func_normalize_space, &ctx, ITERATIONS);
    results[idx++] = bench_run("translate()", bench_func_translate, &ctx, ITERATIONS);

    /* Number functions */
    results[idx++] = bench_run("number()", bench_func_number, &ctx, ITERATIONS);
    results[idx++] = bench_run("sum()", bench_func_sum, &ctx, ITERATIONS);
    results[idx++] = bench_run("floor()", bench_func_floor, &ctx, ITERATIONS);
    results[idx++] = bench_run("ceiling()", bench_func_ceiling, &ctx, ITERATIONS);
    results[idx++] = bench_run("round()", bench_func_round, &ctx, ITERATIONS);

    /* Boolean functions */
    results[idx++] = bench_run("boolean()", bench_func_boolean, &ctx, ITERATIONS);
    results[idx++] = bench_run("not()", bench_func_not, &ctx, ITERATIONS);
    results[idx++] = bench_run("true()", bench_func_true, &ctx, ITERATIONS);
    results[idx++] = bench_run("false()", bench_func_false, &ctx, ITERATIONS);
    results[idx++] = bench_run("lang()", bench_func_lang, &ctx, ITERATIONS);

    /* Nodeset functions */
    results[idx++] = bench_run("last()", bench_func_last, &ctx, ITERATIONS);
    results[idx++] = bench_run("position()", bench_func_position, &ctx, ITERATIONS);
    results[idx++] = bench_run("count()", bench_func_count, &ctx, ITERATIONS);
    results[idx++] = bench_run("id()", bench_func_id, &ctx, ITERATIONS);
    results[idx++] = bench_run("local-name()", bench_func_local_name, &ctx, ITERATIONS);
    results[idx++] = bench_run("namespace-uri()", bench_func_namespace_uri, &ctx, ITERATIONS);
    results[idx++] = bench_run("name()", bench_func_name, &ctx, ITERATIONS);

    /* Print results */
    for (size_t i = 0; i < idx; i++) {
        bench_print_result(&results[i]);
    }

    /* Write JSON results */
    bench_write_json("taurus", results, idx, "results/xpath_functions_taurus.json");

    /* Cleanup */
    taurus_document_free(doc);

    printf("\n✅ Benchmark complete: 27 function tests\n\n");
    return 0;
}