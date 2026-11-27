#include "benchmark.h"
#include "bench_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* Payload size hint for throughput reporting.  Set per benchmark via
 * bench_set_payload_size_kb().  Zero disables MB/s reporting. */
static double g_payload_size_kb = 0.0;

void bench_set_payload_size_kb(double size_kb) {
    g_payload_size_kb = size_kb;
}

/* Wall / CPU / RSS come from the shared portable seam (bench_time.h);
 * the semantics are unchanged: real-time wall clock, process CPU
 * time, peak RSS normalized to KiB. */

static double calculate_mean(const double* values, size_t count) {
    double sum = 0.0;
    for (size_t i = 0; i < count; i++) sum += values[i];
    return sum / (double)count;
}

static double calculate_stddev(const double* values, size_t count, double mean) {
    double sum_sq_diff = 0.0;
    for (size_t i = 0; i < count; i++) {
        double diff = values[i] - mean;
        sum_sq_diff += diff * diff;
    }
    return sqrt(sum_sq_diff / (double)count);
}

static double find_min(const double* values, size_t count) {
    double min_val = values[0];
    for (size_t i = 1; i < count; i++) {
        if (values[i] < min_val) min_val = values[i];
    }
    return min_val;
}

static double find_max(const double* values, size_t count) {
    double max_val = values[0];
    for (size_t i = 1; i < count; i++) {
        if (values[i] > max_val) max_val = values[i];
    }
    return max_val;
}

BenchResult bench_run(const char* name,
                      BenchFunc fn,
                      void* ctx,
                      size_t iterations) {
    double* wall = (double*)malloc(iterations * sizeof(double));
    double* cpu  = (double*)malloc(iterations * sizeof(double));
    if (!wall || !cpu) {
        fprintf(stderr, "Failed to allocate memory for benchmark '%s'\n", name);
        free(wall); free(cpu);
        exit(1);
    }

    /* Warm-up: prime caches, branch predictors, JIT (for some tools). */
    fn(ctx);

    double rss_peak_kb = 0.0;
    double rss_before = bench_rss_peak_kb();

    for (size_t i = 0; i < iterations; i++) {
        double w0 = bench_wall_real_us();
        double c0 = bench_cpu_us();
        fn(ctx);
        double c1 = bench_cpu_us();
        double w1 = bench_wall_real_us();

        wall[i] = w1 - w0;
        cpu[i]  = c1 - c0;

        double rss_now = bench_rss_peak_kb();
        if (rss_now > rss_peak_kb) rss_peak_kb = rss_now;
    }

    /* If RSS didn't move from the pre-run baseline, the benchmark
     * didn't allocate anything that survived; report the baseline so
     * the comparison table still has a meaningful number. */
    if (rss_peak_kb <= rss_before) rss_peak_kb = rss_before;

    double wall_mean = calculate_mean(wall, iterations);
    double cpu_mean  = calculate_mean(cpu, iterations);

    BenchResult result;
    result.name        = name;
    result.mean_us     = wall_mean;
    result.stddev_us   = calculate_stddev(wall, iterations, wall_mean);
    result.min_us      = find_min(wall, iterations);
    result.max_us      = find_max(wall, iterations);
    result.cpu_mean_us = cpu_mean;
    result.rss_peak_kb = rss_peak_kb;
    result.iterations  = iterations;
    result.ops_per_sec = (wall_mean > 0.0) ? (1000000.0 / wall_mean) : 0.0;
    result.mb_per_sec  = (g_payload_size_kb > 0.0 && wall_mean > 0.0)
        ? (g_payload_size_kb / 1024.0) / (wall_mean / 1000000.0)
        : 0.0;

    free(wall);
    free(cpu);
    return result;
}

void bench_print_result(const BenchResult* r) {
    printf("  %-28s ", r->name);
    printf("mean %8.2f us  ", r->mean_us);
    printf("cpu %8.2f us  ", r->cpu_mean_us);
    printf("rss %7.0f KB  ", r->rss_peak_kb);
    if (r->mb_per_sec > 0.0) {
        printf("%6.1f MB/s", r->mb_per_sec);
    } else if (r->ops_per_sec > 0.0) {
        printf("%8.0f ops/s", r->ops_per_sec);
    }
    printf("\n");
}

