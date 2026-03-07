/**
 * DOM Modification Benchmarks
 *
 * Measures DOM modification performance.
 * Target: >= 1.0x vs pugixml (parity or better)
 *
 * Tests:
 * 1. Append child
 * 2. Prepend child
 * 3. Remove child
 * 4. Set attribute
 * 5. Remove attribute
 * 6. Set text content
 * 7. Clone element (append_copy)
 * 8. Serialize to string
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
#define ITERATIONS 10000
#define WARMUP_ITERS 1000
#endif

// Base XML for creating documents
static const char* BASE_XML =
    "<?xml version=\"1.0\"?>"
    "<root/>";

// ============================================================================
// Test 1: Append Child
// ============================================================================

static void bench_taurus_append_child(void) {
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(BASE_XML, strlen(BASE_XML), &status);
    if (!doc) return;

    TaurusElement root = taurus_document_root(doc);
    if (!elem_not_null(root)) {
        taurus_document_free(doc);
        return;
    }

    for (int i = 0; i < ITERATIONS; i++) {
        TaurusElement child = taurus_element_create(doc, "child");
        if (elem_not_null(child)) {
            taurus_element_append_child(root, child);
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_append_child(void) {
    pugi::xml_document doc;
    doc.load_string(BASE_XML);
    pugi::xml_node root = doc.root().first_child();

    for (int i = 0; i < ITERATIONS; i++) {
        pugi::xml_node child = root.append_child("child");
    }
}

// ============================================================================
// Test 2: Prepend Child
// ============================================================================

static void bench_taurus_prepend_child(void) {
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(BASE_XML, strlen(BASE_XML), &status);
    if (!doc) return;

    TaurusElement root = taurus_document_root(doc);
    if (!elem_not_null(root)) {
        taurus_document_free(doc);
        return;
    }

    for (int i = 0; i < ITERATIONS; i++) {
        TaurusElement child = taurus_element_create(doc, "child");
        if (elem_not_null(child)) {
            taurus_element_prepend_child(root, child);
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_prepend_child(void) {
    pugi::xml_document doc;
    doc.load_string(BASE_XML);
    pugi::xml_node root = doc.root().first_child();

    for (int i = 0; i < ITERATIONS; i++) {
        pugi::xml_node child = root.prepend_child("child");
    }
}

// ============================================================================
// Test 3: Remove Child
// ============================================================================

static void bench_taurus_remove_child(void) {
    for (int iter = 0; iter < 10; iter++) {
        TaurusStatus status;
        TaurusDocument doc = taurus_parse_string(BASE_XML, strlen(BASE_XML), &status);
        if (!doc) continue;

        TaurusElement root = taurus_document_root(doc);
        if (!elem_not_null(root)) {
            taurus_document_free(doc);
            continue;
        }

        // Pre-populate children
        for (int i = 0; i < ITERATIONS; i++) {
            TaurusElement child = taurus_element_create(doc, "child");
            if (elem_not_null(child)) {
                taurus_element_append_child(root, child);
            }
        }

        // Remove all children
        TaurusElement child = taurus_element_first_child_any(root);
        while (elem_not_null(child)) {
            TaurusElement next = taurus_element_next_sibling_any(child);
            taurus_element_remove_child(root, child);
            child = next;
        }

        taurus_document_free(doc);
    }
}

static void bench_pugixml_remove_child(void) {
    for (int iter = 0; iter < 10; iter++) {
        pugi::xml_document doc;
        doc.load_string(BASE_XML);
        pugi::xml_node root = doc.root().first_child();

        // Pre-populate children
        for (int i = 0; i < ITERATIONS; i++) {
            root.append_child("child");
        }

        // Remove all children
        while (root.first_child()) {
            root.remove_child(root.first_child());
        }
    }
}

// ============================================================================
// Test 4: Set Attribute
// ============================================================================

static void bench_taurus_set_attribute(void) {
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(BASE_XML, strlen(BASE_XML), &status);
    if (!doc) return;

    TaurusElement root = taurus_document_root(doc);
    if (!elem_not_null(root)) {
        taurus_document_free(doc);
        return;
    }

    for (int i = 0; i < ITERATIONS; i++) {
        char name[32], value[32];
        snprintf(name, sizeof(name), "attr%d", i % 100);
        snprintf(value, sizeof(value), "value%d", i);
        taurus_element_set_attribute(root, name, value);
    }

    taurus_document_free(doc);
}

static void bench_pugixml_set_attribute(void) {
    pugi::xml_document doc;
    doc.load_string(BASE_XML);
    pugi::xml_node root = doc.root().first_child();

    for (int i = 0; i < ITERATIONS; i++) {
        char name[32], value[32];
        snprintf(name, sizeof(name), "attr%d", i % 100);
        snprintf(value, sizeof(value), "value%d", i);
        root.append_attribute(name) = value;
    }
}

// ============================================================================
// Test 5: Remove Attribute
// ============================================================================

static void bench_taurus_remove_attribute(void) {
    for (int iter = 0; iter < 10; iter++) {
        TaurusStatus status;
        TaurusDocument doc = taurus_parse_string(BASE_XML, strlen(BASE_XML), &status);
        if (!doc) continue;

        TaurusElement root = taurus_document_root(doc);
        if (!elem_not_null(root)) {
            taurus_document_free(doc);
            continue;
        }

        // Pre-populate attributes
        for (int i = 0; i < 100; i++) {
            char name[32];
            snprintf(name, sizeof(name), "attr%d", i);
            taurus_element_set_attribute(root, name, "value");
        }

        // Remove all attributes
        for (int i = 0; i < 100; i++) {
            char name[32];
            snprintf(name, sizeof(name), "attr%d", i);
            taurus_element_remove_attribute(root, name);
        }

        taurus_document_free(doc);
    }
}

static void bench_pugixml_remove_attribute(void) {
    for (int iter = 0; iter < 10; iter++) {
        pugi::xml_document doc;
        doc.load_string(BASE_XML);
        pugi::xml_node root = doc.root().first_child();

        // Pre-populate attributes
        for (int i = 0; i < 100; i++) {
            char name[32];
            snprintf(name, sizeof(name), "attr%d", i);
            root.append_attribute(name) = "value";
        }

        // Remove all attributes
        for (int i = 0; i < 100; i++) {
            char name[32];
            snprintf(name, sizeof(name), "attr%d", i);
            root.remove_attribute(name);
        }
    }
}

// ============================================================================
// Test 6: Set Text Content
// ============================================================================

static void bench_taurus_set_text(void) {
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(BASE_XML, strlen(BASE_XML), &status);
    if (!doc) return;

    TaurusElement root = taurus_document_root(doc);
    if (!elem_not_null(root)) {
        taurus_document_free(doc);
        return;
    }

    for (int i = 0; i < ITERATIONS; i++) {
        char text[64];
        snprintf(text, sizeof(text), "Text content %d with some more text", i);
        taurus_element_set_text(root, text);
    }

    taurus_document_free(doc);
}

static void bench_pugixml_set_text(void) {
    pugi::xml_document doc;
    doc.load_string(BASE_XML);
    pugi::xml_node root = doc.root().first_child();

    for (int i = 0; i < ITERATIONS; i++) {
        char text[64];
        snprintf(text, sizeof(text), "Text content %d with some more text", i);
        root.text() = text;
    }
}

// ============================================================================
// Test 7: Clone Element (append_copy)
// ============================================================================

static void bench_taurus_clone(void) {
    // Create source document with element to clone
    const char* source_xml =
        "<?xml version=\"1.0\"?>"
        "<root><source attr0=\"v0\" attr1=\"v1\" attr2=\"v2\" attr3=\"v3\" "
        "attr4=\"v4\" attr5=\"v5\" attr6=\"v6\" attr7=\"v7\" attr8=\"v8\" attr9=\"v9\">"
        "<child1/><child2/><child3/><child4/><child5/>"
        "</source><target/></root>";

    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(source_xml, strlen(source_xml), &status);
    if (!doc) return;

    TaurusElement root = taurus_document_root(doc);
    if (!elem_not_null(root)) {
        taurus_document_free(doc);
        return;
    }

    // Get source element to clone
    TaurusElement source = taurus_element_first_child(root, "source");
    TaurusElement target = taurus_element_first_child(root, "target");

    if (!elem_not_null(source) || !elem_not_null(target)) {
        taurus_document_free(doc);
        return;
    }

    // Clone many times
    for (int i = 0; i < ITERATIONS; i++) {
        TaurusElement clone = taurus_element_append_copy(target, source);
        (void)clone;
    }

    taurus_document_free(doc);
}

static void bench_pugixml_clone(void) {
    // Create source document with element to clone
    const char* source_xml =
        "<?xml version=\"1.0\"?>"
        "<root><source attr0=\"v0\" attr1=\"v1\" attr2=\"v2\" attr3=\"v3\" "
        "attr4=\"v4\" attr5=\"v5\" attr6=\"v6\" attr7=\"v7\" attr8=\"v8\" attr9=\"v9\">"
        "<child1/><child2/><child3/><child4/><child5/>"
        "</source><target/></root>";

    pugi::xml_document doc;
    doc.load_string(source_xml);
    pugi::xml_node root = doc.root().first_child();

    // Get source element to clone
    pugi::xml_node source = root.child("source");
    pugi::xml_node target = root.child("target");

    if (!source || !target) return;

    // Clone many times
    for (int i = 0; i < ITERATIONS; i++) {
        pugi::xml_node clone = target.append_copy(source);
        (void)clone;
    }
}

// ============================================================================
// Test 8: Serialize to String
// ============================================================================

static void bench_taurus_serialize(void) {
    // Create document with content
    const char* template_xml =
        "<?xml version=\"1.0\"?>"
        "<root>"
        "<element id=\"id0\">Text 0</element>"
        "<element id=\"id1\">Text 1</element>"
        "<element id=\"id2\">Text 2</element>"
        "<element id=\"id3\">Text 3</element>"
        "<element id=\"id4\">Text 4</element>"
        "</root>";

    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(template_xml, strlen(template_xml), &status);
    if (!doc) return;

    // Serialize many times
    char* buffer = NULL;
    for (int i = 0; i < ITERATIONS / 10; i++) {
        buffer = taurus_document_serialize(doc, NULL);
        if (buffer) {
            taurus_free_string(buffer);
            buffer = NULL;
        }
    }

    taurus_document_free(doc);
}

static void bench_pugixml_serialize(void) {
    // Create document with content
    const char* template_xml =
        "<?xml version=\"1.0\"?>"
        "<root>"
        "<element id=\"id0\">Text 0</element>"
        "<element id=\"id1\">Text 1</element>"
        "<element id=\"id2\">Text 2</element>"
        "<element id=\"id3\">Text 3</element>"
        "<element id=\"id4\">Text 4</element>"
        "</root>";

    pugi::xml_document doc;
    doc.load_string(template_xml);

    // Serialize many times using stringstream
    for (int i = 0; i < ITERATIONS / 10; i++) {
        std::ostringstream oss;
        doc.save(oss, "", 0);
    }
}

// ============================================================================
// Benchmark Runner
// ============================================================================

typedef void (*bench_func_void_t)(void);

static void run_mod_benchmark(const char* name,
                              bench_func_void_t taurus_fn,
                              bench_func_void_t pugixml_fn) {
    printf("\n=== %s ===\n", name);

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        taurus_fn();
        pugixml_fn();
    }

    // Measure Taurus
    std::vector<double> taurus_times;
    for (int i = 0; i < 10; i++) {
        long start = benchmark_time_us();
        taurus_fn();
        long end = benchmark_time_us();
        taurus_times.push_back((double)(end - start));
    }
    benchmark_stats taurus_stats = benchmark_analyze(taurus_times.data(), 10);

    // Measure pugixml
    std::vector<double> pugixml_times;
    for (int i = 0; i < 10; i++) {
        long start = benchmark_time_us();
        pugixml_fn();
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
    (void)argc;
    (void)argv;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║          DOM Modification Benchmarks (8 tests)            ║\n");
    printf("║  Target: >= 1.0x vs pugixml (parity or better)            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    run_mod_benchmark("Append Child", bench_taurus_append_child, bench_pugixml_append_child);
    run_mod_benchmark("Prepend Child", bench_taurus_prepend_child, bench_pugixml_prepend_child);
    run_mod_benchmark("Remove Child", bench_taurus_remove_child, bench_pugixml_remove_child);
    run_mod_benchmark("Set Attribute", bench_taurus_set_attribute, bench_pugixml_set_attribute);
    run_mod_benchmark("Remove Attribute", bench_taurus_remove_attribute, bench_pugixml_remove_attribute);
    run_mod_benchmark("Set Text Content", bench_taurus_set_text, bench_pugixml_set_text);
    run_mod_benchmark("Clone Element", bench_taurus_clone, bench_pugixml_clone);
    run_mod_benchmark("Serialize to String", bench_taurus_serialize, bench_pugixml_serialize);

    printf("\n");
    printf("DOM Modification Benchmarks Complete\n");
    printf("\n");

    return 0;
}
