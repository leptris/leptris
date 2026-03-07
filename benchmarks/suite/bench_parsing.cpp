/**
 * XML Parsing Benchmarks
 *
 * Measures parsing performance across various XML sizes and structures.
 * Target: >= 0.33x vs pugixml (within 3x), >= 1.0x vs libxml2
 *
 * Tests:
 * 1. Parse small file (500 B)
 * 2. Parse medium file (50 KB)
 * 3. Parse large file (5 MB)
 * 4. Parse deep nesting (100 levels)
 * 5. Parse wide fanout (1000 siblings)
 * 6. Parse with many attributes (100 attrs/element)
 * 7. Parse with namespaces
 * 8. Parse with CDATA
 * 9. Parse with comments
 * 10. Parse with processing instructions
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

// libxml2 API (C)
#include <libxml/parser.h>
#include <libxml/tree.h>

// Benchmark utilities
extern "C" {
#include "utils.h"
}

// Quick mode for development (set to 0 for full runs)
#define QUICK_MODE 1

#if QUICK_MODE
#define PARSE_ITERATIONS_SMALL 100
#define PARSE_ITERATIONS_MEDIUM 10
#define PARSE_ITERATIONS_LARGE 1
#define WARMUP_ITERS 10
#else
#define PARSE_ITERATIONS_SMALL 10000
#define PARSE_ITERATIONS_MEDIUM 1000
#define PARSE_ITERATIONS_LARGE 10
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
// Benchmark Context
// ============================================================================

typedef struct {
    const char* xml;
    size_t len;
    const char* name;
    int iterations;
} parse_context_t;

// ============================================================================
// Taurus Parse Functions
// ============================================================================

static void bench_taurus_parse(void* ctx) {
    parse_context_t* pctx = (parse_context_t*)ctx;
    TaurusDocument doc = taurus_parse_string(pctx->xml, pctx->len, NULL);
    if (doc) {
        taurus_document_free(doc);
    }
}

// ============================================================================
// pugixml Parse Functions
// ============================================================================

static void bench_pugixml_parse(void* ctx) {
    parse_context_t* pctx = (parse_context_t*)ctx;
    pugi::xml_document doc;
    doc.load_buffer(pctx->xml, pctx->len);
}

// ============================================================================
// libxml2 Parse Functions
// ============================================================================

static void bench_libxml2_parse(void* ctx) {
    parse_context_t* pctx = (parse_context_t*)ctx;
    xmlDocPtr doc = xmlReadMemory(pctx->xml, (int)pctx->len, NULL, NULL, 0);
    if (doc) {
        xmlFreeDoc(doc);
    }
}

// ============================================================================
// Benchmark Runner
// ============================================================================

static void run_parse_benchmark(const char* name,
                                const char* xml, size_t len,
                                int iterations) {
    parse_context_t ctx = { xml, len, name, iterations };

    printf("\n=== %s (%.1f KB) ===\n", name, len / 1024.0);

    // Warmup
    for (int i = 0; i < (iterations > 100 ? 100 : iterations); i++) {
        bench_taurus_parse(&ctx);
        bench_pugixml_parse(&ctx);
    }

    // Measure Taurus
    std::vector<double> taurus_times;
    for (int i = 0; i < iterations; i++) {
        long start = benchmark_time_us();
        bench_taurus_parse(&ctx);
        long end = benchmark_time_us();
        taurus_times.push_back((double)(end - start));
    }
    benchmark_stats taurus_stats = benchmark_analyze(taurus_times.data(), iterations);

    // Measure pugixml
    std::vector<double> pugixml_times;
    for (int i = 0; i < iterations; i++) {
        long start = benchmark_time_us();
        bench_pugixml_parse(&ctx);
        long end = benchmark_time_us();
        pugixml_times.push_back((double)(end - start));
    }
    benchmark_stats pugixml_stats = benchmark_analyze(pugixml_times.data(), iterations);

    // Measure libxml2
    std::vector<double> libxml2_times;
    for (int i = 0; i < iterations; i++) {
        long start = benchmark_time_us();
        bench_libxml2_parse(&ctx);
        long end = benchmark_time_us();
        libxml2_times.push_back((double)(end - start));
    }
    benchmark_stats libxml2_stats = benchmark_analyze(libxml2_times.data(), iterations);

    // Print results
    printf("  Taurus:   %8.2f us (median), %8.2f us (p95)\n",
           taurus_stats.median, taurus_stats.p95);
    printf("  pugixml:  %8.2f us (median), %8.2f us (p95)\n",
           pugixml_stats.median, pugixml_stats.p95);
    printf("  libxml2:  %8.2f us (median), %8.2f us (p95)\n",
           libxml2_stats.median, libxml2_stats.p95);

    // Calculate speedups
    double speedup_pugixml = pugixml_stats.median / taurus_stats.median;
    double speedup_libxml2 = libxml2_stats.median / taurus_stats.median;

    printf("  vs pugixml:  %.2fx %s\n", speedup_pugixml,
           speedup_pugixml >= 0.33 ? "PASS" : "FAIL");
    printf("  vs libxml2:  %.2fx %s\n", speedup_libxml2,
           speedup_libxml2 >= 1.0 ? "PASS" : "FAIL");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    const char* data_dir = (argc > 1) ? argv[1] : "data";

    // Initialize libxml2
    xmlInitParser();

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║            XML Parsing Benchmarks (10 tests)               ║\n");
    printf("║  Target: >= 0.33x vs pugixml, >= 1.0x vs libxml2           ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // Test 1-3: Size tests
    std::string small_xml = read_file((std::string(data_dir) + "/small.xml").c_str());
    std::string medium_xml = read_file((std::string(data_dir) + "/medium.xml").c_str());
    std::string large_xml = read_file((std::string(data_dir) + "/large.xml").c_str());

    run_parse_benchmark("Parse Small", small_xml.c_str(), small_xml.length(),
                       PARSE_ITERATIONS_SMALL);
    run_parse_benchmark("Parse Medium", medium_xml.c_str(), medium_xml.length(),
                       PARSE_ITERATIONS_MEDIUM);
    run_parse_benchmark("Parse Large", large_xml.c_str(), large_xml.length(),
                       PARSE_ITERATIONS_LARGE);

    // Test 4-5: Structure tests
    std::string deep_xml = read_file((std::string(data_dir) + "/deep_100.xml").c_str());
    std::string wide_xml = read_file((std::string(data_dir) + "/wide_1000.xml").c_str());

    run_parse_benchmark("Parse Deep (100 levels)", deep_xml.c_str(), deep_xml.length(),
                       PARSE_ITERATIONS_MEDIUM);
    run_parse_benchmark("Parse Wide (1000 siblings)", wide_xml.c_str(), wide_xml.length(),
                       PARSE_ITERATIONS_MEDIUM);

    // Test 6: Attributes test
    std::string attrs_xml = read_file((std::string(data_dir) + "/attrs_100.xml").c_str());
    run_parse_benchmark("Parse Many Attributes", attrs_xml.c_str(), attrs_xml.length(),
                       PARSE_ITERATIONS_MEDIUM);

    // Test 7-10: Special content tests
    std::string cdata_xml = read_file((std::string(data_dir) + "/cdata.xml").c_str());
    std::string comment_xml = read_file((std::string(data_dir) + "/mixed_content.xml").c_str());
    std::string pi_xml = read_file((std::string(data_dir) + "/namespaces.xml").c_str());

    run_parse_benchmark("Parse CDATA", cdata_xml.c_str(), cdata_xml.length(),
                       PARSE_ITERATIONS_SMALL);
    run_parse_benchmark("Parse Comments", comment_xml.c_str(), comment_xml.length(),
                       PARSE_ITERATIONS_SMALL);
    run_parse_benchmark("Parse Processing Instructions", pi_xml.c_str(), pi_xml.length(),
                       PARSE_ITERATIONS_SMALL);

    // Test 10: Namespace test (create inline)
    const char* ns_xml =
        "<?xml version=\"1.0\"?>"
        "<root xmlns:a=\"urn:a\" xmlns:b=\"urn:b\">"
        "<a:elem a:attr=\"1\"/><b:elem b:attr=\"2\"/>"
        "</root>";
    run_parse_benchmark("Parse Namespaces", ns_xml, strlen(ns_xml),
                       PARSE_ITERATIONS_SMALL);

    // Cleanup
    xmlCleanupParser();

    printf("\n");
    printf("Parsing Benchmarks Complete\n");
    printf("\n");

    return 0;
}
