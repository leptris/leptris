#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include "../../lib/include/leptris.h"

/* ============================================================================
 * Benchmark Context
 * ============================================================================ */

typedef struct {
    char* xml;
    size_t xml_len;
    int iterations;
    double* samples_us;  /* Array of timings for statistics */
    double mean_us;
    double median_us;  /* Add median for more robust statistics */
    double min_us;
    double max_us;
    double stddev_us;
} BenchContext;

/* ============================================================================
 * Timing Utilities
 * ============================================================================ */

static double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000000.0 + (double)tv.tv_usec;
}

/* ============================================================================
 * Warm-up Phase (Eliminate Cold Cache Effects)
 * ============================================================================ */

/* Warm-up to eliminate cold cache effects */
static void warmup_cache(BenchContext* ctx, int iterations) {
    printf("Warming up cache (%d iterations)...\n", iterations);
    for (int i = 0; i < iterations; i++) {
        char* xml_copy = malloc(ctx->xml_len + 1);
        if (!xml_copy) continue;
        memcpy(xml_copy, ctx->xml, ctx->xml_len);
        xml_copy[ctx->xml_len] = '\0';

        LeptrisDocument doc = leptris_parse_string_inplace(xml_copy, ctx->xml_len, NULL);
        leptris_document_free(doc);
        /* Note: xml_copy freed by leptris_document_free when using in-place */
    }
}

/* ============================================================================
 * Statistics Calculation
 * ============================================================================ */

/* Compare function for qsort (used in median calculation) */
static int compare_doubles(const void* a, const void* b) {
    double diff = (*(double*)a - *(double*)b);
    return (diff > 0) - (diff < 0);
}

static void calculate_statistics(BenchContext* ctx) {
    double sum = 0.0;
    double min = ctx->samples_us[0];
    double max = ctx->samples_us[0];

    /* Calculate mean, min, max */
    for (int i = 0; i < ctx->iterations; i++) {
        double sample = ctx->samples_us[i];
        sum += sample;
        if (sample < min) min = sample;
        if (sample > max) max = sample;
    }

    ctx->mean_us = sum / ctx->iterations;
    ctx->min_us = min;
    ctx->max_us = max;

    /* Calculate median */
    double* sorted = malloc(ctx->iterations * sizeof(double));
    if (sorted) {
        memcpy(sorted, ctx->samples_us, ctx->iterations * sizeof(double));
        qsort(sorted, ctx->iterations, sizeof(double), compare_doubles);

        if (ctx->iterations % 2 == 0) {
            ctx->median_us = (sorted[ctx->iterations/2 - 1] + sorted[ctx->iterations/2]) / 2.0;
        } else {
            ctx->median_us = sorted[ctx->iterations/2];
        }
        free(sorted);
    } else {
        ctx->median_us = ctx->mean_us;  /* Fallback if allocation fails */
    }

    /* Calculate standard deviation */
    double variance_sum = 0.0;
    for (int i = 0; i < ctx->iterations; i++) {
        double diff = ctx->samples_us[i] - ctx->mean_us;
        variance_sum += diff * diff;
    }
    ctx->stddev_us = sqrt(variance_sum / ctx->iterations);
}

/* ============================================================================
 * Workflow 1: Parse-Only (Baseline)
 * ============================================================================ */

static void benchmark_parse_only(BenchContext* ctx) {
    /* Warm up cache first */
    warmup_cache(ctx, 100);

    printf("Running parse-only benchmark (%d iterations)...\n", ctx->iterations);

    for (int i = 0; i < ctx->iterations; i++) {
        /* Create writable copy for in-place parsing */
        char* xml_copy = malloc(ctx->xml_len + 1);
        if (!xml_copy) continue;
        memcpy(xml_copy, ctx->xml, ctx->xml_len);
        xml_copy[ctx->xml_len] = '\0';

        double start = get_time_us();

        LeptrisDocument doc = leptris_parse_string_inplace(xml_copy, ctx->xml_len, NULL);
        leptris_document_free(doc);
        /* Note: xml_copy freed by leptris_document_free */

        double elapsed = get_time_us() - start;
        ctx->samples_us[i] = elapsed;
    }

    calculate_statistics(ctx);
    printf("  Mean: %.2f µs, Median: %.2f µs, Min: %.2f µs, Max: %.2f µs, StdDev: %.2f µs\n",
           ctx->mean_us, ctx->median_us, ctx->min_us, ctx->max_us, ctx->stddev_us);
}

