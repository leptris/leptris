/**
 * benchmark_parse.cpp - Scenario 1: Parse Only (Cold Start)
 *
 * Measures pure parsing speed WITHOUT any DOM operations.
 * This tests the raw parsing performance of each library.
 *
 * Key insight: This is a "cold start" benchmark - we parse from scratch
 * each time, measuring the full parsing cost including:
 * - Memory allocation
 * - XML structure parsing
 * - DOM construction
 * - String storage
 *
 * Expected results:
 * - pugixml: Fastest for small docs (simple, in-place modification)
 * - Taurus: Competitive for medium docs (SIMD optimizations)
 * - libxml2: Slowest (full validation, more features)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cmath>

// Taurus API (C)
extern "C" {
#include <taurus.h>
}

// pugixml API (C++)
#include <pugixml.hpp>

// ============================================================================
// High-Resolution Timer
// ============================================================================

class Timer {
public:
    typedef std::chrono::high_resolution_clock Clock;
    typedef std::chrono::nanoseconds Nanoseconds;

    static inline uint64_t now_ns() {
        return std::chrono::duration_cast<Nanoseconds>(Clock::now().time_since_epoch()).count();
    }
};

// ============================================================================
// Statistical Analysis
// ============================================================================

struct Stats {
    double mean_ns;
    double median_ns;
    double stddev_ns;
    double min_ns;
    double max_ns;
    double p95_ns;
    double p99_ns;
};

static Stats calculate_stats(std::vector<uint64_t>& times_ns) {
    Stats stats = {0};

    std::sort(times_ns.begin(), times_ns.end());

    stats.min_ns = times_ns.front();
    stats.max_ns = times_ns.back();
    stats.median_ns = times_ns[times_ns.size() / 2];
    stats.p95_ns = times_ns[times_ns.size() * 95 / 100];
    stats.p99_ns = times_ns[times_ns.size() * 99 / 100];

    // Mean
    double sum = std::accumulate(times_ns.begin(), times_ns.end(), 0.0);
    stats.mean_ns = sum / times_ns.size();

    // Std dev
    double sq_sum = 0.0;
    for (auto t : times_ns) {
        sq_sum += (t - stats.mean_ns) * (t - stats.mean_ns);
    }
    stats.stddev_ns = std::sqrt(sq_sum / times_ns.size());

    return stats;
}

// ============================================================================
// Test Data
// ============================================================================

static const char* test_docs[] = {
    // Tiny (100 bytes)
    "<root><item id='1'>text</item></root>",

    // Small (~1 KB)
    "<root>"
    "<item1 id='1' name='first'>Text content 1</item1>"
    "<item2 id='2' name='second'>Text content 2</item2>"
    "<item3 id='3' name='third'>Text content 3</item3>"
    "<item4 id='4' name='fourth'>Text content 4</item4>"
    "<item5 id='5' name='fifth'>Text content 5</item5>"
    "<item6 id='6' name='sixth'>Text content 6</item6>"
    "<item7 id='7' name='seventh'>Text content 7</item7>"
    "<item8 id='8' name='eighth'>Text content 8</item8>"
    "<item9 id='9' name='ninth'>Text content 9</item9>"
    "<item10 id='10' name='tenth'>Text content 10</item10>"
    "</root>",

    // Medium (~10 KB) - generated programmatically
    NULL,  // Placeholder
};

// Generate medium document programmatically
static char* generate_medium_doc() {
    static char doc[12000];
    char* p = doc;
    p += sprintf(p, "<root>");

    for (int i = 0; i < 100; i++) {
        p += sprintf(p, "<item id='%d' name='name%d' category='cat%d'>", i, i, i % 10);

        // Add nested elements
        for (int j = 0; j < 5; j++) {
            p += sprintf(p, "<subitem id='%d_%d'>Data %d.%d</subitem>", i, j, i, j);
        }

        p += sprintf(p, "</item>");
    }

    p += sprintf(p, "</root>");
    return doc;
}

// ============================================================================
// Benchmark Functions
// ============================================================================

static Stats benchmark_taurus_parse(const char* xml, size_t len, int iterations) {
    std::vector<uint64_t> times_ns;

    // Warmup
    for (int i = 0; i < 3; i++) {
        TaurusDocument doc = taurus_parse_string(xml, len, NULL);
        if (doc) taurus_document_free(doc);
    }

    // Measure
    for (int i = 0; i < iterations; i++) {
        uint64_t start = Timer::now_ns();
        TaurusDocument doc = taurus_parse_string(xml, len, NULL);
        uint64_t end = Timer::now_ns();

        if (doc) {
            times_ns.push_back(end - start);
            taurus_document_free(doc);
        }
    }

    return calculate_stats(times_ns);
}

static Stats benchmark_pugixml_parse(const char* xml, size_t len, int iterations) {
    std::vector<uint64_t> times_ns;

    // Warmup
    for (int i = 0; i < 3; i++) {
        pugi::xml_document doc;
        doc.load_buffer(xml, len);
    }

    // Measure
    for (int i = 0; i < iterations; i++) {
        uint64_t start = Timer::now_ns();
        pugi::xml_document doc;
        doc.load_buffer(xml, len);
        uint64_t end = Timer::now_ns();

        times_ns.push_back(end - start);
    }

    return calculate_stats(times_ns);
}

// ============================================================================
// Result Reporting
// ============================================================================

static void print_result(const char* name, const Stats& stats) {
    printf("  %s:\n", name);
    printf("    Mean:   %.2f µs\n", stats.mean_ns / 1000.0);
    printf("    Median: %.2f µs\n", stats.median_ns / 1000.0);
    printf("    StdDev: %.2f µs\n", stats.stddev_ns / 1000.0);
    printf("    Min:    %.2f µs\n", stats.min_ns / 1000.0);
    printf("    Max:    %.2f µs\n", stats.max_ns / 1000.0);
    printf("    P95:    %.2f µs\n", stats.p95_ns / 1000.0);
    printf("    P99:    %.2f µs\n", stats.p99_ns / 1000.0);
}

static void print_comparison(const char* doc_name, const Stats& taurus, const Stats& pugixml) {
    double taurus_mean = taurus.mean_ns;
    double pugixml_mean = pugixml.mean_ns;
    double ratio = pugixml_mean / taurus_mean;

    printf("\n  Comparison (%s):\n", doc_name);
    printf("    Taurus:  %.2f µs\n", taurus_mean / 1000.0);
    printf("    pugixml:  %.2f µs\n", pugixml_mean / 1000.0);
    printf("    Ratio:   %.2fx (%s)\n", ratio,
           ratio >= 1.2 ? "Taurus AHEAD ✅" :
           ratio > 0.8 ? "~ PARITY" : "pugixml AHEAD ⚠️");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     Benchmark: Scenario 1 - Parse Only (Cold Start)        ║\n");
    printf("║     Measures pure parsing speed without any operations     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    const int ITERATIONS = 100;

    // Generate medium document
    test_docs[2] = generate_medium_doc();

    // Test each document size
    const char* doc_names[] = {"Tiny (100B)", "Small (~1KB)", "Medium (~10KB)"};

    for (int doc_idx = 0; doc_idx < 3; doc_idx++) {
        const char* xml = test_docs[doc_idx];
        size_t len = strlen(xml);

        printf("═══ %s ═══\n", doc_names[doc_idx]);
        printf("Size: %zu bytes\n\n", len);

        // Benchmark Taurus
        Stats taurus_stats = benchmark_taurus_parse(xml, len, ITERATIONS);
        print_result("Taurus", taurus_stats);

        // Benchmark pugixml
        Stats pugixml_stats = benchmark_pugixml_parse(xml, len, ITERATIONS);
        print_result("pugixml", pugixml_stats);

        // Comparison
        print_comparison(doc_names[doc_idx], taurus_stats, pugixml_stats);
        printf("\n");
    }

    printf("═══════════════════════════════════════════════════════════\n");
    printf("Conclusion:\n");
    printf("  - Parse-only tests show RAW parsing performance\n");
    printf("  - pugixml expected to lead for small docs (less overhead)\n");
    printf("  - Taurus expected to be competitive for medium/large docs\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n");

    return 0;
}
