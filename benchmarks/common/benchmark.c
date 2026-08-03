#include "benchmark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include <sys/resource.h>

/* Payload size hint for throughput reporting.  Set per benchmark via
 * bench_set_payload_size_kb().  Zero disables MB/s reporting. */
static double g_payload_size_kb = 0.0;

void bench_set_payload_size_kb(double size_kb) {
    g_payload_size_kb = size_kb;
}

/* Wall-clock time in microseconds. */
static double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
}

/* Per-process CPU time in microseconds.
 *
 * clock_gettime(CLOCK_PROCESS_CPUTIME_ID) is available on Linux and
 * macOS 10.12+.  Falls back to clock() (which has CLOCKS_PER_SEC
 * granularity and includes wait time on some platforms) if the
 * POSIX timer is missing. */
static double get_cpu_time_us(void) {
#if defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0 && \
    defined(_POSIX_CPUTIME) && _POSIX_CPUTIME > 0
    struct timespec ts;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0) {
        return ts.tv_sec * 1000000.0 + ts.tv_nsec / 1000.0;
    }
#endif
    return (double)clock() * (1000000.0 / CLOCKS_PER_SEC);
}

/* Peak resident set size in kilobytes.
 *
 * getrusage(RUSAGE_SELF).ru_maxrss is the portable POSIX answer.
 * Units differ by platform: kilobytes on Linux, bytes on macOS/BSD.
 * Normalize to KB.  This is a high-water mark for the process; the
 * benchmark harness reads it after each iteration and keeps the
 * max, so the reported value reflects the heaviest single call. */
static double get_rss_kb(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 0.0;
#if defined(__APPLE__) && defined(__MACH__)
    return (double)ru.ru_maxrss / 1024.0;  /* bytes -> KB */
#else
    return (double)ru.ru_maxrss;            /* already KB */
#endif
}

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
    double rss_before = get_rss_kb();

    for (size_t i = 0; i < iterations; i++) {
        double w0 = get_time_us();
        double c0 = get_cpu_time_us();
        fn(ctx);
        double c1 = get_cpu_time_us();
        double w1 = get_time_us();

        wall[i] = w1 - w0;
        cpu[i]  = c1 - c0;

        double rss_now = get_rss_kb();
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

void bench_print_comparison(const BenchResult* taurus,
                            const BenchResult* competitor,
                            const char* competitor_name) {
    double wall_speedup = competitor->mean_us / taurus->mean_us;
    double cpu_speedup  = competitor->cpu_mean_us / taurus->cpu_mean_us;
    double rss_ratio    = (competitor->rss_peak_kb > 0.0)
        ? (taurus->rss_peak_kb / competitor->rss_peak_kb)
        : 0.0;

    printf("\n  vs %s:\n", competitor_name);
    printf("    wall:  taurus %8.2f us | %s %8.2f us | %s %.2fx\n",
           taurus->mean_us, competitor_name, competitor->mean_us,
           (wall_speedup >= 1.0) ? "faster" : "slower",
           (wall_speedup >= 1.0) ? wall_speedup : 1.0 / wall_speedup);
    printf("    cpu:   taurus %8.2f us | %s %8.2f us | %s %.2fx\n",
           taurus->cpu_mean_us, competitor_name, competitor->cpu_mean_us,
           (cpu_speedup >= 1.0) ? "faster" : "slower",
           (cpu_speedup >= 1.0) ? cpu_speedup : 1.0 / cpu_speedup);
    printf("    rss:   taurus %7.0f KB | %s %7.0f KB | %s %.2fx\n",
           taurus->rss_peak_kb, competitor_name, competitor->rss_peak_kb,
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
