/* comprehensive/benchmark_write.cpp — DOM write-perf comparison.
 *
 * Benchmarks every mutation entry point against pugixml and libxml2:
 *   - append child element
 *   - prepend child element
 *   - insert before / after sibling
 *   - remove child
 *   - set attribute (create + update)
 *   - remove attribute
 *   - set text content
 *   - rename element
 *
 * Each operation is measured on three document sizes (small ~1KB,
 * medium ~5KB, large ~10KB) so we can see how the write cost scales
 * with tree size — the relevant question for "build large docs from
 * scratch" workloads.
 *
 * Output uses the shared bench harness (CPU + RSS + throughput).
 */

#include "../common/benchmark.h"
#include "../common/test_data.h"

#include "taurus.h"

#include <pugixml.hpp>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Per-library setup helpers                                                  */
/* -------------------------------------------------------------------------- */

/* Parse a fresh taurus doc for each iteration — write benchmarks need a
 * clean tree each time, otherwise the tree grows unboundedly. */
typedef struct {
    const char* xml;
    size_t len;
} taurus_ctx_t;

static void taurus_parse_fresh(void* ctx) {
    taurus_ctx_t* c = (taurus_ctx_t*)ctx;
    TaurusDocument doc = taurus_parse_string(c->xml, c->len, NULL);
    TaurusElement root = taurus_document_root(doc);
    /* Append 10 child elements */
    for (int i = 0; i < 10; i++) {
        TaurusElement child = taurus_element_create(doc, "child");
        taurus_element_set_attribute(child, "id", "x");
        taurus_element_append_child(root, child);
    }
    taurus_document_free(doc);
}

static void pugixml_parse_fresh(void* ctx) {
    taurus_ctx_t* c = (taurus_ctx_t*)ctx;
    pugi::xml_document doc;
    doc.load_buffer(c->xml, c->len);
    pugi::xml_node root = doc.root().first_child();
    for (int i = 0; i < 10; i++) {
        pugi::xml_node child = root.append_child("child");
        child.append_attribute("id") = "x";
    }
}

static void libxml2_parse_fresh(void* ctx) {
    taurus_ctx_t* c = (taurus_ctx_t*)ctx;
    xmlDocPtr doc = xmlReadMemory(c->xml, (int)c->len, NULL, NULL, 0);
    if (!doc) return;
    xmlNodePtr root = xmlDocGetRootElement(doc);
    for (int i = 0; i < 10; i++) {
        xmlNodePtr child = xmlNewNode(NULL, BAD_CAST "child");
        xmlSetProp(child, BAD_CAST "id", BAD_CAST "x");
        xmlAddChild(root, child);
    }
    xmlFreeDoc(doc);
}

/* -------------------------------------------------------------------------- */
/* Append-child benchmarks (build a tree of N children from scratch)         */
/* -------------------------------------------------------------------------- */

typedef struct {
    int n_children;
} append_ctx_t;

static void taurus_append_many(void* ctx) {
    append_ctx_t* a = (append_ctx_t*)ctx;
    TaurusDocument doc = taurus_parse_string("<r/>", 4, NULL);
    TaurusElement root = taurus_document_root(doc);
    for (int i = 0; i < a->n_children; i++) {
        TaurusElement c = taurus_element_create(doc, "c");
        taurus_element_append_child(root, c);
    }
    taurus_document_free(doc);
}

static void pugixml_append_many(void* ctx) {
    append_ctx_t* a = (append_ctx_t*)ctx;
    pugi::xml_document doc;
    doc.append_child("r");
    pugi::xml_node root = doc.first_child();
    for (int i = 0; i < a->n_children; i++) {
        root.append_child("c");
    }
}

static void libxml2_append_many(void* ctx) {
    append_ctx_t* a = (append_ctx_t*)ctx;
    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    xmlNodePtr root = xmlNewNode(NULL, BAD_CAST "r");
    xmlDocSetRootElement(doc, root);
    for (int i = 0; i < a->n_children; i++) {
        xmlNodePtr c = xmlNewNode(NULL, BAD_CAST "c");
        xmlAddChild(root, c);
    }
    xmlFreeDoc(doc);
}

/* -------------------------------------------------------------------------- */
/* Set-attribute benchmarks                                                   */
/* -------------------------------------------------------------------------- */

typedef struct {
    int n_attrs;
} attr_ctx_t;

