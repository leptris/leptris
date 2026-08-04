#include "../common/benchmark.h"
#include "../common/test_data.h"
#include <taurus.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Context for parse + root benchmark */
typedef struct {
    const char* xml;
    size_t len;
} parse_ctx_t;

/* Benchmark 1: Parse + Root */
void bench_parse_root(void* ctx) {
    parse_ctx_t* pctx = (parse_ctx_t*)ctx;

    /* Make writable copy for in-place parsing */
    char* xml_copy = (char*)malloc(pctx->len + 1);
    memcpy(xml_copy, pctx->xml, pctx->len);
    xml_copy[pctx->len] = '\0';

    /* Use in-place parser (zero-copy + pool + deduplication) */
    TaurusDocument doc = taurus_parse_string_inplace(xml_copy, pctx->len, NULL);
    TaurusElement root = taurus_document_root(doc);
    (void)root;  /* Prevent optimization */
    taurus_document_free(doc);

    /* Free writable copy */
    free(xml_copy);
}

/* Context for traversal/access benchmarks */
typedef struct {
    TaurusDocument doc;
} doc_ctx_t;

/* Recursive tree traversal helper */
static void traverse_tree(TaurusElement elem) {
    if (!elem) return;

    /* Access element name (simulates real usage) */
    const char* name = taurus_element_name(elem);
    (void)name;

    /* Traverse children */
    size_t child_count = taurus_element_child_count(elem);
    for (size_t i = 0; i < child_count; i++) {
        TaurusElement child = taurus_element_child(elem, i);
        traverse_tree(child);
    }
}

/* Benchmark 2: Tree Traversal */
void bench_traverse(void* ctx) {
    doc_ctx_t* dctx = (doc_ctx_t*)ctx;
    TaurusElement root = taurus_document_root(dctx->doc);
    traverse_tree(root);
}

/* Benchmark 3: Attribute Access (repeated 100x) */
void bench_attributes(void* ctx) {
    doc_ctx_t* dctx = (doc_ctx_t*)ctx;
    TaurusElement root = taurus_document_root(dctx->doc);

    /* Find first book/product element */
    TaurusElement target = root;
    if (taurus_element_child_count(root) > 0) {
        target = taurus_element_child(root, 0);
        if (taurus_element_child_count(target) > 0) {
            target = taurus_element_child(target, 0);
        }
    }

    /* Access attributes 100 times */
    for (int i = 0; i < 100; i++) {
        const char* attr = taurus_element_attribute(target, "id");
        (void)attr;
    }
}

/* Benchmark 4: Text Extraction (repeated 100x) */
void bench_text(void* ctx) {
    doc_ctx_t* dctx = (doc_ctx_t*)ctx;
    TaurusElement root = taurus_document_root(dctx->doc);

    /* Find first leaf element */
    TaurusElement target = root;
    while (taurus_element_child_count(target) > 0) {
        target = taurus_element_child(target, 0);
    }

    /* Extract text 100 times */
    for (int i = 0; i < 100; i++) {
        const char* text = taurus_element_text(target);
        (void)text;
    }
}

/* Benchmark 5: Child Iteration (repeated 100x) */
void bench_children(void* ctx) {
    doc_ctx_t* dctx = (doc_ctx_t*)ctx;
    TaurusElement root = taurus_document_root(dctx->doc);

    /* Iterate children 100 times */
    for (int i = 0; i < 100; i++) {
        size_t count = taurus_element_child_count(root);
        for (size_t j = 0; j < count; j++) {
            TaurusElement child = taurus_element_child(root, j);
            (void)child;
        }
    }
}

int main(void) {
    const size_t ITERATIONS = 1000;

    printf("\n");
    printf("================================================================\n");
    printf("Taurus DOM Benchmarks\n");
    printf("================================================================\n");
    printf("Test Data: Medium XML (~10KB)\n");
    printf("Iterations: %zu per benchmark\n", ITERATIONS);
    printf("================================================================\n");

    /* Benchmark 1: Parse + Root (creates and destroys doc each time) */
    parse_ctx_t parse_ctx = { BENCH_XML_MEDIUM, strlen(BENCH_XML_MEDIUM) };
    BenchResult r1 = bench_run("Parse + Root", bench_parse_root, &parse_ctx, ITERATIONS);
    bench_print_result(&r1);

    /* Create document once for remaining benchmarks */
    TaurusDocument doc = taurus_parse_string(BENCH_XML_MEDIUM, strlen(BENCH_XML_MEDIUM), NULL);
    if (!doc) {
        fprintf(stderr, "Failed to parse test XML\n");
        return 1;
    }
    doc_ctx_t doc_ctx = { doc };

    /* Benchmark 2: Tree Traversal */
    BenchResult r2 = bench_run("Tree Traversal", bench_traverse, &doc_ctx, ITERATIONS);
    bench_print_result(&r2);

    /* Benchmark 3: Attribute Access */
    BenchResult r3 = bench_run("Attribute Access (100x)", bench_attributes, &doc_ctx, ITERATIONS);
    bench_print_result(&r3);

    /* Benchmark 4: Text Extraction */
    BenchResult r4 = bench_run("Text Extraction (100x)", bench_text, &doc_ctx, ITERATIONS);
    bench_print_result(&r4);

    /* Benchmark 5: Child Iteration */
    BenchResult r5 = bench_run("Child Iteration (100x)", bench_children, &doc_ctx, ITERATIONS);
    bench_print_result(&r5);

    taurus_document_free(doc);

    /* Print summary */
    BenchResult results[] = { r1, r2, r3, r4, r5 };
    bench_print_summary("Taurus DOM", results, 5);

    printf("\n");
    return 0;
}