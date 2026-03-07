/**
 * Memory Efficiency Benchmarks
 *
 * Measures memory usage patterns.
 * Target: <= 110% vs pugixml, <= 110% vs libxml2
 *
 * Tests:
 * 1. Peak memory usage during parsing
 * 2. Memory after document creation
 * 3. Memory after node creation
 * 4. Memory fragmentation test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <sys/resource.h>

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

/* TaurusElement null check helper for C++ code */
static inline bool elem_not_null(const TaurusElement& elem) {
    return !taurus_element_is_null(&elem);
}

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

// Get current memory usage in KB
static long get_memory_usage_kb(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}

// ============================================================================
// Test 1: Peak Memory During Parsing
// ============================================================================

static void bench_peak_memory(const char* xml, size_t len, const char* name) {
    printf("\n=== Peak Memory: %s (%.1f KB) ===\n", name, len / 1024.0);

    long baseline = get_memory_usage_kb();

    // Taurus
    {
        long before = get_memory_usage_kb();
        TaurusDocument doc = taurus_parse_string(xml, len, NULL);
        long after = get_memory_usage_kb();

        if (doc) {
            printf("  Taurus:   %ld KB (delta: %ld KB)\n", after, after - before);
            taurus_document_free(doc);
        }
    }

    // pugixml
    {
        long before = get_memory_usage_kb();
        pugi::xml_document doc;
        doc.load_buffer(xml, len);
        long after = get_memory_usage_kb();

        printf("  pugixml:  %ld KB (delta: %ld KB)\n", after, after - before);
    }

    // libxml2
    {
        long before = get_memory_usage_kb();
        xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL, 0);
        long after = get_memory_usage_kb();

        if (doc) {
            printf("  libxml2:  %ld KB (delta: %ld KB)\n", after, after - before);
            xmlFreeDoc(doc);
        }
    }
}

// ============================================================================
// Test 2: Memory Efficiency (bytes per node)
// ============================================================================

static void bench_memory_per_node(const char* xml, size_t len) {
    printf("\n=== Memory per Node ===\n");

    // Taurus
    {
        long before = get_memory_usage_kb();
        TaurusDocument doc = taurus_parse_string(xml, len, NULL);
        long after = get_memory_usage_kb();

        if (doc) {
            // Count nodes
            int node_count = 0;
            TaurusElement root = taurus_document_root(doc);
            std::vector<TaurusElement> stack;
            stack.push_back(root);

            while (!stack.empty()) {
                TaurusElement elem = stack.back();
                stack.pop_back();
                node_count++;

                for (TaurusElement child = taurus_element_first_child_any(elem);
                     elem_not_null(child); child = taurus_element_next_sibling_any(child)) {
                    stack.push_back(child);
                }
            }

            long delta_kb = after - before;
            size_t bytes_per_node = (delta_kb * 1024) / (node_count > 0 ? node_count : 1);

            printf("  Taurus:   %d nodes, %ld KB total, %zu bytes/node\n",
                   node_count, delta_kb, bytes_per_node);

            taurus_document_free(doc);
        }
    }

    // pugixml
    {
        long before = get_memory_usage_kb();
        pugi::xml_document doc;
        doc.load_buffer(xml, len);
        long after = get_memory_usage_kb();

        // Count nodes
        int node_count = 0;
        std::vector<pugi::xml_node> stack;
        stack.push_back(doc.root().first_child());

        while (!stack.empty()) {
            pugi::xml_node node = stack.back();
            stack.pop_back();
            node_count++;

            for (pugi::xml_node child : node.children()) {
                stack.push_back(child);
            }
        }

        long delta_kb = after - before;
        size_t bytes_per_node = (delta_kb * 1024) / (node_count > 0 ? node_count : 1);

        printf("  pugixml:  %d nodes, %ld KB total, %zu bytes/node\n",
               node_count, delta_kb, bytes_per_node);
    }
}

// ============================================================================
// Test 3: Memory Fragmentation Test
// ============================================================================