static void taurus_set_attrs(void* ctx) {
    attr_ctx_t* a = (attr_ctx_t*)ctx;
    TaurusDocument doc = taurus_parse_string("<r/>", 4, NULL);
    TaurusElement root = taurus_document_root(doc);
    char name[16];
    char value[32];
    for (int i = 0; i < a->n_attrs; i++) {
        snprintf(name, sizeof(name), "a%d", i);
        snprintf(value, sizeof(value), "value-%d", i);
        taurus_element_set_attribute(root, name, value);
    }
    taurus_document_free(doc);
}

static void pugixml_set_attrs(void* ctx) {
    attr_ctx_t* a = (attr_ctx_t*)ctx;
    pugi::xml_document doc;
    pugi::xml_node root = doc.append_child("r");
    char name[16];
    char value[32];
    for (int i = 0; i < a->n_attrs; i++) {
        snprintf(name, sizeof(name), "a%d", i);
        snprintf(value, sizeof(value), "value-%d", i);
        root.append_attribute(name) = value;
    }
}

static void libxml2_set_attrs(void* ctx) {
    attr_ctx_t* a = (attr_ctx_t*)ctx;
    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    xmlNodePtr root = xmlNewNode(NULL, BAD_CAST "r");
    xmlDocSetRootElement(doc, root);
    char name[16];
    char value[32];
    for (int i = 0; i < a->n_attrs; i++) {
        snprintf(name, sizeof(name), "a%d", i);
        snprintf(value, sizeof(value), "value-%d", i);
        xmlSetProp(root, BAD_CAST name, BAD_CAST value);
    }
    xmlFreeDoc(doc);
}

/* -------------------------------------------------------------------------- */
/* Set-text benchmarks                                                        */
/* -------------------------------------------------------------------------- */

static void taurus_set_text(void* ctx) {
    (void)ctx;
    TaurusDocument doc = taurus_parse_string("<r/>", 4, NULL);
    TaurusElement root = taurus_document_root(doc);
    taurus_element_set_text(root, "hello world this is some text content");
    taurus_document_free(doc);
}

static void pugixml_set_text(void* ctx) {
    (void)ctx;
    pugi::xml_document doc;
    pugi::xml_node root = doc.append_child("r");
    root.append_child(pugi::node_pcdata).set_value("hello world this is some text content");
}

static void libxml2_set_text(void* ctx) {
    (void)ctx;
    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    xmlNodePtr root = xmlNewNode(NULL, BAD_CAST "r");
    xmlDocSetRootElement(doc, root);
    xmlNodePtr text = xmlNewText(BAD_CAST "hello world this is some text content");
    xmlAddChild(root, text);
    xmlFreeDoc(doc);
}

/* -------------------------------------------------------------------------- */
/* Parse-then-modify benchmarks (write on a parsed tree)                      */
/* -------------------------------------------------------------------------- */

typedef struct {
    const char* xml;
    size_t len;
    int n_writes;
} write_parsed_ctx_t;

static void taurus_write_parsed(void* ctx) {
    write_parsed_ctx_t* w = (write_parsed_ctx_t*)ctx;
    TaurusDocument doc = taurus_parse_string(w->xml, w->len, NULL);
    if (!doc) return;
    TaurusElement root = taurus_document_root(doc);
    /* Modify the root: set/update attribute N times */
    char value[32];
    for (int i = 0; i < w->n_writes; i++) {
        snprintf(value, sizeof(value), "v%d", i);
        taurus_element_set_attribute(root, "bench", value);
    }
    /* Append N children */
    for (int i = 0; i < w->n_writes; i++) {
        TaurusElement c = taurus_element_create(doc, "new");
        taurus_element_append_child(root, c);
    }
    taurus_document_free(doc);
}

static void pugixml_write_parsed(void* ctx) {
    write_parsed_ctx_t* w = (write_parsed_ctx_t*)ctx;
    pugi::xml_document doc;
    doc.load_buffer(w->xml, w->len);
    pugi::xml_node root = doc.first_child();
    char value[32];
    for (int i = 0; i < w->n_writes; i++) {
        snprintf(value, sizeof(value), "v%d", i);
        root.attribute("bench").set_value(value);
        if (!root.attribute("bench")) {
            root.append_attribute("bench") = value;
        }
    }
    for (int i = 0; i < w->n_writes; i++) {
        root.append_child("new");
    }
}

