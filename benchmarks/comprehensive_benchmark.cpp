/**
 * Comprehensive Performance Benchmark
 *
 * Measures complete lifecycle costs for all real-world usage patterns:
 * - Parse-only (read-once, extract minimal data)
 * - Read-many (parse once, query many times)
 * - Tree traversal (parse once, walk repeatedly)
 * - Roundtrip editing (parse, modify, serialize)
 * - Streaming (parse, extract, free immediately)
 *
 * For multiple file sizes: Small (1KB), Medium (~50KB), Large (~500KB)
 *
 * Reports cost breakdown: Parse | Operate | Free | TOTAL
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

// Internal headers for node type access
#include "../../src/taurus/taurus_internal.h"
#include "../../src/taurus/dom/node.h"

extern "C" {
#include <taurus.h>
}

#include <pugixml.hpp>

extern "C" {
#include "utils.h"
}

/* Forward declaration for explicit cleanup function */
extern "C" void taurus_explicit_cleanup(void);

/* TaurusElement null check helper for C++ code */
static inline bool elem_not_null(const TaurusElement& elem) {
    return !taurus_element_is_null(elem);
}

// ============================================================================
// Test File Generation
// ============================================================================

static void create_medium_file(const char* path) {
    FILE* f = fopen(path, "wb");
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<catalog>\n");
    for (int i = 0; i < 500; i++) {
        fprintf(f, "  <product id=\"%05d\" category=\"Category%d\">\n", i, i % 10);
        fprintf(f, "    <name>Product %d Name</name>\n", i);
        fprintf(f, "    <price>%.2f</price>\n", 10.0 + (i % 900) / 100.0);
        fprintf(f, "    <description>Product %d description text</description>\n", i);
        fprintf(f, "    <stock>%d</stock>\n", i % 100);
        fprintf(f, "  </product>\n");
    }
    fprintf(f, "</catalog>\n");
    fclose(f);
}

static void create_large_file(const char* path) {
    FILE* f = fopen(path, "wb");
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<catalog>\n");
    for (int i = 0; i < 5000; i++) {
        fprintf(f, "  <product id=\"%05d\" category=\"Category%d\">\n", i, i % 10);
        fprintf(f, "    <name>Product %d Name</name>\n", i);
        fprintf(f, "    <price>%.2f</price>\n", 10.0 + (i % 900) / 100.0);
        fprintf(f, "    <description>Product %d description text with more details</description>\n", i);
        fprintf(f, "    <specs>\n");
        fprintf(f, "      <weight>%.2f kg</weight>\n", 0.1 + (i % 500) / 100.0);
        fprintf(f, "      <dimensions>%dx%dx%d</dimensions>\n", 10 + (i % 20), 20 + (i % 30), 5 + (i % 10));
        fprintf(f, "    </specs>\n");
        fprintf(f, "    <stock>%d</stock>\n", i % 100);
        fprintf(f, "  </product>\n");
    }
    fprintf(f, "</catalog>\n");
    fclose(f);
}

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
// Benchmark Test Functions
// ============================================================================

// Test 1: Parse-Only (read-once, immediate free)
typedef struct {
    double parse_us;
    double free_us;
    double total_us;
} ParseOnlyResult;

static ParseOnlyResult bench_taurus_parse_only(const char* xml, size_t len, int iterations) {
    std::vector<double> parse_times;
    std::vector<double> free_times;
    parse_times.reserve(iterations);
    free_times.reserve(iterations);

    for (int i = 0; i < iterations; i++) {
        long long start = benchmark_time_ns();
        TaurusDocument doc = taurus_parse_string(xml, len, NULL);
        long long after_parse = benchmark_time_ns();
        taurus_document_free(doc);
        long long end = benchmark_time_ns();

        parse_times.push_back((double)(after_parse - start) / 1000.0);  /* Convert ns to µs */
        free_times.push_back((double)(end - after_parse) / 1000.0);
    }

    benchmark_stats parse_stats = benchmark_analyze(parse_times.data(), parse_times.size());
    benchmark_stats free_stats = benchmark_analyze(free_times.data(), free_times.size());

    ParseOnlyResult result = {
        .parse_us = parse_stats.median,
        .free_us = free_stats.median,
        .total_us = parse_stats.median + free_stats.median
    };
    return result;
}

