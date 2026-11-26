/* Node Creation Performance Benchmark
 * Compares pool-based bulk allocation vs regular malloc-based allocation
 */

#include "leptris.h"
#include "leptris/memory/pool.h"
#include "leptris/dom/comment.h"
#include "leptris/dom/cdata.h"
#include "leptris/dom/pi.h"
#include "../common/bench_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ITERATIONS 10000

/* Benchmark Comment creation */
static void bench_comment_creation(void) {
    const char* content = "Test comment with some content";
    size_t len = strlen(content);

    /* With pool (fast path) */
    LeptrisMemoryPool* pool = leptris_pool_create();
    double start = bench_wall_real_us();
    for (int i = 0; i < ITERATIONS; i++) {
        leptris_comment_create_fast(content, len, pool);
    }
    double end = bench_wall_real_us();
    double fast_time = (end - start) / ITERATIONS;
    leptris_pool_destroy(pool);

    /* Without pool (regular path) */
    start = bench_wall_real_us();
    for (int i = 0; i < ITERATIONS; i++) {
        LeptrisCommentNode* node = leptris_comment_create(content);
        leptris_comment_free(node);
    }
    end = bench_wall_real_us();
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
    LeptrisMemoryPool* pool = leptris_pool_create();
    double start = bench_wall_real_us();
    for (int i = 0; i < ITERATIONS; i++) {
        leptris_cdata_create_fast(content, len, pool);
    }
    double end = bench_wall_real_us();
    double fast_time = (end - start) / ITERATIONS;
    leptris_pool_destroy(pool);

    /* Without pool (regular path) */
    start = bench_wall_real_us();
    for (int i = 0; i < ITERATIONS; i++) {
        LeptrisCDATANode* node = leptris_cdata_create(content);
        leptris_cdata_free(node);
    }
    end = bench_wall_real_us();
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
    LeptrisMemoryPool* pool = leptris_pool_create();
    double start = bench_wall_real_us();
    for (int i = 0; i < ITERATIONS; i++) {
        leptris_pi_create_fast(target, target_len, data, data_len, pool);
    }
    double end = bench_wall_real_us();
    double fast_time = (end - start) / ITERATIONS;
    leptris_pool_destroy(pool);

    /* Without pool (regular path) */
    start = bench_wall_real_us();
    for (int i = 0; i < ITERATIONS; i++) {
        LeptrisPINode* node = leptris_pi_create(target, data);
        leptris_pi_free(node);
    }
    end = bench_wall_real_us();
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
