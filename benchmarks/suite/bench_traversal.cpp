/**
 * DOM Traversal Benchmarks
 *
 * Measures tree walking and navigation performance.
 * Target: >= 1.2x faster than pugixml
 *
 * Tests:
 * 1. First child access
 * 2. Next sibling access
 * 3. Parent access
 * 4. Deep recursive walk
 * 5. Wide sibling iteration
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

// Taurus API (C)
extern "C" {
#include <taurus.h>
}

// pugixml API (C++)
#include <pugixml.hpp>

// Benchmark utilities
extern "C" {
#include "utils.h"
}

/* TaurusElement null check helper for C++ code */
static inline bool elem_not_null(const TaurusElement& elem) {
    return !taurus_element_is_null(&elem);
}

/* Get null TaurusElement */
static inline TaurusElement elem_null(void) {
    return taurus_element_handle_null();
}

// Quick mode for development (set to 0 for full runs)
#define QUICK_MODE 1

#if QUICK_MODE
#define ITERATIONS 1000
#define WARMUP_ITERS 100
#else
#define ITERATIONS 100000
#define WARMUP_ITERS 10000
#endif

// Read file into string
static std::string read_file(const char* filename) {
    std::ifstream file(filename);
    if (!file) {
        fprintf(stderr, "Error: Cannot open file: %s\n", filename);
        exit(1);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// ============================================================================
// Test 1: First Child Access
// ============================================================================

static void bench_taurus_first_child(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement child_result = elem_null();
    for (int i = 0; i < ITERATIONS; i++) {
        child_result = taurus_element_first_child_any(root);
    }
    (void)child_result;  // Prevent optimization

    taurus_document_free(doc);
}

static void bench_pugixml_first_child(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);
    pugi::xml_node root = doc.root().first_child();

    pugi::xml_node child;
    for (int i = 0; i < ITERATIONS; i++) {
        child = root.first_child();
    }
    (void)child;  // Prevent optimization
}

// ============================================================================
// Test 2: Next Sibling Access
// ============================================================================

static void bench_taurus_next_sibling(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);
    TaurusElement root = taurus_document_root(doc);

    volatile int count = 0;
    for (int i = 0; i < ITERATIONS; i++) {
        for (TaurusElement child = taurus_element_first_child_any(root);
             elem_not_null(child); child = taurus_element_next_sibling_any(child)) {
            count++;
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_next_sibling(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);
    pugi::xml_node root = doc.root().first_child();

    volatile int count = 0;
    for (int i = 0; i < ITERATIONS; i++) {
        for (pugi::xml_node child = root.first_child(); child;
             child = child.next_sibling()) {
            count++;
        }
    }
}

// ============================================================================
// Test 3: Parent Access
// ============================================================================

static void bench_taurus_parent(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_first_child_any(root);

    TaurusElement parent_result = elem_null();
    for (int i = 0; i < ITERATIONS; i++) {
        parent_result = taurus_element_parent(child);
    }
    (void)parent_result;  // Prevent optimization

    taurus_document_free(doc);
}

static void bench_pugixml_parent(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);
    pugi::xml_node root = doc.root().first_child();
    pugi::xml_node child = root.first_child();

    pugi::xml_node parent;
    for (int i = 0; i < ITERATIONS; i++) {
        parent = child.parent();
    }
    (void)parent;  // Prevent optimization
}

// ============================================================================
// Test 4: Deep Recursive Walk
// ============================================================================

static int walk_taurus_deep(TaurusElement elem) {
    int count = 1;
    for (TaurusElement child = taurus_element_first_child_any(elem);
         elem_not_null(child); child = taurus_element_next_sibling_any(child)) {
        count += walk_taurus_deep(child);
    }
    return count;
}

static void bench_taurus_deep_walk(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);
    TaurusElement root = taurus_document_root(doc);

    volatile int count = 0;
    for (int i = 0; i < ITERATIONS / 10; i++) {
        count = walk_taurus_deep(root);
    }

    taurus_document_free(doc);
}

static int walk_pugixml_deep(pugi::xml_node node) {
    int count = 1;
    for (pugi::xml_node child : node.children()) {
        count += walk_pugixml_deep(child);
    }
    return count;
}

