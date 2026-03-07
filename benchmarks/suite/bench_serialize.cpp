/**
 * XML Serialization Benchmarks
 *
 * Measures XML serialization performance.
 * Target: >= 1.0x vs pugixml (parity or better)
 *
 * Tests:
 * 1. Serialize small document
 * 2. Serialize medium document
 * 3. Serialize large document
 * 4. Pretty print with indentation
 * 5. Minimal (no whitespace)
 * 6. Single element only
 * 7. Preserve CDATA sections
 * 8. Encode entities
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

// Quick mode for development (set to 0 for full runs)
#define QUICK_MODE 1

#if QUICK_MODE
#define ITERATIONS 100
#define WARMUP_ITERS 10
#else
#define ITERATIONS 1000
#define WARMUP_ITERS 100
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
// Test 1: Serialize Small Document
// ============================================================================

static void bench_taurus_serialize_small(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);

    char* output = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        output = taurus_document_serialize(doc, NULL);
        if (output) {
            taurus_free_string(output);
            output = NULL;
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_serialize_small(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);

    std::stringstream ss;
    for (int i = 0; i < ITERATIONS; i++) {
        ss.str("");
        doc.save(ss, "", pugi::format_raw);
    }
}

// ============================================================================
// Test 2: Serialize Medium Document
// ============================================================================

static void bench_taurus_serialize_medium(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);

    char* output = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        output = taurus_document_serialize(doc, NULL);
        if (output) {
            taurus_free_string(output);
            output = NULL;
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_serialize_medium(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);

    std::stringstream ss;
    for (int i = 0; i < ITERATIONS; i++) {
        ss.str("");
        doc.save(ss, "", pugi::format_raw);
    }
}

// ============================================================================
// Test 3: Serialize Large Document
// ============================================================================

static void bench_taurus_serialize_large(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);

    char* output = NULL;
    for (int i = 0; i < ITERATIONS / 10; i++) {
        output = taurus_document_serialize(doc, NULL);
        if (output) {
            taurus_free_string(output);
            output = NULL;
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_serialize_large(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);

    std::stringstream ss;
    for (int i = 0; i < ITERATIONS / 10; i++) {
        ss.str("");
        doc.save(ss, "", pugi::format_raw);
    }
}

// ============================================================================
// Test 4: Pretty Print with Indentation
// ============================================================================

static void bench_taurus_pretty_print(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);
    TaurusSerializeOptions opts = { .indent = 2, .xml_declaration = 1, .encoding = "UTF-8" };

    char* output = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        output = taurus_document_serialize(doc, &opts);
        if (output) {
            taurus_free_string(output);
            output = NULL;
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_pretty_print(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);

    std::stringstream ss;
    for (int i = 0; i < ITERATIONS; i++) {
        ss.str("");
        doc.save(ss, "  ");  // 2-space indent
    }
}

// ============================================================================
// Test 5: Minimal (No Whitespace)
// ============================================================================

static void bench_taurus_minimal(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);

    char* output = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        output = taurus_document_serialize(doc, NULL);  // NULL = compact
        if (output) {
            taurus_free_string(output);
            output = NULL;
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_minimal(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);

    std::stringstream ss;
    for (int i = 0; i < ITERATIONS; i++) {
        ss.str("");
        doc.save(ss, "", pugi::format_raw);  // No indentation
    }
}

// ============================================================================
// Test 6: Single Element Only
// ============================================================================

static void bench_taurus_element_only(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);
    TaurusElement root = taurus_document_root(doc);

    char* output = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        output = taurus_element_serialize(root, NULL);
        if (output) {
            taurus_free_string(output);
            output = NULL;
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_element_only(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);
    pugi::xml_node root = doc.root().first_child();

    std::stringstream ss;
    for (int i = 0; i < ITERATIONS; i++) {
        ss.str("");
        root.print(ss, "", pugi::format_raw);
    }
}

// ============================================================================
// Test 7: Preserve CDATA Sections
// ============================================================================

static void bench_taurus_with_cdata(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);

    char* output = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        output = taurus_document_serialize(doc, NULL);
        if (output) {
            taurus_free_string(output);
            output = NULL;
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_with_cdata(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);

    std::stringstream ss;
    for (int i = 0; i < ITERATIONS; i++) {
        ss.str("");
        doc.save(ss, "", pugi::format_raw | pugi::format_no_empty_element_tags);
    }
}

// ============================================================================
// Test 8: Encode Entities
// ============================================================================

static void bench_taurus_with_entities(const char* xml, size_t len) {
    TaurusDocument doc = taurus_parse_string(xml, len, NULL);

    char* output = NULL;
    for (int i = 0; i < ITERATIONS; i++) {
        output = taurus_document_serialize(doc, NULL);
        if (output) {
            taurus_free_string(output);
            output = NULL;
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_with_entities(const char* xml, size_t len) {
    pugi::xml_document doc;
    doc.load_buffer(xml, len);

    std::stringstream ss;
    for (int i = 0; i < ITERATIONS; i++) {
        ss.str("");
        doc.save(ss, "", pugi::format_raw);
    }
}

// ============================================================================
// Benchmark Runner
// ============================================================================

typedef void (*bench_func_t)(const char*, size_t);

static void run_serialization_benchmark(const char* name,
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
           speedup >= 1.0 ? "PASS" : "FAIL");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    const char* data_dir = (argc > 1) ? argv[1] : "../fixtures/data";

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║          XML Serialization Benchmarks (8 tests)            ║\n");
    printf("║  Target: >= 1.0x vs pugixml (parity)                      ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // Load test files
    std::string small_xml = read_file((std::string(data_dir) + "/small.xml").c_str());
    std::string medium_xml = read_file((std::string(data_dir) + "/medium.xml").c_str());
    std::string cdata_xml = read_file((std::string(data_dir) + "/cdata.xml").c_str());
    std::string entities_xml = read_file((std::string(data_dir) + "/entities.xml").c_str());

    // Test 1: Serialize Small
    run_serialization_benchmark("Serialize Small",
                               bench_taurus_serialize_small, bench_pugixml_serialize_small,
                               small_xml.c_str(), small_xml.length());

    // Test 2: Serialize Medium
    run_serialization_benchmark("Serialize Medium",
                               bench_taurus_serialize_medium, bench_pugixml_serialize_medium,
                               medium_xml.c_str(), medium_xml.length());

    // Test 3: Serialize Large (reduced iterations)
    std::string large_path = std::string(data_dir) + "/large.xml";
    std::ifstream large_file(large_path);
    if (large_file.good()) {
        std::string large_xml = read_file(large_path.c_str());
        run_serialization_benchmark("Serialize Large",
                                   bench_taurus_serialize_large, bench_pugixml_serialize_large,
                                   large_xml.c_str(), large_xml.length());
    }

    // Test 4: Pretty Print
    run_serialization_benchmark("Pretty Print (indent)",
                               bench_taurus_pretty_print, bench_pugixml_pretty_print,
                               medium_xml.c_str(), medium_xml.length());

    // Test 5: Minimal
    run_serialization_benchmark("Minimal (no whitespace)",
                               bench_taurus_minimal, bench_pugixml_minimal,
                               medium_xml.c_str(), medium_xml.length());

    // Test 6: Element Only
    run_serialization_benchmark("Single Element",
                               bench_taurus_element_only, bench_pugixml_element_only,
                               small_xml.c_str(), small_xml.length());

    // Test 7: With CDATA
    run_serialization_benchmark("With CDATA Sections",
                               bench_taurus_with_cdata, bench_pugixml_with_cdata,
                               cdata_xml.c_str(), cdata_xml.length());

    // Test 8: With Entities
    run_serialization_benchmark("With Entity Encoding",
                               bench_taurus_with_entities, bench_pugixml_with_entities,
                               entities_xml.c_str(), entities_xml.length());

    printf("\n");
    printf("Serialization Benchmarks Complete\n");
    printf("\n");

    return 0;
}
