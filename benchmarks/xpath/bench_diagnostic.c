/* xpath/bench_diagnostic.c — Diagnostic benchmarks to isolate XPath
 * perf cost components.
 *
 * The aggregate axes / functions benchmarks mix too many costs:
 * parse, namespace init, context alloc, axis dispatch, node-test,
 * predicate, result alloc, free. To know what to optimize, we need
 * to measure each piece separately.
 *
 * This benchmark reports:
 *   - cold vs warm eval (AST cache + bytecode cache effect)
 *   - per-call setup floor (parse-only, eval-only, alloc-only)
 *   - axis-traversal cost in isolation (subtract setup)
 *   - predicate cost (same query with vs without [@id])
 *   - named-attribute vs wildcard mystery
 *   - variable reference overhead
 *   - comparison-operator overhead
 *   - micro-doc vs medium-doc scaling
 *
 * Each measurement is reported as (a) wall-clock per call and
 * (b) ratio vs the cheapest measurement in the same group.
 *
 * Baseline expectations (libxml2 parity targets):
 *   - cold eval of self::* on micro doc: < 5 µs (currently ~30 µs)
 *   - warm eval of self::* on micro doc: < 1 µs (currently ~5 µs)
 *   - warm eval of child::name: < 1.5 µs
 *   - warm eval of descendant::* on medium doc: < 3 µs
 *
 * See TODO 123 (benchmark suite) and TODO 124 (xpath-domination plan).
 */
#include "../common/benchmark.h"
#include "../common/test_data.h"
#include <taurus.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Print a section header (the common harness doesn't have one). */
static void print_section(const char* title) {
    printf("\n--- %s ---\n", title);
}

/* ----------------------------------------------------------------------- *
 * Group 1: per-call setup floor
 *
 * These benchmarks measure the cost of just calling into the API,
 * with no actual XPath work. The cost reveals the per-call overhead
 * from context allocation, namespace init, result alloc, free.
 * ----------------------------------------------------------------------- */

/* Micro doc — small enough that document walk cost is negligible
 * compared to setup overhead. */
static const char* kMicroDoc = "<r><a x='1'>t</a></r>";
static const size_t kMicroDocLen = 24;

typedef struct {
    TaurusDocument doc;
    TaurusElement root;
} bench_ctx_t;

static void setup_micro(void* p) {
    bench_ctx_t* c = (bench_ctx_t*)p;
    TaurusStatus st = TAURUS_OK;
    c->doc = taurus_parse_string(kMicroDoc, kMicroDocLen, &st);
    c->root = taurus_document_root(c->doc);
}

static void teardown_micro(void* p) {
    bench_ctx_t* c = (bench_ctx_t*)p;
    taurus_document_free(c->doc);
}

/* Baseline: parse-only. Reveals the parse cost without any XPath. */
static void bench_parse_only(void* p) {
    (void)p;
    TaurusStatus st = TAURUS_OK;
    TaurusDocument d = taurus_parse_string(kMicroDoc, kMicroDocLen, &st);
    taurus_document_free(d);
}

/* Self-axis on micro doc. This should be near-instant; the cost
 * is pure per-call overhead. */
static void bench_self_axis_warm(void* p) {
    bench_ctx_t* c = (bench_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "self::*");
    taurus_xpath_result_free(r);
}

/* Boolean literal — no actual evaluation, just dispatch + result. */
static void bench_literal_true(void* p) {
    bench_ctx_t* c = (bench_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "true()");
    taurus_xpath_result_free(r);
}

/* Number literal — same. */
static void bench_literal_number(void* p) {
    bench_ctx_t* c = (bench_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "42");
    taurus_xpath_result_free(r);
}

/* ----------------------------------------------------------------------- *
 * Group 2: cold vs warm cache
 *
 * Each unique expression is parsed and compiled once, then cached.
 * The bench harness runs N iterations of the same expression, so
 * iter 1 is cold and iter 2+ is warm. To measure the cold cost
 * alone, we use a unique expression per iteration (force cache miss).
 * ----------------------------------------------------------------------- */

/* Counter to generate unique expression strings. */
static char g_unique_expr[64];
static int g_unique_counter = 0;

static void bench_unique_expr_cold(void* p) {
    bench_ctx_t* c = (bench_ctx_t*)p;
    /* Generate a unique expression: `42`, `43`, ... wraps via modulo.
     * Numbers 0..999 are unlikely to all be cached at once, so this
     * thrashes the 16-slot AST cache. */
    snprintf(g_unique_expr, sizeof(g_unique_expr), "%d", 42000 + g_unique_counter);
    g_unique_counter = (g_unique_counter + 1) % 1000;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, g_unique_expr);
    taurus_xpath_result_free(r);
}

/* Same expression every iter — fully cached. */
static void bench_same_expr_warm(void* p) {
    bench_ctx_t* c = (bench_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "42");
    taurus_xpath_result_free(r);
}

/* ----------------------------------------------------------------------- *
 * Group 3: axis traversal cost (subtract setup floor)
 *
 * Use the medium fixture (~5 KB catalog). Same expression every
 * iter, so cache is warm. Cost beyond the setup floor is axis work.
 * ----------------------------------------------------------------------- */