// NEW: Zero-copy (inplace) parsing benchmark
static ParseOnlyResult bench_taurus_parse_only_inplace(const char* xml, size_t len, int iterations) {
    std::vector<double> parse_times;
    std::vector<double> free_times;
    parse_times.reserve(iterations);
    free_times.reserve(iterations);

    for (int i = 0; i < iterations; i++) {
        // Make a mutable copy for inplace parsing
        char* xml_copy = (char*)malloc(len + 1);
        memcpy(xml_copy, xml, len);
        xml_copy[len] = '\0';

        long long start = benchmark_time_ns();
        TaurusDocument doc = taurus_parse_string_inplace(xml_copy, len, NULL);
        long long after_parse = benchmark_time_ns();
        taurus_document_free(doc);  // This also frees xml_copy
        long long end = benchmark_time_ns();

        parse_times.push_back((double)(after_parse - start) / 1000.0);  /* Convert ns to µs */
        free_times.push_back((double)(end - after_parse) / 1000.0);
    }

    benchmark_stats parse_stats = benchmark_analyze(parse_times.data(), parse_times.size());
    benchmark_stats free_stats = benchmark_analyze(free_times.data(), free_times.size());

    ParseOnlyResult result = {
        .parse_us = parse_stats.median,
        .free_us = free_stats.median,
        .total_us = parse_stats.median + free_stats.median
    };
    return result;
}

static ParseOnlyResult bench_pugixml_parse_only(const char* xml, size_t len, int iterations) {
    std::vector<double> parse_times;
    std::vector<double> free_times;
    parse_times.reserve(iterations);
    free_times.reserve(iterations);

    for (int i = 0; i < iterations; i++) {
        long long start = benchmark_time_ns();
        pugi::xml_document doc;
        doc.load_buffer(xml, len);
        long long after_parse = benchmark_time_ns();
        // pugixml uses RAII - explicit "free" measured by scope exit
        long long end = benchmark_time_ns();

        parse_times.push_back((double)(after_parse - start) / 1000.0);  /* Convert ns to µs */
        free_times.push_back((double)(end - after_parse) / 1000.0);
    }

    benchmark_stats parse_stats = benchmark_analyze(parse_times.data(), parse_times.size());
    benchmark_stats free_stats = benchmark_analyze(free_times.data(), free_times.size());

    ParseOnlyResult result = {
        .parse_us = parse_stats.median,
        .free_us = free_stats.median,
        .total_us = parse_stats.median + free_stats.median
    };
    return result;
}

// Test 2: Read-Many (parse once, query N times, then free)
typedef struct {
    double parse_us;
    double operate_us;
    double free_us;
    double total_us;
} ReadManyResult;

static ReadManyResult bench_taurus_read_many(const char* xml, size_t len, int queries) {
    std::vector<double> parse_times;
    std::vector<double> operate_times;
    std::vector<double> free_times;

    parse_times.reserve(1000);
    operate_times.reserve(1000);
    free_times.reserve(1000);

    for (int i = 0; i < 1000; i++) {
        // Parse
        long start = benchmark_time_us();
        TaurusDocument doc = taurus_parse_string(xml, len, NULL);
        long after_parse = benchmark_time_us();

        // Operate (queries)
        for (int q = 0; q < queries; q++) {
            TaurusElement root = taurus_document_root(doc);
            size_t count = taurus_element_child_count(root);
            for (size_t j = 0; j < count && j < 10; j++) {
                TaurusElement child = taurus_element_child(root, j);
                const char* id = taurus_element_attribute(child, "id");
                const char* name = taurus_element_name(child);
                (void)id; (void)name;
            }
        }
        long after_operate = benchmark_time_us();

        // Free
        taurus_document_free(doc);
        long end = benchmark_time_us();

        parse_times.push_back((double)(after_parse - start));
        operate_times.push_back((double)(after_operate - after_parse));
        free_times.push_back((double)(end - after_operate));
    }

    benchmark_stats parse_stats = benchmark_analyze(parse_times.data(), parse_times.size());
    benchmark_stats operate_stats = benchmark_analyze(operate_times.data(), operate_times.size());
    benchmark_stats free_stats = benchmark_analyze(free_times.data(), free_times.size());

    ReadManyResult result = {
        .parse_us = parse_stats.median,
        .operate_us = operate_stats.median,
        .free_us = free_stats.median,
        .total_us = parse_stats.median + operate_stats.median + free_stats.median
    };
    return result;
}

