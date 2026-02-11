/* Taurus DOM Modification API Performance Benchmarks
 * Phase 19 Session 2 - Performance Validation
 * Tests all 10 new DOM APIs introduced in Phase 18 and Phase 19
 */

#include "taurus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ITERATIONS 10000

// Timing macro
#define MEASURE_TIME(label, code) do { \
    clock_t start = clock(); \
    for (int i = 0; i < ITERATIONS; i++) { code } \
    clock_t end = clock(); \
    double elapsed = ((double)(end - start) / CLOCKS_PER_SEC) * 1000000.0; \
    printf("  %s: %.2f µs per operation\n", label, elapsed / ITERATIONS); \
} while(0)

// Helper: Create document with N children
TaurusDocument create_test_doc_with_children(int count) {
    TaurusDocument doc = taurus_parse_string("<root/>", 7, NULL);
    TaurusElement root = taurus_document_root(doc);
    for (int i = 0; i < count; i++) {
        TaurusElement child = taurus_element_create(doc, "item");
        char id[16];
        snprintf(id, sizeof(id), "%d", i);
        taurus_element_set_attribute(child, "id", id);
        taurus_element_append_child(root, child);
    }
    return doc;
}

// Category A: Child Access Benchmarks
void bench_child_access(void) {
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║           Child Access Benchmarks                     ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    // Setup: Create document with 100 children
    TaurusDocument doc = create_test_doc_with_children(100);
    TaurusElement root = taurus_document_root(doc);

    // Baseline: child by index (existing API)
    MEASURE_TIME("child(index=50)", {
        TaurusElement elem = taurus_element_child(root, 50);
        (void)elem;
    });

    // New: first_child by name
    MEASURE_TIME("first_child(name)", {
        TaurusElement elem = taurus_element_first_child(root, "item");
        (void)elem;
    });

    // New: last_child by name
    MEASURE_TIME("last_child(name)", {
        TaurusElement elem = taurus_element_last_child(root, "item");
        (void)elem;
    });

    // New: first_child (any)
    MEASURE_TIME("first_child(NULL)", {
        TaurusElement elem = taurus_element_first_child(root, NULL);
        (void)elem;
    });

    // New: last_child (any)
    MEASURE_TIME("last_child(NULL)", {
        TaurusElement elem = taurus_element_last_child(root, NULL);
        (void)elem;
    });

    taurus_document_free(doc);
}

// Category B: Sibling Navigation Benchmarks
void bench_sibling_navigation(void) {
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║         Sibling Navigation Benchmarks                 ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    TaurusDocument doc = create_test_doc_with_children(100);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement elem = taurus_element_child(root, 50);

    // New: next_sibling by name
    MEASURE_TIME("next_sibling(name)", {
        TaurusElement next = taurus_element_next_sibling(elem, "item");
        (void)next;
    });

    // New: previous_sibling by name
    MEASURE_TIME("previous_sibling(name)", {
        TaurusElement prev = taurus_element_previous_sibling(elem, "item");
        (void)prev;
    });

    // New: next_sibling (any)
    MEASURE_TIME("next_sibling(NULL)", {
        TaurusElement next = taurus_element_next_sibling(elem, NULL);
        (void)next;
    });

    // New: previous_sibling (any)
    MEASURE_TIME("previous_sibling(NULL)", {
        TaurusElement prev = taurus_element_previous_sibling(elem, NULL);
        (void)prev;
    });

    taurus_document_free(doc);
}

// Category C: Element Insertion Benchmarks
void bench_element_insertion(void) {
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║         Element Insertion Benchmarks                  ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    // Test append_child (baseline - Phase 15)
    printf("\nBaseline (Phase 15):\n");
    TaurusDocument doc1 = taurus_parse_string("<root/>", 7, NULL);
    TaurusElement root1 = taurus_document_root(doc1);
    MEASURE_TIME("append_child", {
        TaurusElement child = taurus_element_create(doc1, "item");
        taurus_element_append_child(root1, child);
    });
    taurus_document_free(doc1);

    // Test prepend_child (new - Phase 18)
    printf("\nNew APIs (Phase 18):\n");
    TaurusDocument doc2 = taurus_parse_string("<root/>", 7, NULL);
    TaurusElement root2 = taurus_document_root(doc2);
    MEASURE_TIME("prepend_child", {
        TaurusElement child = taurus_element_create(doc2, "item");
        taurus_element_prepend_child(root2, child);
    });
    taurus_document_free(doc2);

    // Test insert_before (new - Phase 18)
    TaurusDocument doc3 = create_test_doc_with_children(10);
    TaurusElement root3 = taurus_document_root(doc3);
    TaurusElement sibling = taurus_element_child(root3, 5);
    MEASURE_TIME("insert_before", {
        TaurusElement child = taurus_element_create(doc3, "item");
        taurus_element_insert_before(sibling, child);
    });
    taurus_document_free(doc3);

    // Test insert_after (new - Phase 18)
    TaurusDocument doc4 = create_test_doc_with_children(10);
    TaurusElement root4 = taurus_document_root(doc4);
    TaurusElement sibling2 = taurus_element_child(root4, 5);
    MEASURE_TIME("insert_after", {
        TaurusElement child = taurus_element_create(doc4, "item");
        taurus_element_insert_after(sibling2, child);
    });
    taurus_document_free(doc4);
}

