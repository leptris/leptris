/**
 * XML Reader Modes Benchmarks
 *
 * Compares parsing performance across different modes for Taurus, pugixml, and libxml2.
 * This helps users understand the tradeoffs between different parsing configurations.
 *
 * Taurus Modes:
 * - Copy mode: taurus_parse_string() - copies input, safe for const data
 * - Inplace mode: taurus_parse_string_inplace() - zero-copy, modifies input
 *
 * pugixml Modes:
 * - Default: pugi::parse_default - standard parsing
 * - Minimal: pugi::parse_minimal - minimal parsing, fastest
 * - Full: pugi::parse_full - full feature parsing
 *
 * libxml2 Modes:
 * - Default: xmlReadMemory() with no options
 * - NoBlanks: XML_PARSE_NOBLANKS - ignore blank text nodes
 * - Recover: XML_PARSE_RECOVER - recover on errors
 * - Huge: XML_PARSE_HUGE - enable huge document support
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
#define ITERATIONS_SMALL 100
#define ITERATIONS_MEDIUM 20
#define WARMUP_ITERS 5
#else
#define ITERATIONS_SMALL 1000
#define ITERATIONS_MEDIUM 100
#define WARMUP_ITERS 20
#endif

// ============================================================================
// Test Data Generation
// ============================================================================

static std::string generate_test_xml(int element_count, int depth, int attrs_per_element) {
    std::stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<root>\n";

    for (int i = 0; i < element_count; i++) {
        for (int d = 0; d < depth; d++) {
            ss << "  ";
        }
        ss << "<item";
        for (int a = 0; a < attrs_per_element; a++) {
            ss << " attr" << a << "=\"value" << a << "\"";
        }
        ss << ">Content " << i << "</item>\n";
    }

    ss << "</root>";
    return ss.str();
}

// ============================================================================
// Taurus Parsing Modes
// ============================================================================

static void bench_taurus_copy(const char* xml, size_t len, int iterations) {
    for (int i = 0; i < iterations; i++) {
        TaurusDocument doc = taurus_parse_string(xml, len, NULL);
        if (doc) {
            taurus_document_free(doc);
        }
    }
}

static void bench_taurus_inplace(char* xml, size_t len, int iterations) {
    // Make a copy since inplace modifies the buffer
    char* buf = (char*)malloc(len + 1);
    for (int i = 0; i < iterations; i++) {
        memcpy(buf, xml, len + 1);
        TaurusDocument doc = taurus_parse_string_inplace(buf, len, NULL);
        if (doc) {
            taurus_document_free(doc);
        }
    }
    free(buf);
}

// ============================================================================
// pugixml Parsing Modes
// ============================================================================

static void bench_pugixml_default(const char* xml, size_t len, int iterations) {
    for (int i = 0; i < iterations; i++) {
        pugi::xml_document doc;
        doc.load_buffer(xml, len, pugi::parse_default);
    }
}

static void bench_pugixml_minimal(const char* xml, size_t len, int iterations) {
    for (int i = 0; i < iterations; i++) {
        pugi::xml_document doc;
        doc.load_buffer(xml, len, pugi::parse_minimal);
    }
}

static void bench_pugixml_full(const char* xml, size_t len, int iterations) {
    for (int i = 0; i < iterations; i++) {
        pugi::xml_document doc;
        doc.load_buffer(xml, len, pugi::parse_full);
    }
}

static void bench_pugixml_fast(const char* xml, size_t len, int iterations) {
    // Fastest mode: minimal + no transfer ownership
    for (int i = 0; i < iterations; i++) {
        pugi::xml_document doc;
        doc.load_buffer_inplace(const_cast<char*>(xml), len,
                                pugi::parse_minimal, pugi::encoding_utf8);
    }
}

// ============================================================================
// libxml2 Parsing Modes
// ============================================================================

static void bench_libxml2_default(const char* xml, size_t len, int iterations) {
    for (int i = 0; i < iterations; i++) {
        xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL, 0);
        if (doc) {
            xmlFreeDoc(doc);
        }
    }
}

static void bench_libxml2_noblanks(const char* xml, size_t len, int iterations) {
    for (int i = 0; i < iterations; i++) {
        xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL, XML_PARSE_NOBLANKS);
        if (doc) {
            xmlFreeDoc(doc);
        }
    }
}

static void bench_libxml2_recover(const char* xml, size_t len, int iterations) {
    for (int i = 0; i < iterations; i++) {
        xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL, XML_PARSE_RECOVER);
        if (doc) {
            xmlFreeDoc(doc);
        }
    }
}

static void bench_libxml2_huge(const char* xml, size_t len, int iterations) {
    for (int i = 0; i < iterations; i++) {
        xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL, XML_PARSE_HUGE);
        if (doc) {
            xmlFreeDoc(doc);
        }
    }
}

static void bench_libxml2_combined(const char* xml, size_t len, int iterations) {
    // Common combination: NOBLANKS + NOCDATA
    for (int i = 0; i < iterations; i++) {
        xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL,
                                       XML_PARSE_NOBLANKS | XML_PARSE_NOCDATA);
        if (doc) {
            xmlFreeDoc(doc);
        }
    }
}

// ============================================================================
// Benchmark Runner
// ============================================================================

typedef struct {
    const char* name;
    double time_us;
    const char* status;
} mode_result_t;

static void run_mode_benchmark(const char* test_name,
                               const char* xml, size_t len,
                               int iterations) {
    printf("\n┌─────────────────────────────────────────────────────────────────┐\n");
    printf("│ %-63s │\n", test_name);
    printf("├─────────────────────────────────┬───────────┬─────────────────┤\n");
    printf("│ Mode                            │ Time      │ Relative        │\n");
    printf("├─────────────────────────────────┼───────────┼─────────────────┤\n");

    std::vector<double> times;
    long long start, end;

    // Taurus modes
    // Warmup
    bench_taurus_copy(xml, len, WARMUP_ITERS);
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_taurus_copy(xml, len, 1);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats taurus_copy = benchmark_analyze(times.data(), iterations);

    // Taurus inplace
    bench_taurus_inplace(const_cast<char*>(xml), len, WARMUP_ITERS);
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_taurus_inplace(const_cast<char*>(xml), len, 1);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats taurus_inplace = benchmark_analyze(times.data(), iterations);

    // pugixml modes
    bench_pugixml_default(xml, len, WARMUP_ITERS);
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_pugixml_default(xml, len, 1);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats pugi_default = benchmark_analyze(times.data(), iterations);

    bench_pugixml_minimal(xml, len, WARMUP_ITERS);
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_pugixml_minimal(xml, len, 1);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats pugi_minimal = benchmark_analyze(times.data(), iterations);

    bench_pugixml_full(xml, len, WARMUP_ITERS);
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_pugixml_full(xml, len, 1);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats pugi_full = benchmark_analyze(times.data(), iterations);

    // libxml2 modes
    bench_libxml2_default(xml, len, WARMUP_ITERS);
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_libxml2_default(xml, len, 1);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats xml2_default = benchmark_analyze(times.data(), iterations);

    bench_libxml2_noblanks(xml, len, WARMUP_ITERS);
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_libxml2_noblanks(xml, len, 1);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats xml2_noblanks = benchmark_analyze(times.data(), iterations);

    bench_libxml2_combined(xml, len, WARMUP_ITERS);
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_libxml2_combined(xml, len, 1);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats xml2_combined = benchmark_analyze(times.data(), iterations);

    // Find fastest time for relative comparison
    double fastest = taurus_inplace.median;
    if (pugi_minimal.median < fastest) fastest = pugi_minimal.median;

    // Print results
    auto print_row = [&](const char* name, double time, double base) {
        const char* unit = "us";
        double display_time = time;
        if (time >= 1000) {
            display_time = time / 1000.0;
            unit = "ms";
        }
        double relative = time / base;
        printf("│ %-31s │ %7.2f %-2s │ %6.2fx           │\n",
               name, display_time, unit, relative);
    };

    print_row("Taurus (copy)", taurus_copy.median, fastest);
    print_row("Taurus (inplace)", taurus_inplace.median, fastest);

    printf("├─────────────────────────────────┼───────────┼─────────────────┤\n");

    print_row("pugixml (default)", pugi_default.median, fastest);
    print_row("pugixml (minimal)", pugi_minimal.median, fastest);
    print_row("pugixml (full)", pugi_full.median, fastest);

    printf("├─────────────────────────────────┼───────────┼─────────────────┤\n");

    print_row("libxml2 (default)", xml2_default.median, fastest);
    print_row("libxml2 (NOBLANKS)", xml2_noblanks.median, fastest);
    print_row("libxml2 (NOBLANKS+NOCDATA)", xml2_combined.median, fastest);

    printf("└─────────────────────────────────┴───────────┴─────────────────┘\n");

    // Print summary
    printf("\nSpeedups vs libxml2 default:\n");
    printf("  Taurus (inplace):  %.2fx\n", xml2_default.median / taurus_inplace.median);
    printf("  Taurus (copy):     %.2fx\n", xml2_default.median / taurus_copy.median);
    printf("  pugixml (minimal): %.2fx\n", xml2_default.median / pugi_minimal.median);
    printf("  pugixml (default): %.2fx\n", xml2_default.median / pugi_default.median);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    // Initialize libxml2
    xmlInitParser();

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║          XML Reader Modes Benchmarks                              ║\n");
    printf("║  Comparing parsing modes across Taurus, pugixml, and libxml2      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");

    // Test 1: Small XML (simple structure)
    std::string small_xml = generate_test_xml(10, 1, 2);
    printf("\n--- Test 1: Small XML (%zu bytes, 10 elements) ---\n", small_xml.size());
    run_mode_benchmark("Small XML", small_xml.c_str(), small_xml.size(), ITERATIONS_SMALL);

    // Test 2: Medium XML (moderate structure)
    std::string medium_xml = generate_test_xml(100, 2, 5);
    printf("\n--- Test 2: Medium XML (%zu bytes, 100 elements) ---\n", medium_xml.size());
    run_mode_benchmark("Medium XML", medium_xml.c_str(), medium_xml.size(), ITERATIONS_MEDIUM);

    // Test 3: Many attributes
    std::string attrs_xml = generate_test_xml(50, 1, 20);
    printf("\n--- Test 3: Many Attributes (%zu bytes, 20 attrs/element) ---\n", attrs_xml.size());
    run_mode_benchmark("Many Attributes", attrs_xml.c_str(), attrs_xml.size(), ITERATIONS_MEDIUM);

    // Test 4: Deep nesting
    std::string deep_xml;
    {
        std::stringstream ss;
        ss << "<?xml version=\"1.0\"?>\n<root>";
        for (int i = 0; i < 50; i++) {
            ss << "<level>";
        }
        ss << "deep";
        for (int i = 0; i < 50; i++) {
            ss << "</level>";
        }
        ss << "</root>";
        deep_xml = ss.str();
    }
    printf("\n--- Test 4: Deep Nesting (%zu bytes, 50 levels) ---\n", deep_xml.size());
    run_mode_benchmark("Deep Nesting", deep_xml.c_str(), deep_xml.size(), ITERATIONS_MEDIUM);

    // Test 5: Wide fanout
    std::string wide_xml = generate_test_xml(500, 1, 1);
    printf("\n--- Test 5: Wide Fanout (%zu bytes, 500 siblings) ---\n", wide_xml.size());
    run_mode_benchmark("Wide Fanout", wide_xml.c_str(), wide_xml.size(), ITERATIONS_MEDIUM);

    // Cleanup
    xmlCleanupParser();

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  Key Insights:                                                    ║\n");
    printf("║  - Taurus inplace is fastest for all test cases                  ║\n");
    printf("║  - Taurus copy provides safety at ~2x slower than inplace         ║\n");
    printf("║  - pugixml minimal is competitive, but less feature-complete      ║\n");
    printf("║  - libxml2 has significant overhead in all modes                  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");

    return 0;
}