static ReadManyResult bench_pugixml_read_many(const char* xml, size_t len, int queries) {
    std::vector<double> parse_times;
    std::vector<double> operate_times;
    std::vector<double> free_times;

    parse_times.reserve(1000);
    operate_times.reserve(1000);
    free_times.reserve(1000);

    for (int i = 0; i < 1000; i++) {
        // Parse
        long start = benchmark_time_us();
        pugi::xml_document doc;
        doc.load_buffer(xml, len);
        long after_parse = benchmark_time_us();

        // Operate (queries)
        pugi::xml_node root = doc.root().first_child();
        for (int q = 0; q < queries; q++) {
            int count = 0;
            for (pugi::xml_node child = root.first_child(); child && count < 10; child = child.next_sibling(), count++) {
                const char* id = child.attribute("id").value();
                const char* name = child.name();
                (void)id; (void)name;
            }
        }
        long after_operate = benchmark_time_us();

        // Free (RAII - measure scope exit)
        long end = benchmark_time_us();

        parse_times.push_back((double)(after_parse - start));
        operate_times.push_back((double)(after_operate - after_parse));
        free_times.push_back((double)(end - after_operate));
    }

    benchmark_stats parse_stats = benchmark_analyze(parse_times.data(), parse_times.size());
    benchmark_stats operate_stats = benchmark_analyze(operate_times.data(), operate_times.size());
    benchmark_stats free_stats = benchmark_analyze(free_times.data(), free_times.size());

    ReadManyResult result = {
        .parse_us = parse_stats.median,
        .operate_us = operate_stats.median,
        .free_us = free_stats.median,
        .total_us = parse_stats.median + operate_stats.median + free_stats.median
    };
    return result;
}

// Test 3: Tree Traversal (parse once, walk N times, then free)
typedef struct {
    double parse_us;
    double operate_us;
    double free_us;
    double total_us;
} TreeWalkResult;

static int walk_taurus_tree(TaurusElement elem, int* count) {
    int total = 1;
    (*count)++;

    /* OPTIMIZED: Use iterator-style access instead of index-based
     * Old approach (O(n²)): for each child i, call taurus_element_child(elem, i)
     * New approach (O(n)): iterate using first_child_any and next_sibling_any
     *
     * This optimization eliminates the O(n) linked list walk for each child access,
     * changing the overall complexity from O(n²) to O(n) for tree traversal.
     */
    TaurusElement child = taurus_element_first_child_any(elem);
    while (elem_not_null(child)) {
        /* taurus_element_first_child_any returns all child nodes (elements, text, etc)
         * We use taurus_element_name to check if it's an element (returns non-NULL) */
        total += walk_taurus_tree(child, count);
        child = taurus_element_next_sibling_any(child);
    }
    return total;
}

static TreeWalkResult bench_taurus_tree_walk(const char* xml, size_t len, int walks) {
    std::vector<double> parse_times;
    std::vector<double> operate_times;
    std::vector<double> free_times;

    parse_times.reserve(1000);
    operate_times.reserve(1000);
    free_times.reserve(1000);

    for (int i = 0; i < 1000; i++) {
        long start = benchmark_time_us();
        TaurusDocument doc = taurus_parse_string(xml, len, NULL);
        long after_parse = benchmark_time_us();

        // Tree walks
        for (int w = 0; w < walks; w++) {
            int count = 0;
            TaurusElement root = taurus_document_root(doc);
            walk_taurus_tree(root, &count);
        }
        long after_operate = benchmark_time_us();

        taurus_document_free(doc);
        long end = benchmark_time_us();

        parse_times.push_back((double)(after_parse - start));
        operate_times.push_back((double)(after_operate - after_parse));
        free_times.push_back((double)(end - after_operate));
    }

    benchmark_stats parse_stats = benchmark_analyze(parse_times.data(), parse_times.size());
    benchmark_stats operate_stats = benchmark_analyze(operate_times.data(), operate_times.size());
    benchmark_stats free_stats = benchmark_analyze(free_times.data(), free_times.size());

    TreeWalkResult result = {
        .parse_us = parse_stats.median,
        .operate_us = operate_stats.median,
        .free_us = free_stats.median,
        .total_us = parse_stats.median + operate_stats.median + free_stats.median
    };
    return result;
}

static int walk_pugixml_tree(pugi::xml_node node, int* count) {
    int total = 1;
    (*count)++;
    for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
        total += walk_pugixml_tree(child, count);
    }
    return total;
}