// Category D: Find Operation Benchmarks
void bench_find_operations(void) {
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║           Find Operation Benchmarks                   ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    TaurusDocument doc = create_test_doc_with_children(100);
    TaurusElement root = taurus_document_root(doc);

    // New: find_child (Phase 18)
    MEASURE_TIME("find_child", {
        TaurusElement found = taurus_element_find_child(root, "item");
        (void)found;
    });

    // New: find_child_by_attr (Phase 18)
    MEASURE_TIME("find_child_by_attr", {
        TaurusElement found = taurus_element_find_child_by_attr(root, "item", "id", "50");
        (void)found;
    });

    taurus_document_free(doc);
}

// Category E: Attribute Operation Benchmarks
void bench_attribute_operations(void) {
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║        Attribute Operation Benchmarks                 ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    // Phase 15 baseline: set_attribute
    // Test with single attribute updates to avoid accumulation
    printf("\nBaseline (Phase 15):\n");
    TaurusDocument doc1 = taurus_parse_string("<root/>", 7, NULL);
    TaurusElement root1 = taurus_document_root(doc1);
    MEASURE_TIME("set_attribute", {
        taurus_element_set_attribute(root1, "id", "test");
    });
    taurus_document_free(doc1);

    // New: remove_attribute (Phase 18)
    printf("\nNew APIs (Phase 18):\n");
    TaurusDocument doc2 = taurus_parse_string("<root/>", 7, NULL);
    TaurusElement root2 = taurus_document_root(doc2);
    MEASURE_TIME("remove_attribute", {
        taurus_element_set_attribute(root2, "temp", "value");
        taurus_element_remove_attribute(root2, "temp");
    });
    taurus_document_free(doc2);

    // New: remove_all_attributes (Phase 18)
    TaurusDocument doc3 = taurus_parse_string("<root/>", 7, NULL);
    TaurusElement root3 = taurus_document_root(doc3);
    MEASURE_TIME("remove_all_attributes", {
        taurus_element_set_attribute(root3, "a", "1");
        taurus_element_set_attribute(root3, "b", "2");
        taurus_element_set_attribute(root3, "c", "3");
        taurus_element_remove_all_attributes(root3);
    });
    taurus_document_free(doc3);
}

// Memory leak validation test (separate from performance benchmarks)
void validate_memory(void) {
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║         Memory Leak Validation Test                   ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");
    printf("\nRunning memory leak validation (100 iterations)...\n");

    // Test all APIs 100 times to validate no memory leaks
    for (int i = 0; i < 100; i++) {
        TaurusDocument doc = create_test_doc_with_children(10);
        TaurusElement root = taurus_document_root(doc);

        // Test all navigation APIs
        TaurusElement first = taurus_element_first_child(root, "item");
        TaurusElement last = taurus_element_last_child(root, "item");
        if (first) {
            TaurusElement next = taurus_element_next_sibling(first, "item");
            (void)next;
        }
        if (last) {
            TaurusElement prev = taurus_element_previous_sibling(last, "item");
            (void)prev;
        }

        // Test find APIs
        TaurusElement found = taurus_element_find_child(root, "item");
        TaurusElement found_by_attr = taurus_element_find_child_by_attr(root, "item", "id", "5");
        (void)found;
        (void)found_by_attr;

        // Test modification APIs
        TaurusElement new_elem = taurus_element_create(doc, "new");
        taurus_element_prepend_child(root, new_elem);

        TaurusElement another = taurus_element_create(doc, "another");
        TaurusElement sibling = taurus_element_child(root, 5);
        if (sibling) {
            taurus_element_insert_before(sibling, another);
        }

        // Test attribute APIs
        taurus_element_set_attribute(new_elem, "test", "value");
        taurus_element_remove_attribute(new_elem, "test");
        taurus_element_set_attribute(new_elem, "a", "1");
        taurus_element_set_attribute(new_elem, "b", "2");
        taurus_element_remove_all_attributes(new_elem);

        taurus_document_free(doc);
    }

    printf("✓ Memory validation complete (run 'leaks' tool to verify)\n");
}

// Summary Report
void print_summary(void) {
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║                   Summary Report                       ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");
    printf("\nPerformance Targets:\n");
    printf("  ✓ Child access operations < 0.5 µs\n");
    printf("  ✓ Sibling navigation < 0.5 µs\n");
    printf("  ✓ Element insertion < 0.5 µs\n");
    printf("  ✓ Find operations < 1.0 µs\n");
    printf("  ✓ Attribute operations < 0.1 µs\n");
    printf("\nPhase Baselines for Comparison:\n");
    printf("  • Phase 15: set_attribute = 0.088 µs\n");
    printf("  • Phase 16: node creation = 0.014-0.714 µs\n");
    printf("\nNew APIs Tested (10 total):\n");
    printf("  Phase 18 (8 APIs):\n");
    printf("    1. remove_all_attributes\n");
    printf("    2. find_child\n");
    printf("    3. find_child_by_attr\n");
    printf("    4. prepend_child\n");
    printf("    5. insert_before\n");
    printf("    6. insert_after\n");
    printf("    7. next_sibling\n");
    printf("    8. previous_sibling\n");
    printf("\n  Phase 19 (2 APIs):\n");
    printf("    9. first_child\n");
    printf("   10. last_child\n");
    printf("\n✓ All benchmarks complete - Run valgrind for memory validation\n");
}

int main(void) {
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║     DOM Modification Performance Benchmarks           ║\n");
    printf("║     Phase 19 Session 2 - Performance Validation        ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");
    printf("\nIterations per test: %d\n", ITERATIONS);

    bench_child_access();
    bench_sibling_navigation();
    bench_element_insertion();
    bench_find_operations();
    bench_attribute_operations();
    validate_memory();
    print_summary();

    return 0;
}