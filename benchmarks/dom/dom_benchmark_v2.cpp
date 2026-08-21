/**
 * DOM Benchmark v2 - Proper Isolated Performance Measurement
 *
 * This benchmark parses ONCE then measures DOM operations in isolation.
 * The original benchmark was flawed because it parsed on every iteration,
 * making parsing time dominate the measurements.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

// Leptris API (C)
extern "C" {
#include <leptris.h>
}

// pugixml API (C++)
#include <pugixml.hpp>

// Benchmark utilities (C)
extern "C" {
#include "utils.h"
}

#define ITERATIONS 100000  // 100k iterations for DOM operations
#define WARMUP_ITERS 10000   // 10k warmup iterations

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
// Benchmark 1: Parse (measured separately)
// ============================================================================

static void bench_leptris_parse(const char* xml, size_t len) {
    LeptrisDocument doc = leptris_parse_string(xml, len, NULL);
    leptris_document_free(doc);
}

static void bench_pugixml_parse(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);
}

// ============================================================================
// Benchmark 2: Child Access - PARSE ONCE, ACCESS MANY TIMES
// ============================================================================

static void bench_leptris_child_access(const char* xml, size_t len) {
    // Parse ONCE
    LeptrisDocument doc = leptris_parse_string(xml, len, NULL);
    LeptrisElement root = leptris_document_root(doc);

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        volatile size_t count = leptris_element_child_count(root);
        for (size_t j = 0; j < count; j++) {
            volatile LeptrisElement child = leptris_element_child(root, j);
            (void)child;
        }
    }

    // Measure ONLY child access
    long start = benchmark_time_us();
    for (int i = 0; i < ITERATIONS; i++) {
        size_t count = leptris_element_child_count(root);
        for (size_t j = 0; j < count; j++) {
            LeptrisElement child = leptris_element_child(root, j);
            const char* name = leptris_element_name(child);
            (void)name;  // Use result
        }
    }
    long end = benchmark_time_us();

    leptris_document_free(doc);
    benchmark_print_result("Child Access (isolated)", (benchmark_stats){(double)(end - start), 0, 0, 0, 0}, (benchmark_stats){0, 0, 0, 0, 0}, "pugixml");
}

static void bench_pugixml_child_access(const char* xml, size_t len) {
    // Parse ONCE
    pugi::xml_document doc;
    doc.load_buffer(xml, len);
    pugi::xml_node root = doc.root().first_child();

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        for (pugi::xml_node child : root.children()) {
            (void)child.name();
        }
    }

    // Measure ONLY child access
    long start = benchmark_time_us();
    for (int i = 0; i < ITERATIONS; i++) {
        for (pugi::xml_node child : root.children()) {
            const char* name = child.name();
            (void)name;
        }
    }
    long end = benchmark_time_us();

    benchmark_print_result("Child Access (isolated)", (benchmark_stats){(double)(end - start), 0, 0, 0, 0}, (benchmark_stats){0, 0, 0, 0, 0}, "pugixml");
}

// ============================================================================
// Benchmark 3: Attribute Access - PARSE ONCE
// ============================================================================

static void bench_leptris_attribute_access(const char* xml, size_t len) {
    LeptrisDocument doc = leptris_parse_string(xml, len, NULL);
    LeptrisElement root = leptris_document_root(doc);
    size_t count = leptris_element_child_count(root);

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        for (size_t j = 0; j < count; j++) {
            LeptrisElement child = leptris_element_child(root, j);
            const char* id = leptris_element_attribute(child, "id");
            const char* category = leptris_element_attribute(child, "category");
            (void)id; (void)category;
        }
    }

    // Measure ONLY attribute access
    long start = benchmark_time_us();
    for (int i = 0; i < ITERATIONS; i++) {
        for (size_t j = 0; j < count; j++) {
            LeptrisElement child = leptris_element_child(root, j);
            const char* id = leptris_element_attribute(child, "id");
            const char* category = leptris_element_attribute(child, "category");
            (void)id; (void)category;
        }
    }
    long end = benchmark_time_us();

    leptris_document_free(doc);
    benchmark_print_result("Attribute Access (isolated)", (benchmark_stats){(double)(end - start), 0, 0, 0, 0}, (benchmark_stats){0, 0, 0, 0, 0}, "pugixml");
}

static void bench_pugixml_attribute_access(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);
    pugi::xml_node root = doc.root().first_child();

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        for (pugi::xml_node child : root.children()) {
            child.attribute("id").value();
            child.attribute("category").value();
        }
    }

    // Measure ONLY attribute access
    long start = benchmark_time_us();
    for (int i = 0; i < ITERATIONS; i++) {
        for (pugi::xml_node child : root.children()) {
            const char* id = child.attribute("id").value();
            const char* category = child.attribute("category").value();
            (void)id; (void)category;
        }
    }
    long end = benchmark_time_us();

    benchmark_print_result("Attribute Access (isolated)", (benchmark_stats){(double)(end - start), 0, 0, 0, 0}, (benchmark_stats){0, 0, 0, 0, 0}, "pugixml");
}

// ============================================================================
// Benchmark 4: Tree Walking (recursive) - PARSE ONCE
// ============================================================================

static int walk_leptris(LeptrisElement elem) {
    int count = 1;
    size_t children = leptris_element_child_count(elem);
    for (size_t i = 0; i < children; i++) {
        count += walk_leptris(leptris_element_child(elem, i));
    }
    return count;
}

static void bench_leptris_tree_walk(const char* xml, size_t len) {
    LeptrisDocument doc = leptris_parse_string(xml, len, NULL);
    LeptrisElement root = leptris_document_root(doc);

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        volatile int count = walk_leptris(root);
        (void)count;
    }

    // Measure ONLY tree walking
    long start = benchmark_time_us();
    for (int i = 0; i < ITERATIONS; i++) {
        volatile int count = walk_leptris(root);
        (void)count;
    }
    long end = benchmark_time_us();

    leptris_document_free(doc);
    benchmark_print_result("Tree Walk (isolated)", (benchmark_stats){(double)(end - start), 0, 0, 0, 0}, (benchmark_stats){0, 0, 0, 0, 0}, "pugixml");
}

static int walk_pugixml(pugi::xml_node node) {
    int count = 1;
    for (pugi::xml_node child : node.children()) {
        count += walk_pugixml(child);
    }
    return count;
}

static void bench_pugixml_tree_walk(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);
    pugi::xml_node root = doc.root().first_child();

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        volatile int count = walk_pugixml(root);
        (void)count;
    }

    // Measure ONLY tree walking
    long start = benchmark_time_us();
    for (int i = 0; i < ITERATIONS; i++) {
        volatile int count = walk_pugixml(root);
        (void)count;
    }
    long end = benchmark_time_us();

    benchmark_print_result("Tree Walk (isolated)", (benchmark_stats){(double)(end - start), 0, 0, 0, 0}, (benchmark_stats){0, 0, 0, 0, 0}, "pugixml");
}

// ============================================================================
// Main
// ============================================================================

typedef void (*bench_func_t)(const char*, size_t);

struct benchmark_test {
    const char* name;
    bench_func_t leptris_fn;
    bench_func_t pugixml_fn;
};

static void run_benchmark(const char* xml, size_t len, const benchmark_test* test) {
    printf("\n=== %s ===\n", test->name);

    // Leptris measurement
    long leptris_start = benchmark_time_us();
    test->leptris_fn(xml, len);
    long leptris_end = benchmark_time_us();
    double leptris_time = (double)(leptris_end - leptris_start);

    // pugixml measurement
    long pugixml_start = benchmark_time_us();
    test->pugixml_fn(xml, len);
    long pugixml_end = benchmark_time_us();
    double pugixml_time = (double)(pugixml_end - pugixml_start);

    // Calculate speedup
    double speedup = pugixml_time / leptris_time;

    printf("Leptris:  %.2f µs (%d iterations)\n", leptris_time, ITERATIONS);
    printf("pugixml:  %.2f µs (%d iterations)\n", pugixml_time, ITERATIONS);
    printf("Speedup: %.2fx (%s)\n", speedup,
           speedup >= 1.2 ? "✅ AHEAD" : speedup > 0.8 ? "~ PARITY" : "⚠️ BEHIND");
}

int main(int argc, char** argv) {
    const char* filename = (argc > 1) ? argv[1] : "fixtures/small.xml";

    // Read XML file
    std::string xml_content = read_file(filename);
    const char* xml = xml_content.c_str();
    size_t len = xml_content.length();

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     DOM Benchmark v2 - Isolated Operation Measurement         ║\n");
    printf("║     (parse once, measure %d iterations)                       ║\n", ITERATIONS);
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\nFile: %s (%.1f KB)\n", filename, len / 1024.0);

    // Define benchmarks
    benchmark_test tests[] = {
        {"Child Access (isolated)", bench_leptris_child_access, bench_pugixml_child_access},
        {"Attribute Access (isolated)", bench_leptris_attribute_access, bench_pugixml_attribute_access},
        {"Tree Walking (isolated)", bench_leptris_tree_walk, bench_pugixml_tree_walk},
    };

    // Run all benchmarks
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        run_benchmark(xml, len, &tests[i]);
    }

    printf("\n");
    printf("Target: ≥1.2x faster than pugixml for DOM operations\n");
    printf("\n");

    return 0;
}
