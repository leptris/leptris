#include "../common/benchmark.h"
#include "../common/test_data.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdio.h>
#include <string.h>

/* Context for parse + root benchmark */
typedef struct {
    const char* xml;
    size_t len;
} parse_ctx_t;

/* Context for traversal/access benchmarks */
typedef struct {
    xmlDocPtr doc;
} doc_ctx_t;

/* Benchmark 1: Parse + Root */
void bench_parse_root(void* ctx) {
    parse_ctx_t* pctx = (parse_ctx_t*)ctx;
    xmlDocPtr doc = xmlReadMemory(pctx->xml, (int)pctx->len, NULL, NULL, 0);
    xmlNodePtr root = xmlDocGetRootElement(doc);
    (void)root;  /* Prevent optimization */
    xmlFreeDoc(doc);
}

/* Recursive tree traversal helper */
static void traverse_tree(xmlNodePtr node) {
    if (!node) return;

    /* Access node name */
    const char* name = (const char*)node->name;
    (void)name;

    /* Traverse children */
    for (xmlNodePtr child = node->children; child; child = child->next) {
        if (child->type == XML_ELEMENT_NODE) {
            traverse_tree(child);
        }
    }
}

/* Benchmark 2: Tree Traversal */
void bench_traverse(void* ctx) {
    doc_ctx_t* dctx = (doc_ctx_t*)ctx;
    xmlNodePtr root = xmlDocGetRootElement(dctx->doc);
    traverse_tree(root);
}

/* Benchmark 3: Attribute Access (repeated 100x) */
void bench_attributes(void* ctx) {
    doc_ctx_t* dctx = (doc_ctx_t*)ctx;
    xmlNodePtr root = xmlDocGetRootElement(dctx->doc);

    /* Find first child */
    xmlNodePtr target = root->children;
    while (target && target->type != XML_ELEMENT_NODE) {
        target = target->next;
    }

    if (target && target->children) {
        xmlNodePtr sub = target->children;
        while (sub && sub->type != XML_ELEMENT_NODE) {
            sub = sub->next;
        }
        if (sub) {
            target = sub;
        }
    }

    /* Access attributes 100 times */
    for (int i = 0; i < 100; i++) {
        xmlChar* attr = xmlGetProp(target, (const xmlChar*)"id");
        if (attr) {
            xmlFree(attr);
        }
    }
}

/* Benchmark 4: Text Extraction (repeated 100x) */
void bench_text(void* ctx) {
    doc_ctx_t* dctx = (doc_ctx_t*)ctx;
    xmlNodePtr root = xmlDocGetRootElement(dctx->doc);

    /* Find first leaf element */
    xmlNodePtr target = root;
    while (target->children) {
        xmlNodePtr child = target->children;
        while (child && child->type != XML_ELEMENT_NODE) {
            child = child->next;
        }
        if (!child) break;
        target = child;
    }

    /* Extract text 100 times */
    for (int i = 0; i < 100; i++) {
        xmlChar* text = xmlNodeGetContent(target);
        if (text) {
            xmlFree(text);
        }
    }
}

/* Benchmark 5: Child Iteration (repeated 100x) */
void bench_children(void* ctx) {
    doc_ctx_t* dctx = (doc_ctx_t*)ctx;
    xmlNodePtr root = xmlDocGetRootElement(dctx->doc);

    /* Iterate children 100 times */
    for (int i = 0; i < 100; i++) {
        for (xmlNodePtr child = root->children; child; child = child->next) {
            if (child->type == XML_ELEMENT_NODE) {
                (void)child;
            }
        }
    }
}

int main(void) {
    const size_t ITERATIONS = 1000;

    /* Initialize libxml2 */
    LIBXML_TEST_VERSION

    printf("\n");
    printf("================================================================\n");
    printf("libxml2 DOM Benchmarks\n");
    printf("================================================================\n");
    printf("Test Data: Medium XML (~10KB)\n");
    printf("Iterations: %zu per benchmark\n", ITERATIONS);
    printf("================================================================\n");

    /* Benchmark 1: Parse + Root */
    parse_ctx_t parse_ctx = { BENCH_XML_MEDIUM, strlen(BENCH_XML_MEDIUM) };
    BenchResult r1 = bench_run("Parse + Root", bench_parse_root, &parse_ctx, ITERATIONS);
    bench_print_result(&r1);

    /* Create document once for remaining benchmarks */
    xmlDocPtr doc = xmlReadMemory(BENCH_XML_MEDIUM, (int)strlen(BENCH_XML_MEDIUM),
                                   NULL, NULL, 0);
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

    xmlFreeDoc(doc);

    /* Print summary */
    BenchResult results[] = { r1, r2, r3, r4, r5 };
    bench_print_summary("libxml2 DOM", results, 5);

    /* Cleanup libxml2 */
    xmlCleanupParser();

    printf("\n");
    return 0;
}