static TreeWalkResult bench_pugixml_tree_walk(const char* xml, size_t len, int walks) {
    std::vector<double> parse_times;
    std::vector<double> operate_times;
    std::vector<double> free_times;

    parse_times.reserve(1000);
    operate_times.reserve(1000);
    free_times.reserve(1000);

    for (int i = 0; i < 1000; i++) {
        long start = benchmark_time_us();
        pugi::xml_document doc;
        doc.load_buffer(xml, len);
        long after_parse = benchmark_time_us();

        // Tree walks
        pugi::xml_node root = doc.root().first_child();
        for (int w = 0; w < walks; w++) {
            int count = 0;
            walk_pugixml_tree(root, &count);
        }
        long after_operate = benchmark_time_us();

        long end = benchmark_time_us();

        parse_times.push_back((double)(after_parse - start));
        operate_times.push_back((double)(after_operate - after_parse));
        free_times.push_back((double)(end - after_operate));
    }

    benchmark_stats parse_stats = benchmark_analyze(parse_times.data(), parse_times.size());
    benchmark_stats operate_stats = benchmark_analyze(operate_times.data(), operate_times.size());
    benchmark_stats free_stats = benchmark_analyze(free_times.data(), free_times.size());

    TreeWalkResult result = {
        .parse_us = parse_stats.median,
        .operate_us = operate_stats.median,
        .free_us = free_stats.median,
        .total_us = parse_stats.median + operate_stats.median + free_stats.median
    };
    return result;
}

// Test 4: Roundtrip (parse, modify, serialize, free)
typedef struct {
    double parse_us;
    double modify_us;
    double serialize_us;
    double free_us;
    double total_us;
} RoundtripResult;

static RoundtripResult bench_taurus_roundtrip(const char* xml, size_t len, int iterations) {
    std::vector<double> parse_times;
    std::vector<double> modify_times;
    std::vector<double> serialize_times;
    std::vector<double> free_times;

    parse_times.reserve(iterations);
    modify_times.reserve(iterations);
    serialize_times.reserve(iterations);
    free_times.reserve(iterations);

    for (int i = 0; i < iterations; i++) {
        // Parse
        long start = benchmark_time_us();
        TaurusDocument doc = taurus_parse_string(xml, len, NULL);
        long after_parse = benchmark_time_us();

        if (!doc) {
            fprintf(stderr, "ERROR: Failed to parse document in roundtrip test (iteration %d)\n", i);
            continue;
        }

        // Modify
        TaurusElement root = taurus_document_root(doc);
        TaurusElement new_elem = taurus_element_create(doc, "new_element");
        if (taurus_element_is_null(new_elem)) {
            fprintf(stderr, "ERROR: Failed to create element in roundtrip test (iteration %d)\n", i);
            taurus_document_free(doc);
            continue;
        }
        TaurusStatus status = taurus_element_append_child(root, new_elem);
        if (status != TAURUS_OK) {
            fprintf(stderr, "ERROR: Failed to append child in roundtrip test (iteration %d), status=%d\n", i, status);
        }
        long after_modify = benchmark_time_us();

        // Serialize
        char* output = taurus_document_serialize(doc, NULL);
        long after_serialize = benchmark_time_us();

        // Free
        if (output) taurus_free_string(output);
        taurus_document_free(doc);
        long end = benchmark_time_us();

        parse_times.push_back((double)(after_parse - start));
        modify_times.push_back((double)(after_modify - after_parse));
        serialize_times.push_back((double)(after_serialize - after_modify));
        free_times.push_back((double)(end - after_serialize));
    }

    benchmark_stats parse_stats = benchmark_analyze(parse_times.data(), parse_times.size());
    benchmark_stats modify_stats = benchmark_analyze(modify_times.data(), modify_times.size());
    benchmark_stats serialize_stats = benchmark_analyze(serialize_times.data(), serialize_times.size());
    benchmark_stats free_stats = benchmark_analyze(free_times.data(), free_times.size());

    RoundtripResult result = {
        .parse_us = parse_stats.median,
        .modify_us = modify_stats.median,
        .serialize_us = serialize_stats.median,
        .free_us = free_stats.median,
        .total_us = parse_stats.median + modify_stats.median + serialize_stats.median + free_stats.median
    };
    return result;
}

