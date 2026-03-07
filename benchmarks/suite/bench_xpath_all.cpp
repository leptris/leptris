/**
 * XPath Benchmarks (All 13 Axes + Functions)
 *
 * Measures XPath performance against libxml2.
 * Target: >= 1.0x vs libxml2 for all axes and functions
 *
 * Tests:
 * 1. child axis
 * 2. parent axis
 * 3. ancestor axis
 * 4. ancestor-or-self axis
 * 5. descendant axis
 * 6. descendant-or-self axis
 * 7. following axis
 * 8. following-sibling axis
 * 9. preceding axis
 * 10. preceding-sibling axis
 * 11. attribute axis
 * 12. namespace axis
 * 13. self axis
 * 14-20. Common functions (count, sum, concat, string, number, boolean, etc.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

// Taurus API (C)
extern "C" {
#include <taurus.h>
}

// libxml2 API (C)
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

// Benchmark utilities
extern "C" {
#include "utils.h"
}

// Quick mode for development (set to 0 for full runs)
#define QUICK_MODE 1

#if QUICK_MODE
#define ITERATIONS 100
#define WARMUP_ITERS 10
#else
#define ITERATIONS 1000
#define WARMUP_ITERS 100
#endif

// Read file into string
static std::string read_file(const char* filename) {
    std::ifstream file(filename);
    if (!file) {
        fprintf(stderr, "Error: Cannot open file: %s\n", filename);
        exit(1);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// ============================================================================
// XPath Test Runner
// ============================================================================

static void run_xpath_axis_test(const char* name,
                                const char* xpath,
                                const char* xml, size_t len) {
    printf("\n=== %s: %s ===\n", name, xpath);

    // Initialize Taurus
    TaurusDocument taurus_doc = taurus_parse_string(xml, len, NULL);
    if (!taurus_doc) {
        printf("  ERROR: Taurus failed to parse\n");
        return;
    }
    TaurusElement taurus_root = taurus_document_root(taurus_doc);

    // Initialize libxml2
    xmlDocPtr libxml2_doc = xmlReadMemory(xml, (int)len, NULL, NULL, 0);
    xmlXPathContextPtr libxml2_ctx = xmlXPathNewContext(libxml2_doc);

    // CRITICAL: Set context node to root element (matching Taurus behavior)
    // Without this, "child::section" would look for document children, not root element children
    xmlNodePtr libxml2_root = xmlDocGetRootElement(libxml2_doc);
    if (libxml2_root) {
        xmlXPathSetContextNode(libxml2_root, libxml2_ctx);
    }

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        TaurusXPathResult taurus_result = taurus_xpath_eval(taurus_doc, taurus_root, xpath);
        if (taurus_result) taurus_xpath_result_free(taurus_result);

        xmlXPathObjectPtr libxml2_result = xmlXPathEvalExpression(
            BAD_CAST xpath, libxml2_ctx);
        if (libxml2_result) xmlXPathFreeObject(libxml2_result);
    }

    // Measure Taurus
    std::vector<double> taurus_times;
    for (int i = 0; i < ITERATIONS; i++) {
        long start = benchmark_time_us();
        TaurusXPathResult result = taurus_xpath_eval(taurus_doc, taurus_root, xpath);
        long end = benchmark_time_us();

        if (result) {
            taurus_times.push_back((double)(end - start));
            taurus_xpath_result_free(result);
        }
    }
    benchmark_stats taurus_stats = benchmark_analyze(taurus_times.data(), taurus_times.size());

    // Measure libxml2
    std::vector<double> libxml2_times;
    for (int i = 0; i < ITERATIONS; i++) {
        long start = benchmark_time_us();
        xmlXPathObjectPtr result = xmlXPathEvalExpression(
            BAD_CAST xpath, libxml2_ctx);
        long end = benchmark_time_us();

        if (result) {
            libxml2_times.push_back((double)(end - start));
            xmlXPathFreeObject(result);
        }
    }
    benchmark_stats libxml2_stats = benchmark_analyze(libxml2_times.data(), libxml2_times.size());

    // Print results
    double speedup = libxml2_stats.median / taurus_stats.median;
    printf("  Taurus:   %8.2f us (median)\n", taurus_stats.median);
    printf("  libxml2:  %8.2f us (median)\n", libxml2_stats.median);
    printf("  Speedup:  %.2fx %s\n", speedup,
           speedup >= 1.0 ? "PASS" : "FAIL");

    // Cleanup
    taurus_document_free(taurus_doc);
    xmlXPathFreeContext(libxml2_ctx);
    xmlFreeDoc(libxml2_doc);
}

// ============================================================================
// Union Test
// ============================================================================

static void run_xpath_union_test(const char* xml, size_t len) {
    printf("\n=== XPath Union Operator ===\n");

    // Initialize Taurus
    TaurusDocument taurus_doc = taurus_parse_string(xml, len, NULL);
    TaurusElement taurus_root = taurus_document_root(taurus_doc);

    // Initialize libxml2
    xmlDocPtr libxml2_doc = xmlReadMemory(xml, (int)len, NULL, NULL, 0);
    xmlXPathContextPtr libxml2_ctx = xmlXPathNewContext(libxml2_doc);

    // Set context node to root element (matching Taurus behavior)
    xmlNodePtr libxml2_root = xmlDocGetRootElement(libxml2_doc);
    if (libxml2_root) {
        xmlXPathSetContextNode(libxml2_root, libxml2_ctx);
    }

    const char* xpath = "//element | //child";

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        TaurusXPathResult taurus_result = taurus_xpath_eval(taurus_doc, taurus_root, xpath);
        if (taurus_result) taurus_xpath_result_free(taurus_result);

        xmlXPathObjectPtr libxml2_result = xmlXPathEvalExpression(
            BAD_CAST xpath, libxml2_ctx);
        if (libxml2_result) xmlXPathFreeObject(libxml2_result);
    }

    // Measure Taurus
    std::vector<double> taurus_times;
    for (int i = 0; i < ITERATIONS; i++) {
        long start = benchmark_time_us();
        TaurusXPathResult result = taurus_xpath_eval(taurus_doc, taurus_root, xpath);
        long end = benchmark_time_us();

        if (result) {
            taurus_times.push_back((double)(end - start));
            taurus_xpath_result_free(result);
        }
    }
    benchmark_stats taurus_stats = benchmark_analyze(taurus_times.data(), taurus_times.size());

    // Measure libxml2
    std::vector<double> libxml2_times;
    for (int i = 0; i < ITERATIONS; i++) {
        long start = benchmark_time_us();
        xmlXPathObjectPtr result = xmlXPathEvalExpression(
            BAD_CAST xpath, libxml2_ctx);
        long end = benchmark_time_us();

        if (result) {
            libxml2_times.push_back((double)(end - start));
            xmlXPathFreeObject(result);
        }
    }
    benchmark_stats libxml2_stats = benchmark_analyze(libxml2_times.data(), libxml2_times.size());

    // Print results
    double speedup = libxml2_stats.median / taurus_stats.median;
    printf("  Expression: %s\n", xpath);
    printf("  Taurus:   %8.2f us (median)\n", taurus_stats.median);
    printf("  libxml2:  %8.2f us (median)\n", libxml2_stats.median);
    printf("  Speedup:  %.2fx %s\n", speedup,
           speedup >= 1.0 ? "PASS" : "FAIL");

    // Cleanup
    taurus_document_free(taurus_doc);
    xmlXPathFreeContext(libxml2_ctx);
    xmlFreeDoc(libxml2_doc);
}

// ============================================================================
// XPath Function Tests
// ============================================================================

static void run_xpath_function_tests(const char* xml, size_t len) {
    // Test common XPath functions
    const char* functions[] = {
        "count(//element)",
        "string(/root)",
        "concat('Hello', ' ', 'World')",
        "number('42')",
        "boolean(//element)",
        "string-length('Hello World')",
        "contains('Hello World', 'World')",
        "starts-with('Hello World', 'Hello')",
        "substring('Hello World', 1, 5)",
        "normalize-space('  Hello  World  ')",
        "translate('Hello', 'elo', 'ELO')",
        "last()",
        "position()",
        "name(/root)",
        "local-name(/root)",
    };

    // Initialize
    TaurusDocument taurus_doc = taurus_parse_string(xml, len, NULL);
    TaurusElement taurus_root = taurus_document_root(taurus_doc);
    xmlDocPtr libxml2_doc = xmlReadMemory(xml, (int)len, NULL, NULL, 0);
    xmlXPathContextPtr libxml2_ctx = xmlXPathNewContext(libxml2_doc);

    // Set context node to root element (matching Taurus behavior)
    xmlNodePtr libxml2_root = xmlDocGetRootElement(libxml2_doc);
    if (libxml2_root) {
        xmlXPathSetContextNode(libxml2_root, libxml2_ctx);
    }

    printf("\n=== XPath Functions (Parse + Eval) ===\n");

    for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
        const char* xpath = functions[i];
        printf("\n  Function: %s\n", xpath);

        // Warmup
        for (int j = 0; j < WARMUP_ITERS / 10; j++) {
            TaurusXPathResult tr = taurus_xpath_eval(taurus_doc, taurus_root, xpath);
            if (tr) taurus_xpath_result_free(tr);

            xmlXPathObjectPtr lr = xmlXPathEvalExpression(
                BAD_CAST xpath, libxml2_ctx);
            if (lr) xmlXPathFreeObject(lr);
        }

        // Measure Taurus (parse + eval each time)
        std::vector<double> taurus_times;
        for (int j = 0; j < ITERATIONS; j++) {
            long start = benchmark_time_us();
            TaurusXPathResult result = taurus_xpath_eval(taurus_doc, taurus_root, xpath);
            long end = benchmark_time_us();

            if (result) {
                taurus_times.push_back((double)(end - start));
                taurus_xpath_result_free(result);
            }
        }

        // Measure libxml2 (parse + eval each time)
        std::vector<double> libxml2_times;
        for (int j = 0; j < ITERATIONS; j++) {
            long start = benchmark_time_us();
            xmlXPathObjectPtr result = xmlXPathEvalExpression(
                BAD_CAST xpath, libxml2_ctx);
            long end = benchmark_time_us();

            if (result) {
                libxml2_times.push_back((double)(end - start));
                xmlXPathFreeObject(result);
            }
        }

        if (taurus_times.empty() || libxml2_times.empty()) {
            printf("    ERROR: Evaluation failed\n");
            continue;
        }

        benchmark_stats taurus_stats = benchmark_analyze(taurus_times.data(), taurus_times.size());
        benchmark_stats libxml2_stats = benchmark_analyze(libxml2_times.data(), libxml2_times.size());

        double speedup = libxml2_stats.median / taurus_stats.median;
        printf("    Taurus:  %6.2f us, libxml2: %6.2f us, Speedup: %.2fx %s\n",
               taurus_stats.median, libxml2_stats.median, speedup,
               speedup >= 1.0 ? "PASS" : "FAIL");
    }

    // Now test with PRE-COMPILED expressions (true evaluation performance)
    printf("\n=== XPath Functions (Compiled - True Eval Performance) ===\n");

    // Pre-compile all expressions for Taurus
    std::vector<TaurusXPathCompiled> compiled_exprs;
    for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
        TaurusXPathCompiled compiled = taurus_xpath_compile(functions[i]);
        compiled_exprs.push_back(compiled);
    }

    // Pre-compile all expressions for libxml2
    std::vector<xmlXPathCompExprPtr> libxml2_compiled;
    for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
        xmlXPathCompExprPtr compiled = xmlXPathCompile(BAD_CAST functions[i]);
        libxml2_compiled.push_back(compiled);
    }

    for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
        const char* xpath = functions[i];
        TaurusXPathCompiled compiled = compiled_exprs[i];
        xmlXPathCompExprPtr libxml2_comp = libxml2_compiled[i];

        printf("\n  Function: %s\n", xpath);

        if (!compiled || !libxml2_comp) {
            printf("    ERROR: Compilation failed\n");
            continue;
        }

        // Warmup
        for (int j = 0; j < WARMUP_ITERS / 10; j++) {
            TaurusXPathResult tr = taurus_xpath_eval_compiled(taurus_doc, taurus_root, compiled);
            if (tr) taurus_xpath_result_free(tr);

            xmlXPathObjectPtr lr = xmlXPathCompiledEval(libxml2_comp, libxml2_ctx);
            if (lr) xmlXPathFreeObject(lr);
        }

        // Measure Taurus (compiled - just evaluation)
        std::vector<double> taurus_times;
        for (int j = 0; j < ITERATIONS; j++) {
            long start = benchmark_time_us();
            TaurusXPathResult result = taurus_xpath_eval_compiled(taurus_doc, taurus_root, compiled);
            long end = benchmark_time_us();

            if (result) {
                taurus_times.push_back((double)(end - start));
                taurus_xpath_result_free(result);
            }
        }

        // Measure libxml2 (compiled - just evaluation)
        std::vector<double> libxml2_times;
        for (int j = 0; j < ITERATIONS; j++) {
            long start = benchmark_time_us();
            xmlXPathObjectPtr result = xmlXPathCompiledEval(libxml2_comp, libxml2_ctx);
            long end = benchmark_time_us();

            if (result) {
                libxml2_times.push_back((double)(end - start));
                xmlXPathFreeObject(result);
            }
        }

        if (taurus_times.empty() || libxml2_times.empty()) {
            printf("    ERROR: Evaluation failed\n");
            continue;
        }

        benchmark_stats taurus_stats = benchmark_analyze(taurus_times.data(), taurus_times.size());
        benchmark_stats libxml2_stats = benchmark_analyze(libxml2_times.data(), libxml2_times.size());

        double speedup = libxml2_stats.median / taurus_stats.median;
        printf("    Taurus:  %6.2f us, libxml2: %6.2f us, Speedup: %.2fx %s\n",
               taurus_stats.median, libxml2_stats.median, speedup,
               speedup >= 1.0 ? "PASS" : "FAIL");
    }

    // Cleanup compiled expressions
    for (size_t i = 0; i < compiled_exprs.size(); i++) {
        if (compiled_exprs[i]) taurus_xpath_compiled_free(compiled_exprs[i]);
    }
    for (size_t i = 0; i < libxml2_compiled.size(); i++) {
        if (libxml2_compiled[i]) xmlXPathFreeCompExpr(libxml2_compiled[i]);
    }

    // Cleanup
    taurus_document_free(taurus_doc);
    xmlXPathFreeContext(libxml2_ctx);
    xmlFreeDoc(libxml2_doc);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    const char* data_dir = (argc > 1) ? argv[1] : "data";

    // Initialize libxml2
    xmlInitParser();

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║          XPath Benchmarks (20 tests)                      ║\n");
    printf("║  Target: >= 1.0x vs libxml2 for all axes and functions    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // Load test files
    std::string medium_xml = read_file((std::string(data_dir) + "/medium.xml").c_str());
    std::string wide_xml = read_file((std::string(data_dir) + "/wide_1000.xml").c_str());
    std::string deep_xml = read_file((std::string(data_dir) + "/deep_100.xml").c_str());

    // 13 Axes Tests
    printf("\n--- XPath Axes (13 tests) ---\n");

    // Test child axis (using 'section' which exists in medium.xml)
    run_xpath_axis_test("child axis", "child::section", medium_xml.c_str(), medium_xml.length());

    // Test parent axis (using 'item' which exists in medium.xml)
    run_xpath_axis_test("parent axis", "//name/parent::*", medium_xml.c_str(), medium_xml.length());

    // Test ancestor axis (using deep_100.xml with level tags)
    run_xpath_axis_test("ancestor axis", "//content/ancestor::*", deep_xml.c_str(), deep_xml.length());

    // Test ancestor-or-self axis
    run_xpath_axis_test("ancestor-or-self", "//content/ancestor-or-self::*", deep_xml.c_str(), deep_xml.length());

    // Test descendant axis
    run_xpath_axis_test("descendant axis", "/root/descendant::*", medium_xml.c_str(), medium_xml.length());

    // Test descendant-or-self axis
    run_xpath_axis_test("descendant-or-self", "/root/descendant-or-self::*", medium_xml.c_str(), medium_xml.length());

    // Test following axis
    run_xpath_axis_test("following axis", "//item[1]/following::*", medium_xml.c_str(), medium_xml.length());

    // Test following-sibling axis (using wide_1000.xml with item tags)
    run_xpath_axis_test("following-sibling", "//item[1]/following-sibling::*", wide_xml.c_str(), wide_xml.length());

    // Test preceding axis
    run_xpath_axis_test("preceding axis", "//item[last()]/preceding::*", medium_xml.c_str(), medium_xml.length());

    // Test preceding-sibling axis
    run_xpath_axis_test("preceding-sibling", "//item[last()]/preceding-sibling::*", wide_xml.c_str(), wide_xml.length());

    // Test attribute axis
    run_xpath_axis_test("attribute axis", "//item/attribute::*", medium_xml.c_str(), medium_xml.length());

    // Test self axis
    run_xpath_axis_test("self axis", "//item/self::*", medium_xml.c_str(), medium_xml.length());

    // Test namespace axis (deprecated but required)
    run_xpath_axis_test("namespace axis", "//item/namespace::*", medium_xml.c_str(), medium_xml.length());

    // Union test
    run_xpath_union_test(medium_xml.c_str(), medium_xml.length());

    // Function tests
    run_xpath_function_tests(medium_xml.c_str(), medium_xml.length());

    // Cleanup
    xmlCleanupParser();

    printf("\n");
    printf("XPath Benchmarks Complete\n");
    printf("\n");

    return 0;
}