/* ============================================================================
 * Workflow 2: Parse → Query
 * ============================================================================ */

static void benchmark_parse_query(BenchContext* ctx) {
    /* Warm up cache first */
    warmup_cache(ctx, 100);

    printf("Running parse→query benchmark (%d iterations)...\n", ctx->iterations);

    for (int i = 0; i < ctx->iterations; i++) {
        /* Create writable copy for in-place parsing */
        char* xml_copy = malloc(ctx->xml_len + 1);
        if (!xml_copy) continue;
        memcpy(xml_copy, ctx->xml, ctx->xml_len);
        xml_copy[ctx->xml_len] = '\0';

        double start = get_time_us();

        LeptrisDocument doc = leptris_parse_string_inplace(xml_copy, ctx->xml_len, NULL);

        /* Execute XPath query */
        LeptrisXPathResult result = leptris_xpath_eval(doc, NULL, "//item");
        leptris_xpath_result_free(result);

        leptris_document_free(doc);

        double elapsed = get_time_us() - start;
        ctx->samples_us[i] = elapsed;
    }

    calculate_statistics(ctx);
    printf("  Mean: %.2f µs, Median: %.2f µs, Min: %.2f µs, Max: %.2f µs, StdDev: %.2f µs\n",
           ctx->mean_us, ctx->median_us, ctx->min_us, ctx->max_us, ctx->stddev_us);
}

/* ============================================================================
 * Workflow 3: Parse → Modify → Serialize (CRITICAL)
 * ============================================================================ */

static void benchmark_parse_modify_serialize(BenchContext* ctx) {
    /* Warm up cache first */
    warmup_cache(ctx, 100);

    printf("Running parse→modify→serialize benchmark (%d iterations)...\n", ctx->iterations);

    for (int i = 0; i < ctx->iterations; i++) {
        /* Create writable copy for in-place parsing */
        char* xml_copy = malloc(ctx->xml_len + 1);
        if (!xml_copy) continue;
        memcpy(xml_copy, ctx->xml, ctx->xml_len);
        xml_copy[ctx->xml_len] = '\0';

        double start = get_time_us();

        /* 1. Parse */
        LeptrisDocument doc = leptris_parse_string_inplace(xml_copy, ctx->xml_len, NULL);
        LeptrisElement root = leptris_document_root(doc);

        /* 2. Modify - add 10 elements with attributes and text */
        for (int j = 0; j < 10; j++) {
            LeptrisElement item = leptris_element_create(doc, "item");
            leptris_element_set_attribute(item, "id", "123");
            leptris_element_set_attribute(item, "type", "test");
            leptris_element_set_text(item, "Sample text content");
            leptris_element_append_child(root, item);
        }

        /* 3. Serialize */
        char* xml = leptris_document_serialize(doc, NULL);
        leptris_free_string(xml);

        /* 4. Cleanup */
        leptris_document_free(doc);

        double elapsed = get_time_us() - start;
        ctx->samples_us[i] = elapsed;
    }

    calculate_statistics(ctx);
    printf("  Mean: %.2f µs, Median: %.2f µs, Min: %.2f µs, Max: %.2f µs, StdDev: %.2f µs\n",
           ctx->mean_us, ctx->median_us, ctx->min_us, ctx->max_us, ctx->stddev_us);
}

/* ============================================================================
 * Workflow 4: Complex Modification
 * ============================================================================ */

static void benchmark_complex_modification(BenchContext* ctx) {
    /* Warm up cache first */
    warmup_cache(ctx, 100);

    printf("Running complex modification benchmark (%d iterations)...\n", ctx->iterations);

    for (int i = 0; i < ctx->iterations; i++) {
        /* Create writable copy for in-place parsing */
        char* xml_copy = malloc(ctx->xml_len + 1);
        if (!xml_copy) continue;
        memcpy(xml_copy, ctx->xml, ctx->xml_len);
        xml_copy[ctx->xml_len] = '\0';

        double start = get_time_us();

        LeptrisDocument doc = leptris_parse_string_inplace(xml_copy, ctx->xml_len, NULL);
        LeptrisElement root = leptris_document_root(doc);

        /* Complex modifications: nested elements, multiple attributes */
        for (int j = 0; j < 5; j++) {
            LeptrisElement section = leptris_element_create(doc, "section");
            leptris_element_set_attribute(section, "id", "section1");

            LeptrisElement item = leptris_element_create(doc, "item");
            leptris_element_set_attribute(item, "attr1", "value1");
            leptris_element_set_attribute(item, "attr2", "value2");
            leptris_element_set_attribute(item, "attr3", "value3");
            leptris_element_set_text(item, "Complex text content");
            leptris_element_append_child(section, item);

            leptris_element_append_child(root, section);
        }

        char* xml = leptris_document_serialize(doc, NULL);
        leptris_free_string(xml);
        leptris_document_free(doc);

        double elapsed = get_time_us() - start;
        ctx->samples_us[i] = elapsed;
    }

    calculate_statistics(ctx);
    printf("  Mean: %.2f µs, Median: %.2f µs, Min: %.2f µs, Max: %.2f µs, StdDev: %.2f µs\n",
           ctx->mean_us, ctx->median_us, ctx->min_us, ctx->max_us, ctx->stddev_us);
}

