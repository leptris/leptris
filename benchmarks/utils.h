/**
 * Benchmark Utilities for Taurus
 * High-resolution timing and statistical analysis
 */

#ifndef TAURUS_BENCHMARK_UTILS_H
#define TAURUS_BENCHMARK_UTILS_H

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
 * Print comparison results between Taurus and competitor
 *
 * @param name Test name
 * @param taurus Taurus statistics
 * @param competitor Competitor statistics
 * @param competitor_name Name of competitor library
 */
void benchmark_print_result(const char* name,
                            benchmark_stats taurus,
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

#endif /* TAURUS_BENCHMARK_UTILS_H */