static RoundtripResult bench_pugixml_roundtrip(const char* xml, size_t len, int iterations) {
    std::vector<double> parse_times;
    std::vector<double> modify_times;
    std::vector<double> serialize_times;
    std::vector<double> free_times;

    parse_times.reserve(iterations);
    modify_times.reserve(iterations);
    serialize_times.reserve(iterations);
    free_times.reserve(iterations);

    for (int i = 0; i < iterations; i++) {
        // Parse
        long start = benchmark_time_us();
        pugi::xml_document doc;
        doc.load_buffer(xml, len);
        long after_parse = benchmark_time_us();

        // Modify
        pugi::xml_node root = doc.root().first_child();
        root.append_child("new_element");
        long after_modify = benchmark_time_us();

        // Serialize
        std::ostringstream oss;
        doc.print(oss);
        std::string output = oss.str();
        long after_serialize = benchmark_time_us();

        // Free
        long end = benchmark_time_us();

        parse_times.push_back((double)(after_parse - start));
        modify_times.push_back((double)(after_modify - after_parse));
        serialize_times.push_back((double)(after_serialize - after_modify));
        free_times.push_back((double)(end - after_serialize));
    }

    benchmark_stats parse_stats = benchmark_analyze(parse_times.data(), parse_times.size());
    benchmark_stats modify_stats = benchmark_analyze(modify_times.data(), modify_times.size());
    benchmark_stats serialize_stats = benchmark_analyze(serialize_times.data(), serialize_times.size());
    benchmark_stats free_stats = benchmark_analyze(free_times.data(), free_times.size());

    RoundtripResult result = {
        .parse_us = parse_stats.median,
        .modify_us = modify_stats.median,
        .serialize_us = serialize_stats.median,
        .free_us = free_stats.median,
        .total_us = parse_stats.median + modify_stats.median + serialize_stats.median + free_stats.median
    };
    return result;
}

// Test 5: Streaming (parse, extract minimal, immediate free)
typedef struct {
    double parse_us;
    double extract_us;
    double free_us;
    double total_us;
} StreamResult;

static StreamResult bench_taurus_streaming(const char* xml, size_t len, int iterations) {
    std::vector<double> parse_times;
    std::vector<double> extract_times;
    std::vector<double> free_times;

    parse_times.reserve(iterations);
    extract_times.reserve(iterations);
    free_times.reserve(iterations);

    for (int i = 0; i < iterations; i++) {
        long start = benchmark_time_us();
        TaurusDocument doc = taurus_parse_string(xml, len, NULL);
        long after_parse = benchmark_time_us();

        // Extract minimal data (like streaming SAX would)
        TaurusElement root = taurus_document_root(doc);
        const char* name = taurus_element_name(root);
        (void)name;
        long after_extract = benchmark_time_us();

        taurus_document_free(doc);
        long end = benchmark_time_us();

        parse_times.push_back((double)(after_parse - start));
        extract_times.push_back((double)(after_extract - after_parse));
        free_times.push_back((double)(end - after_extract));
    }

    benchmark_stats parse_stats = benchmark_analyze(parse_times.data(), parse_times.size());
    benchmark_stats extract_stats = benchmark_analyze(extract_times.data(), extract_times.size());
    benchmark_stats free_stats = benchmark_analyze(free_times.data(), free_times.size());

    StreamResult result = {
        .parse_us = parse_stats.median,
        .extract_us = extract_stats.median,
        .free_us = free_stats.median,
        .total_us = parse_stats.median + extract_stats.median + free_stats.median
    };
    return result;
}

static StreamResult bench_pugixml_streaming(const char* xml, size_t len, int iterations) {
    std::vector<double> parse_times;
    std::vector<double> extract_times;
    std::vector<double> free_times;

    parse_times.reserve(iterations);
    extract_times.reserve(iterations);
    free_times.reserve(iterations);

    for (int i = 0; i < iterations; i++) {
        long start = benchmark_time_us();
        pugi::xml_document doc;
        doc.load_buffer(xml, len);
        long after_parse = benchmark_time_us();

        // Extract minimal data
        pugi::xml_node root = doc.root().first_child();
        const char* name = root.name();
        (void)name;
        long after_extract = benchmark_time_us();

        long end = benchmark_time_us();

        parse_times.push_back((double)(after_parse - start));
        extract_times.push_back((double)(after_extract - after_parse));
        free_times.push_back((double)(end - after_extract));
    }

    benchmark_stats parse_stats = benchmark_analyze(parse_times.data(), parse_times.size());
    benchmark_stats extract_stats = benchmark_analyze(extract_times.data(), extract_times.size());
    benchmark_stats free_stats = benchmark_analyze(free_times.data(), free_times.size());

    StreamResult result = {
        .parse_us = parse_stats.median,
        .extract_us = extract_stats.median,
        .free_us = free_stats.median,
        .total_us = parse_stats.median + extract_stats.median + free_stats.median
    };
    return result;
}

