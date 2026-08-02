/* Taurus Serialization Benchmark
 * Measures performance of XML serialization operations
 */

#include "taurus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

// Timing utilities
static double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
}

// Load file into memory
static char* load_file(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* content = malloc(*size + 1);
    fread(content, 1, *size, f);
    content[*size] = '\0';
    fclose(f);

    return content;
}

// Benchmark: Serialize small document
static void bench_serialize_small(void) {
    size_t size;
    char* xml = load_file("benchmarks/fixtures/small.xml", &size);
    if (!xml) {
        printf("ERROR: Could not load small.xml\n");
        return;
    }

    TaurusDocument doc = taurus_parse_string(xml, size, NULL);
    free(xml);

    const int iterations = 1000;
    double start = get_time_us();

    for (int i = 0; i < iterations; i++) {
        char* result = taurus_document_serialize(doc, NULL);
        taurus_free_string(result);
    }

    double end = get_time_us();
    double total = end - start;

    printf("  \"serialize_small_compact\": {\n");
    printf("    \"iterations\": %d,\n", iterations);
    printf("    \"file_size\": %zu,\n", size);
    printf("    \"total_us\": %.2f,\n", total);
    printf("    \"avg_us\": %.4f\n", total / iterations);
    printf("  }");

    taurus_document_free(doc);
}

// Benchmark: Serialize medium document
static void bench_serialize_medium(void) {
    size_t size;
    char* xml = load_file("benchmarks/fixtures/medium.xml", &size);
    if (!xml) {
        printf("ERROR: Could not load medium.xml\n");
        return;
    }

    TaurusDocument doc = taurus_parse_string(xml, size, NULL);
    free(xml);

    const int iterations = 100;
    double start = get_time_us();

    for (int i = 0; i < iterations; i++) {
        char* result = taurus_document_serialize(doc, NULL);
        taurus_free_string(result);
    }

    double end = get_time_us();
    double total = end - start;

    printf(",\n  \"serialize_medium_compact\": {\n");
    printf("    \"iterations\": %d,\n", iterations);
    printf("    \"file_size\": %zu,\n", size);
    printf("    \"total_us\": %.2f,\n", total);
    printf("    \"avg_us\": %.4f\n", total / iterations);
    printf("  }");

    taurus_document_free(doc);
}

// Benchmark: Serialize large document
static void bench_serialize_large(void) {
    size_t size;
    char* xml = load_file("benchmarks/fixtures/large.xml", &size);
    if (!xml) {
        printf("ERROR: Could not load large.xml\n");
        return;
    }

    TaurusDocument doc = taurus_parse_string(xml, size, NULL);
    free(xml);

    const int iterations = 10;
    double start = get_time_us();

    for (int i = 0; i < iterations; i++) {
        char* result = taurus_document_serialize(doc, NULL);
        taurus_free_string(result);
    }

    double end = get_time_us();
    double total = end - start;

    printf(",\n  \"serialize_large_compact\": {\n");
    printf("    \"iterations\": %d,\n", iterations);
    printf("    \"file_size\": %zu,\n", size);
    printf("    \"total_us\": %.2f,\n", total);
    printf("    \"avg_us\": %.4f\n", total / iterations);
    printf("  }");

    taurus_document_free(doc);
}

// Benchmark: Pretty-print vs compact
static void bench_format_comparison(void) {
    size_t size;
    char* xml = load_file("benchmarks/fixtures/small.xml", &size);
    if (!xml) {
        printf("ERROR: Could not load small.xml\n");
        return;
    }

    TaurusDocument doc = taurus_parse_string(xml, size, NULL);
    free(xml);

    TaurusSerializeOptions pretty_opts = { .indent = 2 };

    const int iterations = 1000;

    // Compact
    double start_compact = get_time_us();
    for (int i = 0; i < iterations; i++) {
        char* result = taurus_document_serialize(doc, NULL);
        taurus_free_string(result);
    }
    double end_compact = get_time_us();
    double total_compact = end_compact - start_compact;

    // Pretty-print
    double start_pretty = get_time_us();
    for (int i = 0; i < iterations; i++) {
        char* result = taurus_document_serialize(doc, &pretty_opts);
        taurus_free_string(result);
    }
    double end_pretty = get_time_us();
    double total_pretty = end_pretty - start_pretty;

    printf(",\n  \"format_compact\": {\n");
    printf("    \"iterations\": %d,\n", iterations);
    printf("    \"total_us\": %.2f,\n", total_compact);
    printf("    \"avg_us\": %.4f\n", total_compact / iterations);
    printf("  },\n");

    printf("  \"format_pretty\": {\n");
    printf("    \"iterations\": %d,\n", iterations);
    printf("    \"total_us\": %.2f,\n", total_pretty);
    printf("    \"avg_us\": %.4f\n", total_pretty / iterations);
    printf("  }\n");

    taurus_document_free(doc);
}

int main(void) {
    printf("{\n");
    printf("  \"library\": \"taurus\",\n");
    printf("  \"version\": \"0.3.0\",\n");
    printf("  \"operations\": {\n");

    bench_serialize_small();
    bench_serialize_medium();
    bench_serialize_large();
    bench_format_comparison();

    printf("  }\n");
    printf("}\n");

    return 0;
}