typedef struct {
    TaurusDocument doc;
    TaurusElement root;
} medium_ctx_t;

static void setup_medium(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    TaurusStatus st = TAURUS_OK;
    c->doc = taurus_parse_string(BENCH_XML_MEDIUM, strlen(BENCH_XML_MEDIUM), &st);
    c->root = taurus_document_root(c->doc);
}

static void teardown_medium(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    taurus_document_free(c->doc);
}

typedef struct {
    TaurusDocument doc;
    TaurusElement root;
} large_ctx_t;

static void setup_large(void* p) {
    large_ctx_t* c = (large_ctx_t*)p;
    TaurusStatus st = TAURUS_OK;
    c->doc = taurus_parse_string(BENCH_XML_LARGE, strlen(BENCH_XML_LARGE), &st);
    c->root = taurus_document_root(c->doc);
}

static void teardown_large(void* p) {
    large_ctx_t* c = (large_ctx_t*)p;
    taurus_document_free(c->doc);
}

/* self::* on each doc size — the cost difference is the document-walk
 * tax levied by xpath_context_init_from_document. */
static void bench_large_self(void* p) {
    large_ctx_t* c = (large_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "self::*");
    taurus_xpath_result_free(r);
}

static void bench_medium_child_wild(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "child::*");
    taurus_xpath_result_free(r);
}

static void bench_medium_child_name(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "child::book");
    taurus_xpath_result_free(r);
}

static void bench_medium_descendant_wild(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "descendant::*");
    taurus_xpath_result_free(r);
}

static void bench_medium_descendant_name(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "descendant::title");
    taurus_xpath_result_free(r);
}

static void bench_medium_self(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "self::*");
    taurus_xpath_result_free(r);
}

/* ----------------------------------------------------------------------- *
 * Group 4: predicate cost
 *
 * Same axis with and without predicate. Difference = predicate cost.
 * ----------------------------------------------------------------------- */

static void bench_medium_descendant_no_pred(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "descendant::*");
    taurus_xpath_result_free(r);
}

static void bench_medium_descendant_attr_exist(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "descendant::*[@id]");
    taurus_xpath_result_free(r);
}

static void bench_medium_descendant_position_pred(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "descendant::*[1]");
    taurus_xpath_result_free(r);
}

/* ----------------------------------------------------------------------- *
 * Group 5: named-attribute mystery
 *
 * bench_xpath_axes reported attribute::id (8.9 µs) slower than
 * attribute::* (5.9 µs). The named path should be faster (one
 * string compare) not slower. Isolate the cause.
 * ----------------------------------------------------------------------- */

static void bench_medium_attr_wild(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "attribute::*");
    taurus_xpath_result_free(r);
}

static void bench_medium_attr_named(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "attribute::id");
    taurus_xpath_result_free(r);
}

static void bench_medium_attr_short(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "@id");
    taurus_xpath_result_free(r);
}

/* ----------------------------------------------------------------------- *
 * Group 6: comparison operators
 *
 * Predicate-form: `[@price > 30]`. The compiler inlines scalar
 * comparisons but falls back for nodeset comparisons. Measure
 * both paths.
 * ----------------------------------------------------------------------- */

static void bench_medium_pred_literal_cmp(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    /* RHS is a literal — compiler can inline. */
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root,
                                              "count(1 = 1)");
    taurus_xpath_result_free(r);
}

static void bench_medium_pred_nodeset_cmp(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    /* LHS is a path (nodeset) — falls back to evaluate_expr. */
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root,
                                              "count(//book[@id = 'b1'])");
    taurus_xpath_result_free(r);
}

/* ----------------------------------------------------------------------- *
 * Group 6b: absolute-path first-step specialization (TODO 129).
 *
 * These run the absolute path AS THE TOP-LEVEL expression so the
 * VM specialization kicks in. (count() wraps the path in a function
 * call, which currently evaluates the arg via AST — the
 * specialization doesn't apply there yet.)
 * ----------------------------------------------------------------------- */

static void bench_medium_absolute_root_match(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    /* `/catalog` → BC_ABSOLUTE_ROOT_MATCH_NAME. */
    TaurusXPathResult r = taurus_xpath_eval(c->doc, NULL, "/catalog");
    taurus_xpath_result_free(r);
}

static void bench_medium_absolute_descendant_or_self(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    /* `//book` → BC_ABSOLUTE_DESCENDANT_OR_SELF_NAME. */
    TaurusXPathResult r = taurus_xpath_eval(c->doc, NULL, "//book");
    taurus_xpath_result_free(r);
}

static void bench_medium_absolute_descendant_wild(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    /* `//*` → BC_ABSOLUTE_DESCENDANT_OR_SELF_WILD. */
    TaurusXPathResult r = taurus_xpath_eval(c->doc, NULL, "//*");
    taurus_xpath_result_free(r);
}