static void bench_pugixml_deep_walk(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);
    pugi::xml_node root = doc.root().first_child();

    volatile int count = 0;
    for (int i = 0; i < ITERATIONS / 10; i++) {
        count = walk_pugixml_deep(root);
    }
}

// ============================================================================
// Test 5: Wide Sibling Iteration
// ============================================================================

static void bench_taurus_wide_iter(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);
    TaurusElement root = taurus_document_root(doc);

    volatile const char* name = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        for (TaurusElement child = taurus_element_first_child_any(root);
             elem_not_null(child); child = taurus_element_next_sibling_any(child)) {
            name = taurus_element_name(child);
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_wide_iter(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);
    pugi::xml_node root = doc.root().first_child();

    volatile const char* name = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        for (pugi::xml_node child : root.children()) {
            name = child.name();
        }
    }
}

// ============================================================================
// Benchmark Runner
// ============================================================================

typedef void (*bench_func_t)(const char*, size_t);

static void run_traversal_benchmark(const char* name,
                                    bench_func_t taurus_fn,
                                    bench_func_t pugixml_fn,
                                    const char* xml, size_t len) {
    printf("\n=== %s ===\n", name);

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        taurus_fn(xml, len);
        pugixml_fn(xml, len);
    }

    // Measure Taurus
    std::vector<double> taurus_times;
    for (int i = 0; i < 10; i++) {
        long start = benchmark_time_us();
        taurus_fn(xml, len);
        long end = benchmark_time_us();
        taurus_times.push_back((double)(end - start));
    }
    benchmark_stats taurus_stats = benchmark_analyze(taurus_times.data(), 10);

    // Measure pugixml
    std::vector<double> pugixml_times;
    for (int i = 0; i < 10; i++) {
        long start = benchmark_time_us();
        pugixml_fn(xml, len);
        long end = benchmark_time_us();
        pugixml_times.push_back((double)(end - start));
    }
    benchmark_stats pugixml_stats = benchmark_analyze(pugixml_times.data(), 10);

    // Print results
    double speedup = pugixml_stats.median / taurus_stats.median;
    printf("  Taurus:   %8.2f us (median)\n", taurus_stats.median);
    printf("  pugixml:  %8.2f us (median)\n", pugixml_stats.median);
    printf("  Speedup:  %.2fx %s\n", speedup,
           speedup >= 1.2 ? "PASS" : speedup >= 1.0 ? "OK" : "FAIL");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    const char* data_dir = (argc > 1) ? argv[1] : "data";

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║            DOM Traversal Benchmarks (5 tests)              ║\n");
    printf("║  Target: >= 1.2x faster than pugixml                      ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // Load test files (using generated fixture filenames)
    std::string small_xml = read_file((std::string(data_dir) + "/small.xml").c_str());
    std::string medium_xml = read_file((std::string(data_dir) + "/medium.xml").c_str());
    std::string deep_xml = read_file((std::string(data_dir) + "/deep_100.xml").c_str());
    std::string wide_xml = read_file((std::string(data_dir) + "/wide_1000.xml").c_str());

    // Test 1: First Child Access
    run_traversal_benchmark("First Child Access",
                           bench_taurus_first_child, bench_pugixml_first_child,
                           small_xml.c_str(), small_xml.length());

    // Test 2: Next Sibling Access
    run_traversal_benchmark("Next Sibling Access",
                           bench_taurus_next_sibling, bench_pugixml_next_sibling,
                           wide_xml.c_str(), wide_xml.length());

    // Test 3: Parent Access
    run_traversal_benchmark("Parent Access",
                           bench_taurus_parent, bench_pugixml_parent,
                           small_xml.c_str(), small_xml.length());

    // Test 4: Deep Recursive Walk
    run_traversal_benchmark("Deep Recursive Walk",
                           bench_taurus_deep_walk, bench_pugixml_deep_walk,
                           deep_xml.c_str(), deep_xml.length());

    // Test 5: Wide Sibling Iteration
    run_traversal_benchmark("Wide Sibling Iteration",
                           bench_taurus_wide_iter, bench_pugixml_wide_iter,
                           wide_xml.c_str(), wide_xml.length());

    printf("\n");
    printf("Traversal Benchmarks Complete\n");
    printf("\n");

    return 0;
}
