#ifndef LEPTRIS_BENCH_TEST_DATA_H
#define LEPTRIS_BENCH_TEST_DATA_H

#include <stddef.h>

/* Small XML (~1KB) - Micro-benchmarks */
extern const char* BENCH_XML_SMALL;
extern const size_t BENCH_XML_SMALL_LEN;

/* Medium XML (~10KB) - Realistic documents */
extern const char* BENCH_XML_MEDIUM;
extern const size_t BENCH_XML_MEDIUM_LEN;

/* Large XML (~100KB) - Stress testing */
extern const char* BENCH_XML_LARGE;
extern const size_t BENCH_XML_LARGE_LEN;

#endif /* LEPTRIS_BENCH_TEST_DATA_H */