/* ----------------------------------------------------------------------- *
 * Group 7: variable references
 *
 * Variables bypass the cache (need a context-bound value set).
 * Measure the cost vs the same expression with a literal.
 * ----------------------------------------------------------------------- */

static void bench_medium_var_ref(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    TaurusXPathVariableSet vars = taurus_xpath_variable_set_new();
    taurus_xpath_variable_set_number(vars, "n", 42.0);
    TaurusXPathResult r = taurus_xpath_eval_with_vars(c->doc, "$n", vars);
    taurus_xpath_result_free(r);
    taurus_xpath_variable_set_free(vars);
}

static void bench_medium_literal(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "42");
    taurus_xpath_result_free(r);
}

/* ----------------------------------------------------------------------- *
 * Group 8: micro-doc scaling
 *
 * Per-call floor on a 24-byte doc vs a 5 KB doc. If namespace-walk
 * is the cost, the small doc should be much faster.
 * ----------------------------------------------------------------------- */

static void bench_micro_eval(void* p) {
    bench_ctx_t* c = (bench_ctx_t*)p;
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "child::a");
    taurus_xpath_result_free(r);
}

static void bench_medium_eval_same(void* p) {
    medium_ctx_t* c = (medium_ctx_t*)p;
    /* Same expression shape but on the medium doc — isolates the
     * per-call setup cost that scales with document size. */
    TaurusXPathResult r = taurus_xpath_eval(c->doc, c->root, "child::book");
    taurus_xpath_result_free(r);
}

/* ----------------------------------------------------------------------- *
 * Runner
 * ----------------------------------------------------------------------- */

#define RUN(name, fn, ctx, iters) do { \
    BenchResult _r = bench_run((name), (fn), (ctx), (iters)); \
    bench_print_result(&_r); \
} while (0)

int main(void) {
    bench_ctx_t micro = {0};
    medium_ctx_t med = {0};
    large_ctx_t large = {0};
    setup_micro(&micro);
    setup_medium(&med);
    setup_large(&large);

    const size_t iters = 5000;

    bench_print_header("XPath Diagnostic Benchmarks");

    print_section("Group 1: per-call setup floor (micro doc, 24 bytes)");
    RUN("parse-only (no XPath)",   bench_parse_only,        NULL,    iters);
    RUN("self::* (warm)",          bench_self_axis_warm,   &micro,  iters);
    RUN("true() (literal)",        bench_literal_true,     &micro,  iters);
    RUN("42 (literal)",            bench_literal_number,   &micro,  iters);

    print_section("Group 2: cold vs warm cache");
    RUN("unique number expr (cold)", bench_unique_expr_cold, &micro, iters);
    RUN("same number expr (warm)",   bench_same_expr_warm,   &micro, iters);

    print_section("Group 3: axis traversal (medium ~5 KB doc)");
    RUN("child::*",                bench_medium_child_wild,      &med, iters);
    RUN("child::book",             bench_medium_child_name,      &med, iters);
    RUN("descendant::*",           bench_medium_descendant_wild, &med, iters);
    RUN("descendant::title",       bench_medium_descendant_name, &med, iters);
    RUN("self::* (medium)",        bench_medium_self,            &med, iters);

    print_section("Group 4: predicate cost");
    RUN("descendant::* (no pred)",       bench_medium_descendant_no_pred,      &med, iters);
    RUN("descendant::*[@id]",            bench_medium_descendant_attr_exist,   &med, iters);
    RUN("descendant::*[1]",              bench_medium_descendant_position_pred,&med, iters);

    print_section("Group 5: named-attribute mystery");
    RUN("attribute::*",           bench_medium_attr_wild,  &med, iters);
    RUN("attribute::id",          bench_medium_attr_named, &med, iters);
    RUN("@id (short form)",       bench_medium_attr_short, &med, iters);

    print_section("Group 6: comparison operators");
    RUN("count(1 = 1) (scalar)",   bench_medium_pred_literal_cmp, &med, iters);
    RUN("count(//book[@id='b1'])", bench_medium_pred_nodeset_cmp, &med, iters);

    print_section("Group 6b: absolute-path first-step specialization");
    RUN("/catalog (root match)",          bench_medium_absolute_root_match,         &med, iters);
    RUN("//book (descendant-or-self)",    bench_medium_absolute_descendant_or_self, &med, iters);
    RUN("//* (descendant-or-self wild)",  bench_medium_absolute_descendant_wild,    &med, iters);

    print_section("Group 7: variable references");
    RUN("42 (literal)",            bench_medium_literal, &med, iters);
    RUN("$n (variable)",           bench_medium_var_ref, &med, iters);

    print_section("Group 8: doc-size scaling for self::* (smoking gun)");
    RUN("micro (~24 B) self::*",   bench_self_axis_warm, &micro,  iters);
    RUN("medium (~5 KB) self::*",  bench_medium_self,    &med,    iters);
    RUN("large (~100 KB) self::*", bench_large_self,     &large,  iters);

    teardown_micro(&micro);
    teardown_medium(&med);
    teardown_large(&large);

    printf("\nDiagnostic benchmarks complete.\n");
    return 0;
}
