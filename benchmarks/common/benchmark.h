#ifndef TAURUS_BENCH_BENCHMARK_H
#define TAURUS_BENCH_BENCHMARK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Benchmark result with statistics.
 *
 * Wall-clock time answers "how long did the user wait?"  CPU time
 * answers "how much CPU did the library actually burn?" — the gap
 * reveals scheduler / I/O wait.  Peak RSS answers "how much RAM did
 * this need?"  Throughput (ops_per_sec, mb_per_sec) is derived from
 * the wall-clock mean and the per-iteration payload size.
 *
 * All time fields are microseconds.  rss_kb is kilobytes.
 */
typedef struct {
    const char* name;        /* Benchmark name */
    double mean_us;          /* Mean wall-clock time per iteration */
    double stddev_us;        /* Standard deviation across iterations */
    double min_us;           /* Fastest single iteration */
    double max_us;           /* Slowest single iteration */
    double cpu_mean_us;      /* Mean CPU time per iteration */
    double rss_peak_kb;      /* Peak RSS across all iterations */
    double ops_per_sec;      /* Iterations per second (wall-clock) */
    double mb_per_sec;       /* Payload bytes per second; 0 if size hint unset */
    size_t iterations;       /* Number of iterations */
} BenchResult;

/* Benchmark function signature */
typedef void (*BenchFunc)(void* ctx);

/* Optional payload size hint for throughput reporting.  Set to 0 if
 * the benchmark doesn't process a contiguous byte payload. */
void bench_set_payload_size_kb(double size_kb);

/* Run benchmark with N iterations, return statistics.
 *
 * Performs one warm-up call before timing, then runs `iterations`
 * timed invocations.  Per-iteration wall and CPU times are sampled;
 * peak RSS is the maximum observed across the run. */
BenchResult bench_run(const char* name,
                      BenchFunc fn,
                      void* ctx,
                      size_t iterations);

/* Print single result (one line). */
void bench_print_result(const BenchResult* result);

/* Print comparison table header. */
void bench_print_header(const char* title);

/* Print side-by-side comparison. */
void bench_print_comparison(const BenchResult* taurus,
                            const BenchResult* competitor,
                            const char* competitor_name);

/* Print category summary. */
void bench_print_summary(const char* category,
                         const BenchResult* results,
                         size_t count);

/* Serialize results to a JSON file.  Includes every field of
 * BenchResult so downstream tooling can compare across runs. */
void bench_write_json(const char* library,
                      const BenchResult* results,
                      size_t count,
                      const char* filename);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_BENCH_BENCHMARK_H */
