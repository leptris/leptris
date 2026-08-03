/* Node Creation Performance Benchmark
 * Compares pool-based bulk allocation vs regular malloc-based allocation
 */

#include "taurus.h"
#include "taurus/memory/pool.h"
#include "taurus/dom/comment.h"
#include "taurus/dom/cdata.h"
#include "taurus/dom/pi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define ITERATIONS 10000

static double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
}

/* Benchmark Comment creation */
static void bench_comment_creation(void) {
    const char* content = "Test comment with some content";
    size_t len = strlen(content);

    /* With pool (fast path) */
    TaurusMemoryPool* pool = taurus_pool_create();
    double start = get_time_us();
    for (int i = 0; i < ITERATIONS; i++) {
        taurus_comment_create_fast(content, len, pool);
    }
    double end = get_time_us();
    double fast_time = (end - start) / ITERATIONS;
    taurus_pool_destroy(pool);

    /* Without pool (regular path) */
    start = get_time_us();
    for (int i = 0; i < ITERATIONS; i++) {
        TaurusCommentNode* node = taurus_comment_create(content);
        taurus_comment_free(node);
    }
    end = get_time_us();
    double regular_time = (end - start) / ITERATIONS;

    printf("  \"comment\": {\n");
    printf("    \"regular_us\": %.4f,\n", regular_time);
    printf("    \"fast_us\": %.4f,\n", fast_time);
    printf("    \"speedup\": %.2fx\n", regular_time / fast_time);
    printf("  }");
}

/* Benchmark CDATA creation */
static void bench_cdata_creation(void) {
    const char* content = "Raw data with <special> &characters& preserved";
    size_t len = strlen(content);

    /* With pool (fast path) */
    TaurusMemoryPool* pool = taurus_pool_create();
    double start = get_time_us();
    for (int i = 0; i < ITERATIONS; i++) {
        taurus_cdata_create_fast(content, len, pool);
    }
    double end = get_time_us();
    double fast_time = (end - start) / ITERATIONS;
    taurus_pool_destroy(pool);

    /* Without pool (regular path) */
    start = get_time_us();
    for (int i = 0; i < ITERATIONS; i++) {
        TaurusCDATANode* node = taurus_cdata_create(content);
        taurus_cdata_free(node);
    }
    end = get_time_us();
    double regular_time = (end - start) / ITERATIONS;

    printf(",\n  \"cdata\": {\n");
    printf("    \"regular_us\": %.4f,\n", regular_time);
    printf("    \"fast_us\": %.4f,\n", fast_time);
    printf("    \"speedup\": %.2fx\n", regular_time / fast_time);
    printf("  }");
}

/* Benchmark PI creation */
static void bench_pi_creation(void) {
    const char* target = "xml-stylesheet";
    const char* data = "type=\"text/xsl\" href=\"style.xsl\"";
    size_t target_len = strlen(target);
    size_t data_len = strlen(data);

    /* With pool (fast path) */
    TaurusMemoryPool* pool = taurus_pool_create();
    double start = get_time_us();
    for (int i = 0; i < ITERATIONS; i++) {
        taurus_pi_create_fast(target, target_len, data, data_len, pool);
    }
    double end = get_time_us();
    double fast_time = (end - start) / ITERATIONS;
    taurus_pool_destroy(pool);

    /* Without pool (regular path) */
    start = get_time_us();
    for (int i = 0; i < ITERATIONS; i++) {
        TaurusPINode* node = taurus_pi_create(target, data);
        taurus_pi_free(node);
    }
    end = get_time_us();
    double regular_time = (end - start) / ITERATIONS;

    printf(",\n  \"pi\": {\n");
    printf("    \"regular_us\": %.4f,\n", regular_time);
    printf("    \"fast_us\": %.4f,\n", fast_time);
    printf("    \"speedup\": %.2fx\n", regular_time / fast_time);
    printf("  }");
}

int main(void) {
    printf("{\n");
    printf("  \"benchmark\": \"node_creation\",\n");
    printf("  \"iterations\": %d,\n", ITERATIONS);
    printf("  \"results\": {\n");

    bench_comment_creation();
    bench_cdata_creation();
    bench_pi_creation();

    printf("\n  }\n}\n");
    return 0;
}
