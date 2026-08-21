#include "../common/benchmark.h"
#include "../common/test_data.h"
#include <leptris.h>
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
    LeptrisDocument doc = leptris_parse_string_inplace(xml_copy, pctx->len, NULL);
    LeptrisElement root = leptris_document_root(doc);
    (void)root;  /* Prevent optimization */
    leptris_document_free(doc);

    /* Free writable copy */
    free(xml_copy);
}

/* Context for traversal/access benchmarks */
typedef struct {
    LeptrisDocument doc;
} doc_ctx_t;

/* Recursive tree traversal helper */
static void traverse_tree(LeptrisElement elem) {
    if (!elem) return;

    /* Access element name (simulates real usage) */
    const char* name = leptris_element_name(elem);
    (void)name;

    /* Traverse children */
    size_t child_count = leptris_element_child_count(elem);
    for (size_t i = 0; i < child_count; i++) {
        LeptrisElement child = leptris_element_child(elem, i);
        traverse_tree(child);
    }
}

/* Benchmark 2: Tree Traversal */
void bench_traverse(void* ctx) {
    doc_ctx_t* dctx = (doc_ctx_t*)ctx;
    LeptrisElement root = leptris_document_root(dctx->doc);
    traverse_tree(root);
}

/* Benchmark 3: Attribute Access (repeated 100x) */
void bench_attributes(void* ctx) {
    doc_ctx_t* dctx = (doc_ctx_t*)ctx;
    LeptrisElement root = leptris_document_root(dctx->doc);

    /* Find first book/product element */
    LeptrisElement target = root;
    if (leptris_element_child_count(root) > 0) {
        target = leptris_element_child(root, 0);
        if (leptris_element_child_count(target) > 0) {
            target = leptris_element_child(target, 0);
        }
    }

    /* Access attributes 100 times */
    for (int i = 0; i < 100; i++) {
        const char* attr = leptris_element_attribute(target, "id");
        (void)attr;
    }
}

/* Benchmark 4: Text Extraction (repeated 100x) */
void bench_text(void* ctx) {
    doc_ctx_t* dctx = (doc_ctx_t*)ctx;
    LeptrisElement root = leptris_document_root(dctx->doc);

    /* Find first leaf element */
    LeptrisElement target = root;
    while (leptris_element_child_count(target) > 0) {
        target = leptris_element_child(target, 0);
    }

    /* Extract text 100 times */
    for (int i = 0; i < 100; i++) {
        const char* text = leptris_element_text(target);
        (void)text;
    }
}

/* Benchmark 5: Child Iteration (repeated 100x) */
void bench_children(void* ctx) {
    doc_ctx_t* dctx = (doc_ctx_t*)ctx;
    LeptrisElement root = leptris_document_root(dctx->doc);

    /* Iterate children 100 times */
    for (int i = 0; i < 100; i++) {
        size_t count = leptris_element_child_count(root);
        for (size_t j = 0; j < count; j++) {
            LeptrisElement child = leptris_element_child(root, j);
            (void)child;
        }
    }
}

/* Benchmark 5b: Child Iteration via O(1) iterator pattern.
 *
 * leptris_element_child(elem, j) is O(j) — see leptris.h.
 * Sequential iteration via first_child + next_sibling is O(1) per
 * step.  This benchmark shows the difference.  See TODO 105. */
void bench_children_iterator(void* ctx) {
    doc_ctx_t* dctx = (doc_ctx_t*)ctx;
    LeptrisElement root = leptris_document_root(dctx->doc);

    for (int i = 0; i < 100; i++) {
        LeptrisElement child = leptris_element_first_child_any(root);
        while (child) {
            child = leptris_element_next_sibling_any(child);
        }
    }
}

int main(void) {
    const size_t ITERATIONS = 1000;

    printf("\n");
    printf("================================================================\n");
    printf("Leptris DOM Benchmarks\n");
    printf("================================================================\n");
    printf("Test Data: Medium XML (~10KB)\n");
    printf("Iterations: %zu per benchmark\n", ITERATIONS);
    printf("================================================================\n");

    /* Benchmark 1: Parse + Root (creates and destroys doc each time) */
    parse_ctx_t parse_ctx = { BENCH_XML_MEDIUM, strlen(BENCH_XML_MEDIUM) };
    BenchResult r1 = bench_run("Parse + Root", bench_parse_root, &parse_ctx, ITERATIONS);
    bench_print_result(&r1);

    /* Create document once for remaining benchmarks */
    LeptrisDocument doc = leptris_parse_string(BENCH_XML_MEDIUM, strlen(BENCH_XML_MEDIUM), NULL);
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
    BenchResult r5 = bench_run("Child Iteration (indexed, O(N²))", bench_children, &doc_ctx, ITERATIONS);
    bench_print_result(&r5);

    /* Benchmark 5b: Child Iteration via O(1) iterator pattern */
    BenchResult r5b = bench_run("Child Iteration (iterator, O(N))", bench_children_iterator, &doc_ctx, ITERATIONS);
    bench_print_result(&r5b);

    leptris_document_free(doc);

    /* Print summary */
    BenchResult results[] = { r1, r2, r3, r4, r5, r5b };
    bench_print_summary("Leptris DOM", results, 6);

    printf("\n");
    return 0;
}