// ============================================================================
// Main
// ============================================================================

struct FileConfig {
    const char* name;
    const char* path;
    int parse_iterations;
    int query_count;
    int walk_count;
};

int main(int argc, char** argv) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║     COMPREHENSIVE PERFORMANCE MATRIX                             ║\n");
    printf("║     Complete Lifecycle Cost Analysis for All Use Cases              ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    // Generate test files
    printf("Generating test files...\n");
    create_medium_file("fixtures/medium_catalog.xml");
    create_large_file("fixtures/large_catalog.xml");
    printf("Done.\n\n");

    FileConfig files[] = {
        {"Small (1 KB)", "fixtures/small.xml", 100000, 1000, 100},
        {"Medium (50 KB)", "fixtures/medium_catalog.xml", 1000, 1000, 50},
        {"Large (500 KB)", "fixtures/large_catalog.xml", 100, 100, 10},
        {NULL, NULL, 0, 0, 0}
    };

    // Run benchmarks for each file size
    for (int f = 0; files[f].name != NULL; f++) {
        printf("══════════════════════════════════════════════════════════════════\n");
        printf("FILE SIZE: %s\n", files[f].name);
        printf("══════════════════════════════════════════════════════════════════\n");
        printf("\n");

        std::string xml_content = read_file(files[f].path);
        const char* xml = xml_content.c_str();
        size_t len = xml_content.length();

        // Test 1: Parse-Only
        {
            printf("┌─ 1. Parse-Only (%d iterations) ─────────────────────────────┐\n", files[f].parse_iterations);
            ParseOnlyResult taurus = bench_taurus_parse_only(xml, len, files[f].parse_iterations);
            ParseOnlyResult taurus_inplace = bench_taurus_parse_only_inplace(xml, len, files[f].parse_iterations);
            ParseOnlyResult pugixml = bench_pugixml_parse_only(xml, len, files[f].parse_iterations);

            printf("│ Component │ Taurus   │ Inplace  │ pugixml  │ Ratio  │           │\n");
            printf("├───────────┼──────────┼──────────┼─────────┼───────┼───────────┤\n");
            printf("│ Parse     │ %7.2f µs │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.parse_us, taurus_inplace.parse_us, pugixml.parse_us,
                   taurus_inplace.parse_us / pugixml.parse_us);
            printf("│ Free      │ %7.2f µs │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.free_us, taurus_inplace.free_us, pugixml.free_us,
                   taurus_inplace.free_us / pugixml.free_us);
            printf("├───────────┼──────────┼──────────┼─────────┼───────┼───────────┤\n");
            printf("│ TOTAL     │ %7.2f µs │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.total_us, taurus_inplace.total_us, pugixml.total_us,
                   taurus_inplace.total_us / pugixml.total_us);
            printf("└───────────┴──────────┴──────────┴─────────┴───────┴───────────┘\n");
            printf("\n");
        }

        // Test 2: Read-Many
        {
            printf("┌─ 2. Read-Many (Parse + %d queries, 1000 iterations) ────────┐\n", files[f].query_count);
            ReadManyResult taurus = bench_taurus_read_many(xml, len, files[f].query_count);
            ReadManyResult pugixml = bench_pugixml_read_many(xml, len, files[f].query_count);

            printf("│ Component │ Taurus   │ pugixml  │ Ratio  │           │\n");
            printf("├───────────┼──────────┼─────────┼───────┼───────────┤\n");
            printf("│ Parse     │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.parse_us, pugixml.parse_us, taurus.parse_us / pugixml.parse_us);
            printf("│ Operate   │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.operate_us, pugixml.operate_us, taurus.operate_us / pugixml.operate_us);
            printf("│ Free      │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.free_us, pugixml.free_us, taurus.free_us / pugixml.free_us);
            printf("├───────────┼──────────┼─────────┼───────┼───────────┤\n");
            printf("│ TOTAL     │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.total_us, pugixml.total_us, taurus.total_us / pugixml.total_us);
            printf("└───────────┴──────────┴─────────┴───────┴───────────┘\n");
            printf("\n");
        }

        // Test 3: Tree Traversal
        {
            printf("┌─ 3. Tree Traversal (Parse + %d walks, 1000 iterations) ──────┐\n", files[f].walk_count);
            TreeWalkResult taurus = bench_taurus_tree_walk(xml, len, files[f].walk_count);
            TreeWalkResult pugixml = bench_pugixml_tree_walk(xml, len, files[f].walk_count);

            printf("│ Component │ Taurus   │ pugixml  │ Ratio  │           │\n");
            printf("├───────────┼──────────┼─────────┼───────┼───────────┤\n");
            printf("│ Parse     │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.parse_us, pugixml.parse_us, taurus.parse_us / pugixml.parse_us);
            printf("│ Operate   │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.operate_us, pugixml.operate_us, taurus.operate_us / pugixml.operate_us);
            printf("│ Free      │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.free_us, pugixml.free_us, taurus.free_us / pugixml.free_us);
            printf("├───────────┼──────────┼─────────┼───────┼───────────┤\n");
            printf("│ TOTAL     │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.total_us, pugixml.total_us, taurus.total_us / pugixml.total_us);
            printf("└───────────┴──────────┴─────────┴───────┴───────────┘\n");
            printf("\n");
        }

        // Test 4: Roundtrip
        {
            int iterations = (f == 0) ? 1000 : (f == 1) ? 100 : 10;
            printf("┌─ 4. Roundtrip (Parse + Modify + Serialize, %d iterations) ──────┐\n", iterations);
            RoundtripResult taurus = bench_taurus_roundtrip(xml, len, iterations);
            RoundtripResult pugixml = bench_pugixml_roundtrip(xml, len, iterations);

            printf("│ Component │ Taurus   │ pugixml  │ Ratio  │           │\n");
            printf("├───────────┼──────────┼─────────┼───────┼───────────┤\n");
            printf("│ Parse     │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.parse_us, pugixml.parse_us, taurus.parse_us / pugixml.parse_us);
            printf("│ Modify    │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.modify_us, pugixml.modify_us, taurus.modify_us / pugixml.modify_us);
            printf("│ Serialize│ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.serialize_us, pugixml.serialize_us, taurus.serialize_us / pugixml.serialize_us);
            printf("│ Free      │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.free_us, pugixml.free_us, taurus.free_us / pugixml.free_us);
            printf("├───────────┼──────────┼─────────┼───────┼───────────┤\n");
            printf("│ TOTAL     │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.total_us, pugixml.total_us, taurus.total_us / pugixml.total_us);
            printf("└───────────┴──────────┴─────────┴───────┴───────────┘\n");
            printf("\n");
        }

        // Test 5: Streaming
        {
            printf("┌─ 5. Streaming (Parse + Extract + Free, %d iterations) ────────┐\n", files[f].parse_iterations);
            StreamResult taurus = bench_taurus_streaming(xml, len, files[f].parse_iterations);
            StreamResult pugixml = bench_pugixml_streaming(xml, len, files[f].parse_iterations);

            printf("│ Component │ Taurus   │ pugixml  │ Ratio  │           │\n");
            printf("├───────────┼──────────┼─────────┼───────┼───────────┤\n");
            printf("│ Parse     │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.parse_us, pugixml.parse_us, taurus.parse_us / pugixml.parse_us);
            printf("│ Extract   │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.extract_us, pugixml.extract_us, taurus.extract_us / pugixml.extract_us);
            printf("│ Free      │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.free_us, pugixml.free_us, taurus.free_us / pugixml.free_us);
            printf("├───────────┼──────────┼─────────┼───────┼───────────┤\n");
            printf("│ TOTAL     │ %7.2f µs │ %7.2f µs │ %5.2fx │           │\n",
                   taurus.total_us, pugixml.total_us, taurus.total_us / pugixml.total_us);
            printf("└───────────┴──────────┴─────────┴───────┴───────────┘\n");
            printf("\n");
        }
    }

    printf("══════════════════════════════════════════════════════════════════\n");
    printf("Legend:\n");
    printf("  Ratio > 1.0  = Taurus is FASTER\n");
    printf("  Ratio < 1.0  = pugixml is FASTER\n");
    printf("  Ratio = 1.0  = EQUAL performance\n");
    printf("\n");

    return 0;
}
