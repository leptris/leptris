/**
 * Benchmark Utilities Implementation.
 *
 * clock_gettime / CLOCK_MONOTONIC need _POSIX_C_SOURCE >= 199309L on
 * glibc (Linux).  Defining it there is enough.  macOS exposes them
 * without any feature-test macro and is fine with the same define,
 * but combined with strict POSIX mode its libc hides snprintf — so
 * we pull in <stdio.h> BEFORE the feature-test definition on macOS.
 */

#ifndef _POSIX_C_SOURCE
#  if !defined(__APPLE__) && !defined(__MACH__)
#    define _POSIX_C_SOURCE 199309L
#  endif
#endif

#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Platform-specific timing */
#if defined(__APPLE__)
    #include <mach/mach_time.h>
#elif defined(__linux__)
    #include <time.h>
#else
    #error "Unsupported platform for benchmarking"
#endif

/* Get current time in microseconds */
long benchmark_time_us(void) {
#if defined(__APPLE__)
    static mach_timebase_info_data_t timebase = {0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    uint64_t now = mach_absolute_time();
    /* Convert to nanoseconds, then to microseconds */
    return (long)((now * timebase.numer / timebase.denom) / 1000);

#elif defined(__linux__)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000000L + ts.tv_nsec / 1000);

#endif
}

/* Comparison function for qsort */
static int compare_double(const void* a, const void* b) {
    double diff = *(const double*)a - *(const double*)b;
    return (diff > 0) - (diff < 0);
}

/* Calculate percentile from sorted array */
static double percentile(const double* sorted, size_t count, double p) {
    if (count == 0) return 0.0;
    if (count == 1) return sorted[0];

    double rank = p * (count - 1);
    size_t lower = (size_t)floor(rank);
    size_t upper = (size_t)ceil(rank);

    if (lower == upper) {
        return sorted[lower];
    }

    double weight = rank - lower;
    return sorted[lower] * (1.0 - weight) + sorted[upper] * weight;
}

/* Analyze benchmark samples */
benchmark_stats benchmark_analyze(const double* samples, size_t count) {
    benchmark_stats stats = {0};

    if (count == 0) {
        return stats;
    }

    /* Copy and sort for percentile calculations */
    double* sorted = malloc(count * sizeof(double));
    if (!sorted) {
        return stats;
    }
    memcpy(sorted, samples, count * sizeof(double));
    qsort(sorted, count, sizeof(double), compare_double);

    /* Min and max */
    stats.min = sorted[0];
    stats.max = sorted[count - 1];

    /* Median (50th percentile) */
    stats.median = percentile(sorted, count, 0.50);

    /* 95th percentile */
    stats.p95 = percentile(sorted, count, 0.95);

    /* Mean */
    double sum = 0.0;
    for (size_t i = 0; i < count; i++) {
        sum += samples[i];
    }
    stats.mean = sum / count;

    /* Standard deviation */
    double variance = 0.0;
    for (size_t i = 0; i < count; i++) {
        double diff = samples[i] - stats.mean;
        variance += diff * diff;
    }
    stats.stddev = sqrt(variance / count);

    free(sorted);
    return stats;
}

/* Print table header */
void benchmark_print_header(const char* competitor_name) {
    printf("┌────────────────────────────────┬──────────┬──────────┬──────────┐\n");
    printf("│ %-30s │ Leptris   │ %-8s │ Speedup  │\n", "Operation", competitor_name);
    printf("├────────────────────────────────┼──────────┼──────────┼──────────┤\n");
}

/* Print table footer */
void benchmark_print_footer(void) {
    printf("└────────────────────────────────┴──────────┴──────────┴──────────┘\n");
}

/* Format time value with appropriate unit - use ns for times < 1us */
static void format_time(char* buf, size_t bufsize, double us) {
    if (us <= 0) {
        snprintf(buf, bufsize, "0.00 ns");
    } else if (us < 1.0) {
        snprintf(buf, bufsize, "%.0f ns", us * 1000.0);
    } else if (us < 1000.0) {
        snprintf(buf, bufsize, "%.1f µs", us);
    } else if (us < 1000000.0) {
        snprintf(buf, bufsize, "%.2f ms", us / 1000.0);
    } else {
        snprintf(buf, bufsize, "%.2f s", us / 1000000.0);
    }
}

/* Print benchmark result row */
void benchmark_print_result(const char* name,
                            benchmark_stats leptris,
                            benchmark_stats competitor,
                            const char* competitor_name) {
    char leptris_str[32];
    char competitor_str[32];
    char speedup_str[32];

    (void)competitor_name; /* Unused, name is in header */

    /* Use median for comparison (more robust than mean) */
    format_time(leptris_str, sizeof(leptris_str), leptris.median);
    format_time(competitor_str, sizeof(competitor_str), competitor.median);

    /* Calculate speedup */
    double speedup = competitor.median / leptris.median;
    if (speedup >= 1.0) {
        snprintf(speedup_str, sizeof(speedup_str), "%.2fx ✅", speedup);
    } else {
        snprintf(speedup_str, sizeof(speedup_str), "%.2fx ⚠️", speedup);
    }

    printf("│ %-30s │ %8s │ %8s │ %8s │\n",
           name, leptris_str, competitor_str, speedup_str);
}
