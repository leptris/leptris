/* bench_time.h — the benchmark tree's single portable timing seam.
 *
 * Every benchmark binary reads the clock through THIS header; no
 * benchmark source includes <sys/time.h>, <sys/resource.h> or
 * <mach/mach_time.h> directly anymore. That per-file spread is what
 * kept the windows PGO legs from building the battery (issue #2191):
 * MSVC provides none of the POSIX timing headers.
 *
 * What the seam provides (all double, all fixed units):
 *   bench_wall_us()         monotonic wall clock, microseconds
 *                           (CLOCK_MONOTONIC / mach_absolute_time /
 *                           QueryPerformanceCounter — never steps)
 *   bench_wall_real_us()    real-time wall clock, microseconds
 *                           (gettimeofday / GetSystemTimePreciseAsFileTime)
 *   bench_cpu_us()          process CPU time consumed, microseconds
 *                           (CLOCK_PROCESS_CPUTIME_ID, clock() fallback /
 *                           GetProcessTimes user+kernel)
 *   bench_cpu_split_us()    same, split into user and system microseconds
 *                           (getrusage ru_utime/ru_stime / GetProcessTimes)
 *   bench_rss_peak_kb()     peak resident set size, KiB, process high-water
 *                           (getrusage ru_maxrss, unit-normalized /
 *                           GetProcessMemoryInfo PeakWorkingSetSize)
 *   bench_rss_current_kb()  current resident set size, KiB
 *                           (Linux /proc VmRSS + macOS task_info /
 *                           GetProcessMemoryInfo WorkingSetSize)
 *
 * Comparability rule: competitor binaries (libxml2/pugixml sides) go
 * through the same seam, so a leptris-vs-competitor row on one machine
 * is always the same clock on both sides.
 *
 * Header-only on purpose: the twins/ A/B probes are compiled by hand
 * (single cc/cc++ invocation, no CMake), so the seam must ride the
 * source file. All functions are static inline — translation units
 * that need only one of the six pay nothing for the rest.
 *
 * Feature-test macros (glibc): the tree builds with -std=c11
 * (C_EXTENSIONS OFF), which defines __STRICT_ANSI__ and makes glibc
 * hide clock_gettime/gettimeofday/getrusage behind _DEFAULT_SOURCE.
 * The macro must be set BEFORE glibc's first header. For the
 * CMake-built tree benchmarks/CMakeLists.txt carries it
 * (add_compile_definitions) — bench sources include leptris.h ahead
 * of this header, so a define here cannot win that race. For
 * HAND-COMPILED TUs (the twins/ A/B probes: a bare cc/cc++ line with
 * no CMake), pass -D_DEFAULT_SOURCE on the command line — or include
 * this header before leptris.h; the guarded define below covers only
 * the include-first case. Apple and Windows need nothing.
 *
 * Windows: PSAPI_VERSION 2 before <psapi.h> so the process-memory
 * calls resolve to the K32* names — those live in kernel32.dll, which
 * every image links by default; no psapi.lib dependency.
 */

#ifndef LEPTRIS_BENCH_TIME_H
#define LEPTRIS_BENCH_TIME_H

/* MSVC's C mode predates C99 inline conformance; __inline is its
 * keyword there (and a working extension everywhere else). */
#if defined(_MSC_VER) && !defined(__cplusplus)
#define BENCH_TIME_INLINE static __inline
#else
#define BENCH_TIME_INLINE static inline
#endif

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#  define NOMINMAX
#  endif
#  ifndef PSAPI_VERSION
#  define PSAPI_VERSION 2
#  endif
#  include <windows.h>
#  include <psapi.h>
#else
   /* Must precede every system include — see the comment above. */
#  if !defined(__APPLE__) && !defined(_DEFAULT_SOURCE) && \
      !defined(_GNU_SOURCE) && !defined(_POSIX_C_SOURCE)
#    define _DEFAULT_SOURCE 1
#  endif
#  include <sys/time.h>
#  include <time.h>
#  include <stdio.h>
#  include <string.h>
#  include <sys/resource.h>
#  if defined(__APPLE__) && defined(__MACH__)
#    include <mach/mach.h>
#    include <mach/mach_time.h>
#  endif
#endif