void bench_print_header(const char* title) {
    printf("\n");
    printf("================================================================================\n");
    printf("%s\n", title);
    printf("================================================================================\n");
    printf("  %-28s %-19s %-19s %-16s %s\n",
           "Benchmark", "Wall time", "CPU time", "Peak RSS", "Throughput");
    printf("--------------------------------------------------------------------------------\n");
}

void bench_print_comparison(const BenchResult* leptris,
                            const BenchResult* competitor,
                            const char* competitor_name) {
    double wall_speedup = competitor->mean_us / leptris->mean_us;
    double cpu_speedup  = competitor->cpu_mean_us / leptris->cpu_mean_us;
    double rss_ratio    = (competitor->rss_peak_kb > 0.0)
        ? (leptris->rss_peak_kb / competitor->rss_peak_kb)
        : 0.0;

    printf("\n  vs %s:\n", competitor_name);
    printf("    wall:  leptris %8.2f us | %s %8.2f us | %s %.2fx\n",
           leptris->mean_us, competitor_name, competitor->mean_us,
           (wall_speedup >= 1.0) ? "faster" : "slower",
           (wall_speedup >= 1.0) ? wall_speedup : 1.0 / wall_speedup);
    printf("    cpu:   leptris %8.2f us | %s %8.2f us | %s %.2fx\n",
           leptris->cpu_mean_us, competitor_name, competitor->cpu_mean_us,
           (cpu_speedup >= 1.0) ? "faster" : "slower",
           (cpu_speedup >= 1.0) ? cpu_speedup : 1.0 / cpu_speedup);
    printf("    rss:   leptris %7.0f KB | %s %7.0f KB | %s %.2fx\n",
           leptris->rss_peak_kb, competitor_name, competitor->rss_peak_kb,
           (rss_ratio <= 1.0) ? "smaller" : "larger",
           (rss_ratio <= 1.0) ? 1.0 / rss_ratio : rss_ratio);
}

void bench_print_summary(const char* category,
                         const BenchResult* results,
                         size_t count) {
    printf("\n%s Summary:\n", category);
    printf("----------------------------------------------------------------------\n");

    double total_wall = 0.0;
    double total_cpu  = 0.0;
    double max_rss    = 0.0;
    for (size_t i = 0; i < count; i++) {
        total_wall += results[i].mean_us;
        total_cpu  += results[i].cpu_mean_us;
        if (results[i].rss_peak_kb > max_rss) max_rss = results[i].rss_peak_kb;
        printf("  %-28s wall %8.2f us  cpu %8.2f us\n",
               results[i].name, results[i].mean_us, results[i].cpu_mean_us);
    }

    printf("----------------------------------------------------------------------\n");
    printf("  total wall: %.2f us   total cpu: %.2f us   peak rss: %.0f KB\n",
           total_wall, total_cpu, max_rss);
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
    fprintf(f, "  \"timestamp\": %ld,\n", (long)time(NULL));
    fprintf(f, "  \"results\": [\n");

    for (size_t i = 0; i < count; i++) {
        const BenchResult* r = &results[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": \"%s\",\n", r->name);
        fprintf(f, "      \"mean_us\": %.4f,\n", r->mean_us);
        fprintf(f, "      \"stddev_us\": %.4f,\n", r->stddev_us);
        fprintf(f, "      \"min_us\": %.4f,\n", r->min_us);
        fprintf(f, "      \"max_us\": %.4f,\n", r->max_us);
        fprintf(f, "      \"cpu_mean_us\": %.4f,\n", r->cpu_mean_us);
        fprintf(f, "      \"rss_peak_kb\": %.0f,\n", r->rss_peak_kb);
        fprintf(f, "      \"ops_per_sec\": %.2f,\n", r->ops_per_sec);
        fprintf(f, "      \"mb_per_sec\": %.4f,\n", r->mb_per_sec);
        fprintf(f, "      \"iterations\": %zu\n", r->iterations);
        fprintf(f, "    }%s\n", (i < count - 1) ? "," : "");
    }

    fprintf(f, "  ]\n}\n");
    fclose(f);
    printf("[ok] Benchmark results written to %s\n", filename);
}
