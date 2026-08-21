/**
 * Benchmark Utilities for Leptris
 * High-resolution timing and statistical analysis
 */

#ifndef LEPTRIS_BENCHMARK_UTILS_H
#define LEPTRIS_BENCHMARK_UTILS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get current time in microseconds
 * Uses high-resolution monotonic clock
 */
long benchmark_time_us(void);

/**
 * Statistical results from benchmark measurements
 */
typedef struct {
    double median;   /* Median (50th percentile) - robust to outliers */
    double p95;      /* 95th percentile - worst-case analysis */
    double mean;     /* Arithmetic mean */
    double stddev;   /* Standard deviation */
    double min;      /* Minimum value */
    double max;      /* Maximum value */
} benchmark_stats;

/**
 * Analyze array of timing samples
 * Computes all statistical measures
 *
 * @param samples Array of timing measurements (microseconds)
 * @param count Number of samples
 * @return Statistical analysis results
 */
benchmark_stats benchmark_analyze(const double* samples, size_t count);

/**
 * Print comparison results between Leptris and competitor
 *
 * @param name Test name
 * @param leptris Leptris statistics
 * @param competitor Competitor statistics
 * @param competitor_name Name of competitor library
 */
void benchmark_print_result(const char* name,
                            benchmark_stats leptris,
                            benchmark_stats competitor,
                            const char* competitor_name);

/**
 * Print benchmark table header
 */
void benchmark_print_header(const char* competitor_name);

/**
 * Print benchmark table footer
 */
void benchmark_print_footer(void);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_BENCHMARK_UTILS_H */