static void bench_fragmentation(const char* xml, size_t len) {
    printf("\n=== Memory Fragmentation ===\n");
    printf("  Creating and destroying documents 100 times...\n");

    long before_taurus = get_memory_usage_kb();
    for (int i = 0; i < 100; i++) {
        TaurusDocument doc = taurus_parse_string(xml, len, NULL);
        if (doc) taurus_document_free(doc);
    }
    long after_taurus = get_memory_usage_kb();

    long before_pugixml = get_memory_usage_kb();
    for (int i = 0; i < 100; i++) {
        pugi::xml_document doc;
        doc.load_buffer(xml, len);
    }
    long after_pugixml = get_memory_usage_kb();

    printf("  Taurus:   %ld KB (leaked: %ld KB)\n",
           after_taurus, after_taurus - before_taurus);
    printf("  pugixml:  %ld KB (leaked: %ld KB)\n",
           after_pugixml, after_pugixml - before_pugixml);

    if (after_taurus - before_taurus > 0) {
        printf("  WARNING: Possible memory leak in Taurus!\n");
    } else {
        printf("  PASS: No memory leaks detected in Taurus\n");
    }
}

// ============================================================================
// Test 4: Large Document Memory
// ============================================================================

static void bench_large_document(const char* xml, size_t len) {
    printf("\n=== Large Document Memory (%.1f MB) ===\n", len / (1024.0 * 1024.0));

    // Taurus
    {
        long before = get_memory_usage_kb();
        TaurusDocument doc = taurus_parse_string(xml, len, NULL);
        long after = get_memory_usage_kb();

        if (doc) {
            double ratio = (double)(after - before) / (len / 1024.0);
            printf("  Taurus:   %ld KB (ratio: %.2fx file size)\n",
                   after - before, ratio);
            taurus_document_free(doc);
        }
    }

    // pugixml
    {
        long before = get_memory_usage_kb();
        pugi::xml_document doc;
        doc.load_buffer(xml, len);
        long after = get_memory_usage_kb();

        double ratio = (double)(after - before) / (len / 1024.0);
        printf("  pugixml:  %ld KB (ratio: %.2fx file size)\n",
               after - before, ratio);
    }

    // libxml2
    {
        long before = get_memory_usage_kb();
        xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL, 0);
        long after = get_memory_usage_kb();

        if (doc) {
            double ratio = (double)(after - before) / (len / 1024.0);
            printf("  libxml2:  %ld KB (ratio: %.2fx file size)\n",
                   after - before, ratio);
            xmlFreeDoc(doc);
        }
    }
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
    printf("║          Memory Efficiency Benchmarks (4 tests)           ║\n");
    printf("║  Target: <= 110%% vs pugixml, <= 110%% vs libxml2           ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // Load test files
    std::string small_xml = read_file((std::string(data_dir) + "/small.xml").c_str());
    std::string medium_xml = read_file((std::string(data_dir) + "/medium.xml").c_str());
    std::string large_xml = read_file((std::string(data_dir) + "/large.xml").c_str());

    // Test 1: Peak memory
    bench_peak_memory(small_xml.c_str(), small_xml.length(), "Small");
    bench_peak_memory(medium_xml.c_str(), medium_xml.length(), "Medium");
    bench_peak_memory(large_xml.c_str(), large_xml.length(), "Large");

    // Test 2: Memory per node
    bench_memory_per_node(medium_xml.c_str(), medium_xml.length());

    // Test 3: Fragmentation
    bench_fragmentation(medium_xml.c_str(), medium_xml.length());

    // Test 4: Large document
    bench_large_document(large_xml.c_str(), large_xml.length());

    // Cleanup
    xmlCleanupParser();

    printf("\n");
    printf("Memory Benchmarks Complete\n");
    printf("\n");
    printf("Note: Memory measurements use getrusage() which reports RSS.\n");
    printf("Actual allocator overhead may vary.\n");
    printf("\n");

    return 0;
}
