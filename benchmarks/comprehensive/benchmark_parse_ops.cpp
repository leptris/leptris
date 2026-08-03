/**
 * benchmark_parse_ops.cpp - Scenario 4: Parse + Single Operations
 *
 * Measures REALISTIC end-to-end workflow where applications:
 * 1. Parse a document
 * 2. Perform operations ONCE (read/query/modify)
 * 3. Free the document
 *
 * This is the MOST REALISTIC benchmark for most applications:
 * - Config file loaders (parse once, read once)
 * - API clients (parse response, extract data)
 * - Data processors (parse, transform, output)
 * - Query engines (parse, execute query, get results)
 *
 * Expected results:
 * - Combined metric shows TRUE performance for real apps
 * - Parse cost + first-access cost = total workflow cost
 * - May favor pugixml for simple workflows
 * - May favor Taurus for complex workflows (amortized over multiple ops)
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
// Timer & Statistics
// ============================================================================

class Timer {
public:
    typedef std::chrono::high_resolution_clock Clock;
    typedef std::chrono::nanoseconds Nanoseconds;

    static inline uint64_t now_ns() {
        return std::chrono::duration_cast<Nanoseconds>(Clock::now().time_since_epoch()).count();
    }
};

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

    double sum = std::accumulate(times_ns.begin(), times_ns.end(), 0.0);
    stats.mean_ns = sum / times_ns.size();

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
    "  <header><title>Test Document</title><version>1.0</version></header>"
    "  <data>"
    "    <item id='1' name='first' category='A' priority='high'>Value 1</item>"
    "    <item id='2' name='second' category='B' priority='low'>Value 2</item>"
    "    <item id='3' name='third' category='A' priority='medium'>Value 3</item>"
    "    <item id='4' name='fourth' category='C' priority='high'>Value 4</item>"
    "    <item id='5' name='fifth' category='B' priority='low'>Value 5</item>"
    "  </data>"
    "  <footer><count>5</count><status>complete</status></footer>"
    "</root>";

// ============================================================================
// Workflow Operations
// ============================================================================

// Workflow 1: Parse + Simple Read (get title)
static uint64_t workflow_parse_and_read_simple() {
    uint64_t start, end;

    // Taurus version
    start = Timer::now_ns();
    {
        TaurusDocument doc = taurus_parse_string(test_xml, strlen(test_xml), NULL);
        if (doc) {
            TaurusElement root = taurus_document_root(doc);
            // Navigate: root > header > title
            size_t count = taurus_element_child_count(root);
            for (size_t i = 0; i < count; i++) {
                TaurusElement child = taurus_element_child(root, i);
                const char* name = taurus_element_name(child);
                if (name && strcmp(name, "header") == 0) {
                    size_t header_children = taurus_element_child_count(child);
                    for (size_t j = 0; j < header_children; j++) {
                        TaurusElement header_child = taurus_element_child(child, j);
                        const char* hname = taurus_element_name(header_child);
                        if (hname && strcmp(hname, "title") == 0) {
                            const char* text = taurus_element_text(header_child);
                            (void)text;  // Use result
                            break;
                        }
                    }
                    break;
                }
            }
            taurus_document_free(doc);
        }
    }
    end = Timer::now_ns();
    uint64_t taurus_time = end - start;

    // pugixml version
    start = Timer::now_ns();
    {
        pugi::xml_document doc;
        doc.load_buffer(test_xml, strlen(test_xml));
        pugi::xml_node root = doc.child("root");
        pugi::xml_node header = root.child("header");
        pugi::xml_node title = header.child("title");
        const char* text = text = title.child_value();
        (void)text;
    }
    end = Timer::now_ns();
    uint64_t pugixml_time = end - start;

    // Return combined (we'll compare separately in real benchmark)
    return taurus_time + pugixml_time;
}

// Workflow 2: Parse + Attribute Query (find items by category)
static void taurus_workflow_attribute_query() {
    TaurusDocument doc = taurus_parse_string(test_xml, strlen(test_xml), NULL);
    if (!doc) return;

    TaurusElement root = taurus_document_root(doc);
    TaurusElement data = NULL;
    size_t count = taurus_element_child_count(root);
    for (size_t i = 0; i < count; i++) {
        TaurusElement child = taurus_element_child(root, i);
        const char* name = taurus_element_name(child);
        if (name && strcmp(name, "data") == 0) {
            data = child;
            break;
        }
    }

    if (data) {
        size_t item_count = taurus_element_child_count(data);
        int category_a_count = 0;
        for (size_t i = 0; i < item_count; i++) {
            TaurusElement item = taurus_element_child(data, i);
            const char* category = taurus_element_attribute(item, "category");
            if (category && strcmp(category, "A") == 0) {
                category_a_count++;
            }
        }
        (void)category_a_count;
    }

    taurus_document_free(doc);
}

static void pugixml_workflow_attribute_query() {
    pugi::xml_document doc;
    doc.load_buffer(test_xml, strlen(test_xml));
    pugi::xml_node root = doc.child("root");
    pugi::xml_node data = root.child("data");

    int category_a_count = 0;
    for (pugi::xml_node item : data.children("item")) {
        const char* category = item.attribute("category").value();
        if (strcmp(category, "A") == 0) {
            category_a_count++;
        }
    }
    (void)category_a_count;
}

// Workflow 3: Parse + Tree Traversal (visit all nodes)
static void taurus_workflow_tree_traversal() {
    TaurusDocument doc = taurus_parse_string(test_xml, strlen(test_xml), NULL);
    if (!doc) return;

    TaurusElement root = taurus_document_root(doc);
    int node_count = 0;

    // Simple recursive traversal
    std::function<void(TaurusElement)> traverse = [&](TaurusElement elem) {
        node_count++;
        size_t count = taurus_element_child_count(elem);
        for (size_t i = 0; i < count; i++) {
            TaurusElement child = taurus_element_child(elem, i);
            if (child) traverse(child);
        }
    };

    traverse(root);
    (void)node_count;

    taurus_document_free(doc);
}

static void pugixml_workflow_tree_traversal() {
    pugi::xml_document doc;
    doc.load_buffer(test_xml, strlen(test_xml));

    int node_count = 0;
    std::function<void(pugi::xml_node)> traverse = [&](pugi::xml_node node) {
        node_count++;
        for (pugi::xml_node child : node.children()) {
            traverse(child);
        }
    };

    traverse(doc.first_child());
    (void)node_count;
}

// ============================================================================
// Benchmark Functions
// ============================================================================

static Stats benchmark_taurus_parse_ops(void (*workflow_func)(), int iterations) {
    std::vector<uint64_t> times_ns;

    for (int i = 0; i < iterations; i++) {
        uint64_t start = Timer::now_ns();
        workflow_func();
        uint64_t end = Timer::now_ns();
        times_ns.push_back(end - start);
    }

    return calculate_stats(times_ns);
}

static Stats benchmark_pugixml_parse_ops(void (*workflow_func)(), int iterations) {
    std::vector<uint64_t> times_ns;

    for (int i = 0; i < iterations; i++) {
        uint64_t start = Timer::now_ns();
        workflow_func();
        uint64_t end = Timer::now_ns();
        times_ns.push_back(end - start);
    }

    return calculate_stats(times_ns);
}

// ============================================================================
// Reporting
// ============================================================================

static void print_result(const char* name, const Stats& stats) {
    printf("  %s:\n", name);
    printf("    Mean: %.2f µs\n", stats.mean_ns / 1000.0);
    printf("    Median: %.2f µs\n", stats.median_ns / 1000.0);
}

static void print_comparison(const Stats& taurus, const Stats& pugixml) {
    double ratio = pugixml.mean_ns / taurus.mean_ns;
    printf("    Ratio: %.2fx (%s)\n", ratio,
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
    printf("║     Benchmark: Scenario 4 - Parse + Single Operations     ║\n");
    printf("║     Measures REALISTIC end-to-end workflows               ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    const int ITERATIONS = 1000;

    // Workflow 1: Simple Read
    printf("═══ Workflow 1: Parse + Simple Read ═══\n");
    printf("Operation: Parse document, extract title value\n\n");

    Stats taurus_simple = benchmark_taurus_parse_ops([]() {
        // Inline simple read workflow for Taurus
        TaurusDocument doc = taurus_parse_string(test_xml, strlen(test_xml), NULL);
        if (doc) {
            TaurusElement root = taurus_document_root(doc);
            size_t count = taurus_element_child_count(root);
            for (size_t i = 0; i < count; i++) {
                TaurusElement child = taurus_element_child(root, i);
                const char* name = taurus_element_name(child);
                if (name && strcmp(name, "header") == 0) {
                    size_t hcount = taurus_element_child_count(child);
                    for (size_t j = 0; j < hcount; j++) {
                        TaurusElement hc = taurus_element_child(child, j);
                        const char* hname = taurus_element_name(hc);
                        if (hname && strcmp(hname, "title") == 0) {
                            const char* text = taurus_element_text(hc);
                            (void)text;
                            break;
                        }
                    }
                    break;
                }
            }
            taurus_document_free(doc);
        }
    }, ITERATIONS);

    Stats pugixml_simple = benchmark_pugixml_parse_ops([]() {
        pugi::xml_document doc;
        doc.load_buffer(test_xml, strlen(test_xml));
        pugi::xml_node title = doc.child("root").child("header").child("title");
        const char* text = title.child_value();
        (void)text;
    }, ITERATIONS);

    print_result("Taurus", taurus_simple);
    print_result("pugixml", pugixml_simple);
    print_comparison(taurus_simple, pugixml_simple);
    printf("\n");

    // Workflow 2: Attribute Query
    printf("═══ Workflow 2: Parse + Attribute Query ═══\n");
    printf("Operation: Parse document, find items with category='A'\n\n");

    Stats taurus_attr = benchmark_taurus_parse_ops(taurus_workflow_attribute_query, ITERATIONS);
    Stats pugixml_attr = benchmark_pugixml_parse_ops(pugixml_workflow_attribute_query, ITERATIONS);

    print_result("Taurus", taurus_attr);
    print_result("pugixml", pugixml_attr);
    print_comparison(taurus_attr, pugixml_attr);
    printf("\n");

    // Workflow 3: Tree Traversal
    printf("═══ Workflow 3: Parse + Tree Traversal ═══\n");
    printf("Operation: Parse document, visit all nodes recursively\n\n");

    Stats taurus_traverse = benchmark_taurus_parse_ops(taurus_workflow_tree_traversal, ITERATIONS);
    Stats pugixml_traverse = benchmark_pugixml_parse_ops(pugixml_workflow_tree_traversal, ITERATIONS);

    print_result("Taurus", taurus_traverse);
    print_result("pugixml", pugixml_traverse);
    print_comparison(taurus_traverse, pugixml_traverse);
    printf("\n");

    printf("═══════════════════════════════════════════════════════════\n");
    printf("Conclusion:\n");
    printf("  - These workflows represent REAL application usage\n");
    printf("  - Parse + operations combined = true end-to-end cost\n");
    printf("  - Results show practical performance, not theoretical\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n");

    return 0;
}
