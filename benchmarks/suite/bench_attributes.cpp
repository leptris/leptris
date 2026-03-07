/**
 * Attribute Access Benchmarks
 *
 * Measures attribute get/set performance with varying numbers of attributes.
 * This is CRITICAL for detecting O(n) vs O(1) attribute lookup.
 * Target: >= 1.2x faster than pugixml
 *
 * Tests:
 * 1. Get first attribute (should be O(1) for both)
 * 2. Get last attribute (O(n) vs O(1) test)
 * 3. Get middle attribute
 * 4. Get attribute with 5 attrs/element
 * 5. Get attribute with 20 attrs/element
 * 6. Get attribute with 100 attrs/element
 * 7. Scaling analysis (O(n) vs O(1) detection)
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
// Test 1: Get First Attribute (O(1) baseline)
// ============================================================================

static void bench_taurus_get_first_attr(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);
    TaurusElement root = taurus_document_root(doc);

    volatile const char* value = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        for (TaurusElement child = taurus_element_first_child_any(root);
             elem_not_null(child); child = taurus_element_next_sibling_any(child)) {
            value = taurus_element_attribute(child, "attr0");
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_get_first_attr(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);
    pugi::xml_node root = doc.root().first_child();

    volatile const char* value = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        for (pugi::xml_node child : root.children()) {
            value = child.attribute("attr0").value();
        }
    }
}

// ============================================================================
// Test 2: Get Last Attribute (O(n) vs O(1) test)
// ============================================================================

static void bench_taurus_get_last_attr(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);
    TaurusElement root = taurus_document_root(doc);

    volatile const char* value = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        for (TaurusElement child = taurus_element_first_child_any(root);
             elem_not_null(child); child = taurus_element_next_sibling_any(child)) {
            value = taurus_element_attribute(child, "attr99");
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_get_last_attr(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);
    pugi::xml_node root = doc.root().first_child();

    volatile const char* value = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        for (pugi::xml_node child : root.children()) {
            value = child.attribute("attr99").value();
        }
    }
}

// ============================================================================
// Test 3: Get Middle Attribute
// ============================================================================

static void bench_taurus_get_middle_attr(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);
    TaurusElement root = taurus_document_root(doc);

    volatile const char* value = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        for (TaurusElement child = taurus_element_first_child_any(root);
             elem_not_null(child); child = taurus_element_next_sibling_any(child)) {
            value = taurus_element_attribute(child, "attr50");
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_get_middle_attr(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);
    pugi::xml_node root = doc.root().first_child();

    volatile const char* value = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        for (pugi::xml_node child : root.children()) {
            value = child.attribute("attr50").value();
        }
    }
}

// ============================================================================
// Helper to generate inline XML with N attributes per element
// ============================================================================

static std::string generate_attrs_xml(int attrs_per_element, int num_elements) {
    std::stringstream ss;
    ss << "<?xml version=\"1.0\"?>\n<root>\n";
    for (int i = 0; i < num_elements; i++) {
        ss << "  <element";
        for (int j = 0; j < attrs_per_element; j++) {
            ss << " attr" << j << "=\"value" << j << "_" << i << "\"";
        }
        ss << ">Content " << i << "</element>\n";
    }
    ss << "</root>";
    return ss.str();
}

// ============================================================================
// Scaling Analysis: Varying Attribute Counts
// ============================================================================

static void run_attr_count_benchmark(const char* name, int attrs_per_element) {
    std::string xml = generate_attrs_xml(attrs_per_element, 100);

    printf("\n=== %s (%d attrs/element) ===\n", name, attrs_per_element);

    // Warmup
    for (int i = 0; i < WARMUP_ITERS / 10; i++) {
        TaurusDocument doc = taurus_parse_string(xml.c_str(), xml.length(), NULL);
        taurus_document_free(doc);
        pugi::xml_document pdoc;
        pdoc.load_buffer(xml.c_str(), xml.length());
    }

    // Lookup middle attribute
    char middle_attr[32];
    snprintf(middle_attr, sizeof(middle_attr), "attr%d", attrs_per_element / 2);

    // Taurus measurement
    std::vector<double> taurus_times;
    for (int iter = 0; iter < 10; iter++) {
        TaurusDocument doc = taurus_parse_string(xml.c_str(), xml.length(), NULL);
        TaurusElement root = taurus_document_root(doc);

        long start = benchmark_time_us();
        volatile const char* value = NULL;
        for (int i = 0; i < ITERATIONS; i++) {
            for (TaurusElement child = taurus_element_first_child_any(root);
                 elem_not_null(child); child = taurus_element_next_sibling_any(child)) {
                value = taurus_element_attribute(child, middle_attr);
            }
        }
        long end = benchmark_time_us();
        taurus_times.push_back((double)(end - start));

        taurus_document_free(doc);
    }
    benchmark_stats taurus_stats = benchmark_analyze(taurus_times.data(), 10);

    // pugixml measurement
    std::vector<double> pugixml_times;
    for (int iter = 0; iter < 10; iter++) {
        pugi::xml_document doc;
        doc.load_buffer(xml.c_str(), xml.length());
        pugi::xml_node root = doc.root().first_child();

        long start = benchmark_time_us();
        volatile const char* value = NULL;
        for (int i = 0; i < ITERATIONS; i++) {
            for (pugi::xml_node child : root.children()) {
                value = child.attribute(middle_attr).value();
            }
        }
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

    // Analysis: O(n) vs O(1)
    printf("  Analysis: ");
    if (attrs_per_element <= 4) {
        printf("Inline array should be O(1)\n");
    } else {
        printf("Hash table needed for O(1) - check scaling across tests\n");
    }
}

// ============================================================================
// Benchmark Runner
// ============================================================================

typedef void (*bench_func_t)(const char*, size_t);

static void run_attr_benchmark(const char* name,
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
    // Default data directory is fixtures/data relative to this executable
    const char* data_dir = (argc > 1) ? argv[1] : "../fixtures/data";

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║          Attribute Access Benchmarks (12 tests)           ║\n");
    printf("║  Target: >= 1.2x faster than pugixml                      ║\n");
    printf("║  CRITICAL: Detects O(n) vs O(1) attribute lookup          ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // Try to find the attrs_100.xml file
    std::string attrs_path = std::string(data_dir) + "/attrs_100.xml";
    FILE* test_file = fopen(attrs_path.c_str(), "r");
    if (!test_file) {
        // Try alternative paths
        attrs_path = "data/attrs_100.xml";
        test_file = fopen(attrs_path.c_str(), "r");
    }
    if (!test_file) {
        // Generate test data inline if file not found
        printf("\nNote: Using generated test data (fixture file not found)\n");
    } else {
        fclose(test_file);
    }

    // Tests 1-6: Varying attribute counts (scaling analysis)
    // These tests use generated XML to ensure consistent measurements
    run_attr_count_benchmark("Attribute Lookup (1 attr)", 1);
    run_attr_count_benchmark("Attribute Lookup (4 attrs - inline)", 4);
    run_attr_count_benchmark("Attribute Lookup (10 attrs)", 10);
    run_attr_count_benchmark("Attribute Lookup (20 attrs)", 20);
    run_attr_count_benchmark("Attribute Lookup (50 attrs)", 50);
    run_attr_count_benchmark("Attribute Lookup (100 attrs)", 100);

    // Additional tests if fixture file exists
    if (test_file || fopen(attrs_path.c_str(), "r")) {
        std::string attrs_xml = read_file(attrs_path.c_str());

        printf("\n--- Using fixture file: %s ---\n", attrs_path.c_str());

        // Test: Get First Attribute from fixture
        run_attr_benchmark("Get First Attribute (O(1) baseline)",
                           bench_taurus_get_first_attr, bench_pugixml_get_first_attr,
                           attrs_xml.c_str(), attrs_xml.length());

        // Test: Get Last Attribute (CRITICAL for O(n) detection)
        run_attr_benchmark("Get Last Attribute (O(n) vs O(1) test)",
                           bench_taurus_get_last_attr, bench_pugixml_get_last_attr,
                           attrs_xml.c_str(), attrs_xml.length());

        // Test: Get Middle Attribute
        run_attr_benchmark("Get Middle Attribute",
                           bench_taurus_get_middle_attr, bench_pugixml_get_middle_attr,
                           attrs_xml.c_str(), attrs_xml.length());
    }

    printf("\n");
    printf("Attribute Access Benchmarks Complete\n");
    printf("\n");
    printf("SCALING ANALYSIS:\n");
    printf("If times grow linearly with attribute count, O(n) lookup is being used.\n");
    printf("Expected: Constant time across all attribute counts for O(1) hash table.\n");
    printf("  - 1-4 attributes: Should use inline array (fastest)\n");
    printf("  - 5+ attributes: Should use hash table (O(1))\n");
    printf("\n");

    return 0;
}