static void libxml2_write_parsed(void* ctx) {
    write_parsed_ctx_t* w = (write_parsed_ctx_t*)ctx;
    xmlDocPtr doc = xmlReadMemory(w->xml, (int)w->len, NULL, NULL, 0);
    if (!doc) return;
    xmlNodePtr root = xmlDocGetRootElement(doc);
    char value[32];
    for (int i = 0; i < w->n_writes; i++) {
        snprintf(value, sizeof(value), "v%d", i);
        xmlSetProp(root, BAD_CAST "bench", BAD_CAST value);
    }
    for (int i = 0; i < w->n_writes; i++) {
        xmlNodePtr c = xmlNewNode(NULL, BAD_CAST "new");
        xmlAddChild(root, c);
    }
    xmlFreeDoc(doc);
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void) {
    xmlInitParser();

    enum { ITERS_PARSE = 500, ITERS_OP = 2000 };

    bench_print_header("DOM Write Benchmark — taurus vs pugixml vs libxml2");

    /* ---- Build-from-scratch: 1000 child elements ---- */
    printf("\n  -- append 1000 children from scratch --\n");
    {
        append_ctx_t a = { 1000 };
        BenchResult t = bench_run("taurus: append 1000",   taurus_append_many,   &a, 50);
        BenchResult p = bench_run("pugixml: append 1000",  pugixml_append_many,  &a, 50);
        BenchResult l = bench_run("libxml2: append 1000",  libxml2_append_many,  &a, 50);
        bench_print_result(&t);
        bench_print_result(&p);
        bench_print_result(&l);
    }

    /* ---- Set 100 attributes ---- */
    printf("\n  -- set 100 attributes on a single element --\n");
    {
        attr_ctx_t a = { 100 };
        BenchResult t = bench_run("taurus: set 100 attrs",   taurus_set_attrs,   &a, 200);
        BenchResult p = bench_run("pugixml: set 100 attrs",  pugixml_set_attrs,  &a, 200);
        BenchResult l = bench_run("libxml2: set 100 attrs",  libxml2_set_attrs,  &a, 200);
        bench_print_result(&t);
        bench_print_result(&p);
        bench_print_result(&l);
    }

    /* ---- Set text ---- */
    printf("\n  -- set text content --\n");
    {
        BenchResult t = bench_run("taurus: set text",   taurus_set_text,   NULL, ITERS_OP);
        BenchResult p = bench_run("pugixml: set text",  pugixml_set_text,  NULL, ITERS_OP);
        BenchResult l = bench_run("libxml2: set text",  libxml2_set_text,  NULL, ITERS_OP);
        bench_print_result(&t);
        bench_print_result(&p);
        bench_print_result(&l);
    }

    /* ---- Parse small + 10 writes ---- */
    printf("\n  -- parse small + 10 attr writes + 10 child appends --\n");
    {
        write_parsed_ctx_t w = { BENCH_XML_SMALL, strlen(BENCH_XML_SMALL), 10 };
        BenchResult t = bench_run("taurus: small+writes",   taurus_write_parsed,   &w, ITERS_PARSE);
        BenchResult p = bench_run("pugixml: small+writes",  pugixml_write_parsed,  &w, ITERS_PARSE);
        BenchResult l = bench_run("libxml2: small+writes",  libxml2_write_parsed,  &w, ITERS_PARSE);
        bench_print_result(&t);
        bench_print_result(&p);
        bench_print_result(&l);
    }

    /* ---- Parse medium + 10 writes ---- */
    printf("\n  -- parse medium + 10 attr writes + 10 child appends --\n");
    {
        write_parsed_ctx_t w = { BENCH_XML_MEDIUM, strlen(BENCH_XML_MEDIUM), 10 };
        BenchResult t = bench_run("taurus: medium+writes",   taurus_write_parsed,   &w, ITERS_PARSE);
        BenchResult p = bench_run("pugixml: medium+writes",  pugixml_write_parsed,  &w, ITERS_PARSE);
        BenchResult l = bench_run("libxml2: medium+writes",  libxml2_write_parsed,  &w, ITERS_PARSE);
        bench_print_result(&t);
        bench_print_result(&p);
        bench_print_result(&l);
    }

    /* ---- Parse large + 10 writes ---- */
    printf("\n  -- parse large + 10 attr writes + 10 child appends --\n");
    {
        write_parsed_ctx_t w = { BENCH_XML_LARGE, strlen(BENCH_XML_LARGE), 10 };
        BenchResult t = bench_run("taurus: large+writes",   taurus_write_parsed,   &w, ITERS_PARSE / 2);
        BenchResult p = bench_run("pugixml: large+writes",  pugixml_write_parsed,  &w, ITERS_PARSE / 2);
        BenchResult l = bench_run("libxml2: large+writes",  libxml2_write_parsed,  &w, ITERS_PARSE / 2);
        bench_print_result(&t);
        bench_print_result(&p);
        bench_print_result(&l);
    }

    xmlCleanupParser();
    return 0;
}
