#include "benchmark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>

/* Get current time in microseconds */
static double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
}

/* Calculate mean */
static double calculate_mean(const double* values, size_t count) {
    double sum = 0.0;
    for (size_t i = 0; i < count; i++) {
        sum += values[i];
    }
    return sum / count;
}

/* Calculate standard deviation */
static double calculate_stddev(const double* values, size_t count, double mean) {
    double sum_sq_diff = 0.0;
    for (size_t i = 0; i < count; i++) {
        double diff = values[i] - mean;
        sum_sq_diff += diff * diff;
    }
    return sqrt(sum_sq_diff / count);
}

/* Find minimum value */
static double find_min(const double* values, size_t count) {
    double min_val = values[0];
    for (size_t i = 1; i < count; i++) {
        if (values[i] < min_val) {
            min_val = values[i];
        }
    }
    return min_val;
}

/* Find maximum value */
static double find_max(const double* values, size_t count) {
    double max_val = values[0];
    for (size_t i = 1; i < count; i++) {
        if (values[i] > max_val) {
            max_val = values[i];
        }
    }
    return max_val;
}

BenchResult bench_run(const char* name,
                      BenchFunc fn,
                      void* ctx,
                      size_t iterations) {
    double* times = (double*)malloc(iterations * sizeof(double));
    if (!times) {
        fprintf(stderr, "Failed to allocate memory for benchmark\n");
        exit(1);
    }

    /* Warm-up run */
    fn(ctx);

    /* Timed runs */
    for (size_t i = 0; i < iterations; i++) {
        double start = get_time_us();
        fn(ctx);
        double end = get_time_us();
        times[i] = end - start;
    }

    /* Calculate statistics */
    double mean = calculate_mean(times, iterations);
    double stddev = calculate_stddev(times, iterations, mean);
    double min_val = find_min(times, iterations);
    double max_val = find_max(times, iterations);

    free(times);

    BenchResult result;
    result.name = name;
    result.mean_us = mean;
    result.stddev_us = stddev;
    result.min_us = min_val;
    result.max_us = max_val;
    result.iterations = iterations;

    return result;
}

void bench_print_result(const BenchResult* result) {
    printf("  %-25s ", result->name);
    printf("Mean: %8.2f µs  ", result->mean_us);
    printf("±%6.2f µs  ", result->stddev_us);
    printf("Min: %8.2f µs  ", result->min_us);
    printf("Max: %8.2f µs\n", result->max_us);
}

void bench_print_header(const char* title) {
    printf("\n");
    printf("================================================================\n");
    printf("%s\n", title);
    printf("================================================================\n");
    printf("  %-25s %-15s %-12s %-15s %-15s\n",
           "Benchmark", "Mean", "Stddev", "Min", "Max");
    printf("----------------------------------------------------------------\n");
}

void bench_print_comparison(const BenchResult* taurus,
                            const BenchResult* competitor,
                            const char* competitor_name) {
    double speedup = competitor->mean_us / taurus->mean_us;

    printf("\n  Comparison vs %s:\n", competitor_name);
    printf("    Taurus:      %.2f µs\n", taurus->mean_us);
    printf("    %s: %.2f µs\n", competitor_name, competitor->mean_us);

    if (speedup >= 1.0) {
        printf("    Speedup:     %.2fx faster ✓\n", speedup);
    } else {
        printf("    Speed:       %.2fx slower ✗\n", 1.0 / speedup);
    }
}

void bench_print_summary(const char* category,
                         const BenchResult* results,
                         size_t count) {
    printf("\n%s Summary:\n", category);
    printf("----------------------------------------------------------\n");

    double total_mean = 0.0;
    for (size_t i = 0; i < count; i++) {
        total_mean += results[i].mean_us;
        printf("  %-25s %.2f µs\n", results[i].name, results[i].mean_us);
    }

    printf("----------------------------------------------------------\n");
    printf("  Total:                    %.2f µs\n", total_mean);
    printf("  Average:                  %.2f µs\n", total_mean / count);
}

void bench_write_json(const char* library,
                      const BenchResult* results,
                      size_t count,
                      const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing\n", filename);
        return;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"library\": \"%s\",\n", library);
    fprintf(f, "  \"timestamp\": \"%ld\",\n", (long)time(NULL));
    fprintf(f, "  \"results\": [\n");

    for (size_t i = 0; i < count; i++) {
        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": \"%s\",\n", results[i].name);
        fprintf(f, "      \"mean_us\": %.2f,\n", results[i].mean_us);
        fprintf(f, "      \"stddev_us\": %.2f,\n", results[i].stddev_us);
        fprintf(f, "      \"min_us\": %.2f,\n", results[i].min_us);
        fprintf(f, "      \"max_us\": %.2f,\n", results[i].max_us);
        fprintf(f, "      \"iterations\": %zu\n", results[i].iterations);
        fprintf(f, "    }%s\n", (i < count - 1) ? "," : "");
    }

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");

    fclose(f);
    printf("✅ Benchmark results written to %s\n", filename);
}