/* Leptris DOM Modification Benchmark
 * Measures performance of DOM modification operations
 */

#include "leptris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define ITERATIONS 1000

// Timing utilities
static double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
}

// Benchmark: Create element
static void bench_create_element(void) {
    LeptrisDocument doc = leptris_parse_string("<root/>", 7, NULL);
    LeptrisElement root = leptris_document_root(doc);

    double start = get_time_us();

    for (int i = 0; i < ITERATIONS; i++) {
        LeptrisElement elem = leptris_element_create(doc, "item");
        leptris_element_append_child(root, elem);
    }

    double end = get_time_us();
    double total = end - start;

    printf("  \"create_element\": {\n");
    printf("    \"iterations\": %d,\n", ITERATIONS);
    printf("    \"total_us\": %.2f,\n", total);
    printf("    \"avg_us\": %.4f\n", total / ITERATIONS);
    printf("  }");

    leptris_document_free(doc);
}

// Benchmark: Append child
static void bench_append_child(void) {
    LeptrisDocument doc = leptris_parse_string("<root/>", 7, NULL);
    LeptrisElement root = leptris_document_root(doc);

    // Pre-create elements
    LeptrisElement items[ITERATIONS];
    for (int i = 0; i < ITERATIONS; i++) {
        items[i] = leptris_element_create(doc, "item");
    }

    double start = get_time_us();

    for (int i = 0; i < ITERATIONS; i++) {
        leptris_element_append_child(root, items[i]);
    }

    double end = get_time_us();
    double total = end - start;

    printf(",\n  \"append_child\": {\n");
    printf("    \"iterations\": %d,\n", ITERATIONS);
    printf("    \"total_us\": %.2f,\n", total);
    printf("    \"avg_us\": %.4f\n", total / ITERATIONS);
    printf("  }");

    leptris_document_free(doc);
}

// Benchmark: Remove child
static void bench_remove_child(void) {
    LeptrisDocument doc = leptris_parse_string("<root/>", 7, NULL);
    LeptrisElement root = leptris_document_root(doc);

    // Pre-create and append elements
    LeptrisElement items[ITERATIONS];
    for (int i = 0; i < ITERATIONS; i++) {
        items[i] = leptris_element_create(doc, "item");
        leptris_element_append_child(root, items[i]);
    }

    double start = get_time_us();

    for (int i = 0; i < ITERATIONS; i++) {
        leptris_element_remove_child(root, items[i]);
    }

    double end = get_time_us();
    double total = end - start;

    printf(",\n  \"remove_child\": {\n");
    printf("    \"iterations\": %d,\n", ITERATIONS);
    printf("    \"total_us\": %.2f,\n", total);
    printf("    \"avg_us\": %.4f\n", total / ITERATIONS);
    printf("  }");

    leptris_document_free(doc);
}

// Benchmark: Set text content
static void bench_set_text(void) {
    LeptrisDocument doc = leptris_parse_string("<root/>", 7, NULL);
    LeptrisElement root = leptris_document_root(doc);

    // Pre-create elements
    LeptrisElement items[ITERATIONS];
    for (int i = 0; i < ITERATIONS; i++) {
        items[i] = leptris_element_create(doc, "item");
        leptris_element_append_child(root, items[i]);
    }

    double start = get_time_us();

    for (int i = 0; i < ITERATIONS; i++) {
        leptris_element_set_text(items[i], "Test content");
    }

    double end = get_time_us();
    double total = end - start;

    printf(",\n  \"set_text\": {\n");
    printf("    \"iterations\": %d,\n", ITERATIONS);
    printf("    \"total_us\": %.2f,\n", total);
    printf("    \"avg_us\": %.4f\n", total / ITERATIONS);
    printf("  }");

    leptris_document_free(doc);
}

// Benchmark: Set attribute
static void bench_set_attribute(void) {
    LeptrisDocument doc = leptris_parse_string("<root/>", 7, NULL);
    LeptrisElement root = leptris_document_root(doc);

    // Pre-create elements
    LeptrisElement items[ITERATIONS];
    for (int i = 0; i < ITERATIONS; i++) {
        items[i] = leptris_element_create(doc, "item");
        leptris_element_append_child(root, items[i]);
    }

    double start = get_time_us();

    for (int i = 0; i < ITERATIONS; i++) {
        leptris_element_set_attribute(items[i], "id", "123");
    }

    double end = get_time_us();
    double total = end - start;

    printf(",\n  \"set_attribute\": {\n");
    printf("    \"iterations\": %d,\n", ITERATIONS);
    printf("    \"total_us\": %.2f,\n", total);
    printf("    \"avg_us\": %.4f\n", total / ITERATIONS);
    printf("  }");

    leptris_document_free(doc);
}

// Benchmark: Remove attribute
static void bench_remove_attribute(void) {
    LeptrisDocument doc = leptris_parse_string("<root/>", 7, NULL);
    LeptrisElement root = leptris_document_root(doc);

    // Pre-create elements with attributes
    LeptrisElement items[ITERATIONS];
    for (int i = 0; i < ITERATIONS; i++) {
        items[i] = leptris_element_create(doc, "item");
        leptris_element_append_child(root, items[i]);
        leptris_element_set_attribute(items[i], "id", "123");
    }

    double start = get_time_us();

    for (int i = 0; i < ITERATIONS; i++) {
        leptris_element_remove_attribute(items[i], "id");
    }

    double end = get_time_us();
    double total = end - start;

    printf(",\n  \"remove_attribute\": {\n");
    printf("    \"iterations\": %d,\n", ITERATIONS);
    printf("    \"total_us\": %.2f,\n", total);
    printf("    \"avg_us\": %.4f\n", total / ITERATIONS);
    printf("  }\n");

    leptris_document_free(doc);
}

int main(void) {
    printf("{\n");
    printf("  \"library\": \"leptris\",\n");
    printf("  \"version\": \"0.3.0\",\n");
    printf("  \"operations\": {\n");

    bench_create_element();
    bench_append_child();
    bench_remove_child();
    bench_set_text();
    bench_set_attribute();
    bench_remove_attribute();

    printf("  }\n");
    printf("}\n");

    return 0;
}