/* ============================================================================
 * File Loading
 * ============================================================================ */

static char* load_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = malloc(len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);

    *out_len = len;
    return buf;
}

/* ============================================================================
 * JSON Output
 * ============================================================================ */

typedef struct {
    double parse_only;
    double parse_query;
    double parse_modify_serialize;
    double complex_modification;
} WorkflowResults;

static void write_json_results(const char* output_path,
                                const char* small_name,
                                const char* medium_name,
                                const char* large_name,
                                WorkflowResults* small,
                                WorkflowResults* medium,
                                WorkflowResults* large,
                                int iterations) {
    FILE* f = fopen(output_path, "w");
    if (!f) {
        fprintf(stderr, "Error: Cannot write to %s\n", output_path);
        return;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"benchmark\": \"end_to_end_workflows\",\n");
    fprintf(f, "  \"date\": \"2024-12-24\",\n");
    fprintf(f, "  \"iterations\": %d,\n", iterations);
    fprintf(f, "  \"document_sizes\": {\n");
    fprintf(f, "    \"small\": \"%s\",\n", small_name);
    fprintf(f, "    \"medium\": \"%s\",\n", medium_name);
    fprintf(f, "    \"large\": \"%s\"\n", large_name);
    fprintf(f, "  },\n");
    fprintf(f, "  \"results\": {\n");

    /* Small results */
    fprintf(f, "    \"small\": {\n");
    fprintf(f, "      \"parse_only_us\": %.2f,\n", small->parse_only);
    fprintf(f, "      \"parse_query_us\": %.2f,\n", small->parse_query);
    fprintf(f, "      \"parse_modify_serialize_us\": %.2f,\n", small->parse_modify_serialize);
    fprintf(f, "      \"complex_modification_us\": %.2f\n", small->complex_modification);
    fprintf(f, "    },\n");

    /* Medium results */
    fprintf(f, "    \"medium\": {\n");
    fprintf(f, "      \"parse_only_us\": %.2f,\n", medium->parse_only);
    fprintf(f, "      \"parse_query_us\": %.2f,\n", medium->parse_query);
    fprintf(f, "      \"parse_modify_serialize_us\": %.2f,\n", medium->parse_modify_serialize);
    fprintf(f, "      \"complex_modification_us\": %.2f\n", medium->complex_modification);
    fprintf(f, "    },\n");

    /* Large results */
    fprintf(f, "    \"large\": {\n");
    fprintf(f, "      \"parse_only_us\": %.2f,\n", large->parse_only);
    fprintf(f, "      \"parse_query_us\": %.2f,\n", large->parse_query);
    fprintf(f, "      \"parse_modify_serialize_us\": %.2f,\n", large->parse_modify_serialize);
    fprintf(f, "      \"complex_modification_us\": %.2f\n", large->complex_modification);
    fprintf(f, "    }\n");

    fprintf(f, "  }\n");
    fprintf(f, "}\n");

    fclose(f);
    printf("\nJSON results written to: %s\n", output_path);
}

/* ============================================================================
 * Main Benchmark Runner
 * ============================================================================ */

