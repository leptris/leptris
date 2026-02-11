/**
 * benchmark_first_access.cpp - Scenario 2: First Access (Cold Operations)
 *
 * Measures lazy initialization costs on FIRST access to DOM operations.
 * This is CRITICAL for real-world performance where most operations
 * are performed once, not repeated thousands of times.
 *
 * What we measure:
 * - First access to child_count (may trigger array rebuild)
 * - First access to child(0) (may trigger array rebuild)
 * - First access to element_name (triggers StringView→string conversion)
 * - First access to attribute() (may trigger hash table build)
 *
 * Expected results:
 * - pugixml: Fastest (no lazy work, everything ready after parse)
 * - Taurus: Slower due to lazy costs (StringView conversion, array rebuild)
 * - This is the REAL cost for single-access applications!
 *
 * IMPORTANT: This benchmark does NOT warm up - we WANT to measure cold costs!
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
};

static Stats calculate_stats(std::vector<uint64_t>& times_ns) {
    Stats stats = {0};

    std::sort(times_ns.begin(), times_ns.end());

    stats.min_ns = times_ns.front();
    stats.max_ns = times_ns.back();
    stats.median_ns = times_ns[times_ns.size() / 2];

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

static const char* test_xml =
    "<root>"
    "  <item1 id='1' name='first' category='A'>Text 1</item1>"
    "  <item2 id='2' name='second' category='B'>Text 2</item2>"
    "  <item3 id='3' name='third' category='C'>Text 3</item3>"
    "  <item4 id='4' name='fourth' category='D'>Text 4</item4>"
    "  <item5 id='5' name='fifth' category='E'>Text 5</item5>"
    "</root>";

// ============================================================================
// Benchmark Functions - Taurus
// ============================================================================

static Stats benchmark_taurus_first_access(int iterations) {
    std::vector<uint64_t> times_ns;
    size_t len = strlen(test_xml);

    for (int i = 0; i < iterations; i++) {
        // Parse (pays lazy StringView parsing, but NOT string conversion)
        TaurusDocument doc = taurus_parse_string(test_xml, len, NULL);
        if (!doc) continue;

        TaurusElement root = taurus_document_root(doc);

        // MEASURE: First access operations (triggers lazy costs)
        uint64_t start = Timer::now_ns();

        // These are FIRST accesses:
        size_t child_count = taurus_element_child_count(root);  // May rebuild array
        if (child_count > 0) {
            TaurusElement child = taurus_element_child(root, 0);  // May rebuild array
            if (child) {
                const char* name = taurus_element_name(child);  // Triggers StringView→string conversion!
                const char* id = taurus_element_attribute(child, "id");  // May build hash table!
                const char* category = taurus_element_attribute(child, "category");  // O(1) if hash built
                (void)name; (void)id; (void)category;  // Use results
            }
        }

        uint64_t end = Timer::now_ns();

        times_ns.push_back(end - start);
        taurus_document_free(doc);
    }

    return calculate_stats(times_ns);
}

// ============================================================================
// Benchmark Functions - pugixml
// ============================================================================

static Stats benchmark_pugixml_first_access(int iterations) {
    std::vector<uint64_t> times_ns;
    size_t len = strlen(test_xml);

    for (int i = 0; i < iterations; i++) {
        // Parse (in-place modification, strings ready)
        pugi::xml_document doc;
        doc.load_buffer(test_xml, len);
        pugi::xml_node root = doc.child("root");

        // MEASURE: First access operations
        uint64_t start = Timer::now_ns();

        // These are also first accesses, but no lazy cost:
        int child_count = 0;
        for (pugi::xml_node child : root.children()) {
            child_count++;
            const char* name = child.name();  // Already available (pointer into buffer)
            const char* id = child.attribute("id").value();  // O(n) search every time
            const char* category = child.attribute("category").value();  // O(n) search
            (void)name; (void)id; (void)category;
            if (child_count >= 1) break;  // Just access first child
        }

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
}

static void print_comparison(const Stats& taurus, const Stats& pugixml) {
    double taurus_mean = taurus.mean_ns;
    double pugixml_mean = pugixml.mean_ns;
    double ratio = pugixml_mean / taurus_mean;

    printf("\n  Comparison:\n");
    printf("    Taurus:  %.2f µs\n", taurus_mean / 1000.0);
    printf("    pugixml:  %.2f µs\n", pugixml_mean / 1000.0);
    printf("    Ratio:   %.2fx (%s)\n", ratio,
           ratio >= 1.2 ? "Taurus AHEAD ✅" :
           ratio > 0.8 ? "~ PARITY" : "pugixml AHEAD ⚠️");

    if (ratio < 1.0) {
        printf("    ⚠️  NOTE: Taurus slower due to lazy initialization costs\n");
        printf("    - StringView→string conversion on first name access\n");
        printf("    - Array rebuild on first child access\n");
        printf("    - Hash table build on first attribute access\n");
        printf("    → These costs are AMORTIZED over repeated accesses\n");
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     Benchmark: Scenario 2 - First Access (Cold Ops)        ║\n");
    printf("║     Measures lazy initialization costs on FIRST access     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    const int ITERATIONS = 1000;

    printf("Test Document (%zu bytes):\n", strlen(test_xml));
    printf("  - 5 child elements\n");
    printf("  - 3 attributes per element\n");
    printf("  - Mix of element names and text content\n\n");

    printf("Operations measured (FIRST access only):\n");
    printf("  1. Get child count (may trigger array rebuild)\n");
    printf("  2. Get first child (may trigger array rebuild)\n");
    printf("  3. Get child name (triggers StringView→string conversion)\n");
    printf("  4. Get 'id' attribute (may trigger hash table build)\n");
    printf("  5. Get 'category' attribute (O(1) if hash built)\n\n");

    // Benchmark Taurus
    Stats taurus_stats = benchmark_taurus_first_access(ITERATIONS);
    print_result("Taurus", taurus_stats);

    // Benchmark pugixml
    Stats pugixml_stats = benchmark_pugixml_first_access(ITERATIONS);
    print_result("pugixml", pugixml_stats);

    // Comparison
    print_comparison(taurus_stats, pugixml_stats);

    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("Key Insights:\n");
    printf("  - First access includes LAZY initialization costs\n");
    printf("  - Taurus pays: StringView conversion + array rebuild + hash build\n");
    printf("  - pugixml pays: Nothing (everything ready after parse)\n");
    printf("  → This is REAL-WORLD performance for single-access apps!\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n");

    return 0;
}
