/* xpath/bench_pugixml.cpp — XPath evaluation vs pugixml.
 *
 * Fills the gap noted in TODO 107: every XPath bench in the suite
 * compared leptris to libxml2 only.  pugixml has its own XPath
 * implementation (a separate compiler + evaluator) and is widely
 * used; not measuring it left a major blind spot.
 *
 * Queries are the same set used by bench_leptris.c / bench_libxml2.c
 * so the three numbers are directly comparable.
 */

#include "../common/benchmark.h"
#include "../common/test_data.h"

#include <leptris.h>

#include <pugixml.hpp>

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Leptris XPath                                                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    const char* xml;
    size_t len;
    const char* query;
} leptris_xpath_ctx_t;

static void leptris_xpath_eval(void* ctx) {
    leptris_xpath_ctx_t* c = (leptris_xpath_ctx_t*)ctx;
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(c->xml, c->len, &st);
    if (!doc) return;

    LeptrisXPathResult result = leptris_xpath_eval(doc, NULL, c->query);
    if (result) {
        /* Force evaluation to complete — leptris_xpath_eval is lazy
         * for nodeset results in some paths.  Querying the type
         * flushes the nodeset. */
        (void)leptris_xpath_result_type(result);
        leptris_xpath_result_free(result);
    }
    leptris_document_free(doc);
}

/* -------------------------------------------------------------------------- */
/* pugixml XPath                                                              */
/* -------------------------------------------------------------------------- */

typedef struct {
    const char* xml;
    size_t len;
    const char* query;
} pugi_xpath_ctx_t;

static void pugi_xpath_eval(void* ctx) {
    pugi_xpath_ctx_t* c = (pugi_xpath_ctx_t*)ctx;
    pugi::xml_document doc;
    doc.load_buffer(c->xml, c->len);

    pugi::xpath_query q(c->query);
    if (!q) return;

    /* Dispatch on return type — pugixml throws if you call select_nodes
     * on a query that doesn't return a node-set.  count() returns a
     * number; the rest of our queries are node-sets. */
    if (q.return_type() == pugi::xpath_type_number) {
        (void)q.evaluate_number(doc);
    } else {
        pugi::xpath_node_set ns = doc.select_nodes(q);
        (void)ns.size();
    }
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void) {
    enum { ITERS_SMALL = 1000, ITERS_LARGE = 200 };

    /* Query set chosen to exercise common patterns:
     *   - count() aggregate
     *   - absolute path
     *   - descendant axis
     *   - predicate filter
     *   - attribute selector
     */
    struct { const char* q; const char* desc; } queries[] = {
        { "count(//book)",                       "count(//book)" },
        { "/library/section/book",               "/library/section/book" },
        { "//book[price > 30]",                  "//book[price > 30]" },
        { "//book[@category='fiction']/title",   "//book[@cat='fiction']/title" },
        { "count(//*)",                          "count(//*)" },
    };

    bench_print_header("XPath Benchmark — leptris vs pugixml");

    /* Use BENCH_XML_MEDIUM which has a library/books structure. */
    leptris_xpath_ctx_t tc = { BENCH_XML_MEDIUM, strlen(BENCH_XML_MEDIUM), NULL };
    pugi_xpath_ctx_t   pc = { BENCH_XML_MEDIUM, strlen(BENCH_XML_MEDIUM), NULL };

    for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) {
        tc.query = queries[i].q;
        pc.query = queries[i].q;
        BenchResult t = bench_run(queries[i].desc, leptris_xpath_eval, &tc, ITERS_SMALL);
        BenchResult p = bench_run(queries[i].desc, pugi_xpath_eval,   &pc, ITERS_SMALL);
        printf("\n  query: %s\n", queries[i].desc);
        bench_print_result(&t);
        bench_print_result(&p);
    }

    return 0;
}