int main(int argc, char** argv) {
    int iterations = 100;  /* Default: 100 iterations per benchmark */

    if (argc > 1) {
        iterations = atoi(argv[1]);
        if (iterations < 1) iterations = 100;
    }

    printf("=== Leptris End-to-End Workflow Benchmarks ===\n");
    printf("Iterations: %d\n\n", iterations);

    /* Load test fixtures */
    printf("Loading test fixtures...\n");

    size_t small_len, medium_len, large_len;
    char* small_xml = load_file("benchmarks/data/small_workflow.xml", &small_len);
    char* medium_xml = load_file("benchmarks/data/medium_workflow.xml", &medium_len);
    char* large_xml = load_file("benchmarks/data/large_workflow.xml", &large_len);

    if (!small_xml || !medium_xml || !large_xml) {
        fprintf(stderr, "Error: Failed to load test fixtures\n");
        free(small_xml);
        free(medium_xml);
        free(large_xml);
        return 1;
    }

    printf("  Small: %zu bytes\n", small_len);
    printf("  Medium: %zu bytes\n", medium_len);
    printf("  Large: %zu bytes\n\n", large_len);

    /* Allocate sample arrays */
    double* samples = malloc(iterations * sizeof(double));
    if (!samples) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        free(small_xml);
        free(medium_xml);
        free(large_xml);
        return 1;
    }

    WorkflowResults small_results = {0};
    WorkflowResults medium_results = {0};
    WorkflowResults large_results = {0};

    /* ========================================================================
     * Small Document Benchmarks
     * ======================================================================== */

    printf("=== Small Document (%zu bytes) ===\n", small_len);

    BenchContext small_ctx = {
        .xml = small_xml,
        .xml_len = small_len,
        .iterations = iterations,
        .samples_us = samples
    };

    warmup_cache(&small_ctx, 10);
    benchmark_parse_only(&small_ctx);
    small_results.parse_only = small_ctx.mean_us;

    warmup_cache(&small_ctx, 10);
    benchmark_parse_query(&small_ctx);
    small_results.parse_query = small_ctx.mean_us;

    warmup_cache(&small_ctx, 10);
    benchmark_parse_modify_serialize(&small_ctx);
    small_results.parse_modify_serialize = small_ctx.mean_us;

    warmup_cache(&small_ctx, 10);
    benchmark_complex_modification(&small_ctx);
    small_results.complex_modification = small_ctx.mean_us;

    printf("\n");

    /* ========================================================================
     * Medium Document Benchmarks
     * ======================================================================== */

    printf("=== Medium Document (%zu bytes) ===\n", medium_len);

    BenchContext medium_ctx = {
        .xml = medium_xml,
        .xml_len = medium_len,
        .iterations = iterations,
        .samples_us = samples
    };

    warmup_cache(&medium_ctx, 10);
    benchmark_parse_only(&medium_ctx);
    medium_results.parse_only = medium_ctx.mean_us;

    warmup_cache(&medium_ctx, 10);
    benchmark_parse_query(&medium_ctx);
    medium_results.parse_query = medium_ctx.mean_us;

    warmup_cache(&medium_ctx, 10);
    benchmark_parse_modify_serialize(&medium_ctx);
    medium_results.parse_modify_serialize = medium_ctx.mean_us;

    warmup_cache(&medium_ctx, 10);
    benchmark_complex_modification(&medium_ctx);
    medium_results.complex_modification = medium_ctx.mean_us;

    printf("\n");

    /* ========================================================================
     * Large Document Benchmarks
     * ======================================================================== */

    printf("=== Large Document (%zu bytes) ===\n", large_len);

    BenchContext large_ctx = {
        .xml = large_xml,
        .xml_len = large_len,
        .iterations = iterations,
        .samples_us = samples
    };

    warmup_cache(&large_ctx, 10);
    benchmark_parse_only(&large_ctx);
    large_results.parse_only = large_ctx.mean_us;

    warmup_cache(&large_ctx, 10);
    benchmark_parse_query(&large_ctx);
    large_results.parse_query = large_ctx.mean_us;

    warmup_cache(&large_ctx, 10);
    benchmark_parse_modify_serialize(&large_ctx);
    large_results.parse_modify_serialize = large_ctx.mean_us;

    warmup_cache(&large_ctx, 10);
    benchmark_complex_modification(&large_ctx);
    large_results.complex_modification = large_ctx.mean_us;

    printf("\n");

    /* Write JSON results */
    char size_small[32], size_medium[32], size_large[32];
    snprintf(size_small, sizeof(size_small), "%.1fKB", small_len / 1024.0);
    snprintf(size_medium, sizeof(size_medium), "%.1fKB", medium_len / 1024.0);
    snprintf(size_large, sizeof(size_large), "%.1fKB", large_len / 1024.0);

    write_json_results("benchmarks/results/phase17.5_session1.json",
                       size_small, size_medium, size_large,
                       &small_results, &medium_results, &large_results,
                       iterations);

    /* Cleanup */
    free(samples);
    free(small_xml);
    free(medium_xml);
    free(large_xml);

    printf("\nBenchmark complete!\n");
    return 0;
}