#ifndef TAURUS_BENCH_BENCHMARK_H
#define TAURUS_BENCH_BENCHMARK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Benchmark result with statistics */
typedef struct {
    const char* name;        /* Benchmark name */
    double mean_us;          /* Mean time in microseconds */
    double stddev_us;        /* Standard deviation */
    double min_us;           /* Fastest run */
    double max_us;           /* Slowest run */
    size_t iterations;       /* Number of iterations */
} BenchResult;

/* Benchmark function signature */
typedef void (*BenchFunc)(void* ctx);

/* Run benchmark with N iterations, return statistics */
BenchResult bench_run(const char* name,
                      BenchFunc fn,
                      void* ctx,
                      size_t iterations);

/* Print single result */
void bench_print_result(const BenchResult* result);

/* Print comparison table header */
void bench_print_header(const char* title);

/* Print comparison (shows speedup factor) */
void bench_print_comparison(const BenchResult* taurus,
                            const BenchResult* competitor,
                            const char* competitor_name);

/* Print summary statistics */
void bench_print_summary(const char* category,
                         const BenchResult* results,
                         size_t count);

/* Write results to JSON file */
void bench_write_json(const char* library,
                      const BenchResult* results,
                      size_t count,
                      const char* filename);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_BENCH_BENCHMARK_H */