/* Monotonic wall clock, microseconds since an arbitrary fixed epoch. */
BENCH_TIME_INLINE double bench_wall_us(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000000.0 / (double)freq.QuadPart;
#elif defined(__APPLE__) && defined(__MACH__)
    static mach_timebase_info_data_t timebase = {0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    uint64_t now = mach_absolute_time();
    /* Convert to nanoseconds, then to microseconds. */
    return (double)(now * timebase.numer / timebase.denom) / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000000.0 + (double)ts.tv_nsec / 1000.0;
#endif
}

/* Real-time (time-of-day) wall clock, microseconds. */
BENCH_TIME_INLINE double bench_wall_real_us(void) {
#if defined(_WIN32)
    FILETIME ft;
    ULARGE_INTEGER u;
    GetSystemTimePreciseAsFileTime(&ft);
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    /* FILETIME ticks are 100ns since 1601-01-01. */
    return (double)u.QuadPart / 10.0;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000000.0 + (double)tv.tv_usec;
#endif
}

/* Process CPU time consumed, microseconds.  User + system.
 *
 * clock_gettime(CLOCK_PROCESS_CPUTIME_ID) is available on Linux and
 * macOS 10.12+.  Falls back to clock() (which has CLOCKS_PER_SEC
 * granularity and includes wait time on some platforms) if the
 * POSIX timer is missing. */
BENCH_TIME_INLINE double bench_cpu_us(void) {
#if defined(_WIN32)
    FILETIME create, exit, kernel, user;
    ULARGE_INTEGER k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &create, &exit,
                         &kernel, &user)) {
        return 0.0;
    }
    k.LowPart = kernel.dwLowDateTime;  k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;    u.HighPart = user.dwHighDateTime;
    return (double)(k.QuadPart + u.QuadPart) / 10.0;
#else
#if defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0 && \
    defined(_POSIX_CPUTIME) && _POSIX_CPUTIME > 0
    struct timespec ts;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0) {
        return (double)ts.tv_sec * 1000000.0 + (double)ts.tv_nsec / 1000.0;
    }
#endif
    return (double)clock() * (1000000.0 / CLOCKS_PER_SEC);
#endif
}

/* Cumulative user / system CPU time, microseconds, split.  Callers
 * subtract two snapshots to get the split consumed by a workload
 * (bench_matrix's user_ms/sys_ms rows). */
typedef struct BenchCpuSplit {
    double user_us;
    double sys_us;
} BenchCpuSplit;

BENCH_TIME_INLINE void bench_cpu_split_us(BenchCpuSplit* out) {
    out->user_us = 0.0;
    out->sys_us = 0.0;
#if defined(_WIN32)
    FILETIME create, exit, kernel, user;
    ULARGE_INTEGER k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &create, &exit,
                         &kernel, &user)) {
        return;
    }
    k.LowPart = kernel.dwLowDateTime;  k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;    u.HighPart = user.dwHighDateTime;
    out->sys_us = (double)k.QuadPart / 10.0;
    out->user_us = (double)u.QuadPart / 10.0;
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return;
    out->user_us = (double)ru.ru_utime.tv_sec * 1000000.0 +
                   (double)ru.ru_utime.tv_usec;
    out->sys_us = (double)ru.ru_stime.tv_sec * 1000000.0 +
                  (double)ru.ru_stime.tv_usec;
#endif
}

/* Peak resident set size in kilobytes.
 *
 * getrusage(RUSAGE_SELF).ru_maxrss is the portable POSIX answer.
 * Units differ by platform: kilobytes on Linux, bytes on macOS/BSD.
 * Normalize to KiB.  This is a high-water mark for the process; the
 * benchmark harness reads it after each iteration and keeps the
 * max, so the reported value reflects the heaviest single call.
 *
 * Windows: PeakWorkingSetSize is bytes — the same /1024 normalization. */
BENCH_TIME_INLINE double bench_rss_peak_kb(void) {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return 0.0;
    }
    return (double)pmc.PeakWorkingSetSize / 1024.0;
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 0.0;
#if defined(__APPLE__) && defined(__MACH__)
    return (double)ru.ru_maxrss / 1024.0;  /* bytes -> KiB */
#else
    return (double)ru.ru_maxrss;           /* already KiB */
#endif
#endif
}

/* Current resident set size in kilobytes (not the high-water mark).
 *
 * POSIX has no single call for this — Linux reads VmRSS from
 * /proc/self/status, macOS asks the mach task_info interface, both
 * reporting bytes/VmRSS-kilobytes normalized here to KiB.
 * Windows: WorkingSetSize (bytes) / 1024. */
BENCH_TIME_INLINE double bench_rss_current_kb(void) {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return 0.0;
    }
    return (double)pmc.WorkingSetSize / 1024.0;
#elif defined(__APPLE__) && defined(__MACH__)
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &count) != KERN_SUCCESS) {
        return 0.0;
    }
    return (double)info.resident_size / 1024.0;
#else
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return 0.0;
    char line[256];
    double kb = 0.0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            (void)sscanf(line + 6, "%lf", &kb);
            break;
        }
    }
    fclose(f);
    return kb;
#endif
}

#endif /* LEPTRIS_BENCH_TIME_H */
