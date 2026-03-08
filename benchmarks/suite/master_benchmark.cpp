/**
 * Taurus Master Benchmark Suite
 *
 * Unified benchmark program comparing Taurus, pugixml, and libxml2
 * across all operations: parsing, writing, DOM operations, and XPath.
 *
 * Output formats:
 * - JSON: Machine-parseable for CI/CD analysis
 * - Table: Human-readable console output
 *
 * Usage:
 *   ./master_benchmark                    # Run all tests with table output
 *   ./master_benchmark --json             # JSON output
 *   ./master_benchmark --quick            # Quick mode (fewer iterations)
 *   ./master_benchmark --category reader  # Run specific category
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <map>
#include <cmath>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <chrono>

// Taurus API (C)
extern "C" {
#include <taurus.h>
#include <taurus/writer.h>
}

// pugixml API (C++)
#include <pugixml.hpp>

// libxml2 API (C)
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlwriter.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

// ============================================================================
// Configuration
// ============================================================================

static bool g_json_output = false;
static bool g_quick_mode = true;  // Default to quick for development

#if 1 // Quick mode defaults
#define ITERATIONS_SMALL 100
#define ITERATIONS_MEDIUM 20
#define ITERATIONS_LARGE 5
#else
#define ITERATIONS_SMALL 1000
#define ITERATIONS_MEDIUM 100
#define ITERATIONS_LARGE 20
#endif

// ============================================================================
// Test Result Structures
// ============================================================================

struct BenchmarkResult {
    std::string category;
    std::string test_name;
    std::string library;
    std::string mode;
    double time_us;        // median time in microseconds
    double time_p95_us;    // 95th percentile
    size_t bytes_processed;
    bool success;
    std::string error_msg;

    // Calculated
    double throughput_mbps;  // MB/s
};

struct CategoryResults {
    std::string category_name;
    std::vector<BenchmarkResult> results;
};

static std::vector<CategoryResults> g_all_results;

// ============================================================================
// Utility Functions
// ============================================================================

static long long get_time_ns() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

static std::string generate_xml(int elements, int depth, int attrs) {
    std::stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root>\n";
    for (int i = 0; i < elements; i++) {
        for (int d = 0; d < depth; d++) ss << "  ";
        ss << "<item";
        for (int a = 0; a < attrs; a++) ss << " attr" << a << "=\"val" << a << "\"";
        ss << ">content" << i << "</item>\n";
    }
    ss << "</root>";
    return ss.str();
}

static std::string generate_deep_xml(int depth) {
    std::stringstream ss;
    ss << "<?xml version=\"1.0\"?><root>";
    for (int i = 0; i < depth; i++) ss << "<level>";
    ss << "deep";
    for (int i = 0; i < depth; i++) ss << "</level>";
    ss << "</root>";
    return ss.str();
}

static void calculate_stats(std::vector<double>& times, double& median, double& p95) {
    if (times.empty()) { median = p95 = 0; return; }
    std::sort(times.begin(), times.end());
    size_t n = times.size();
    median = times[n / 2];
    p95 = times[(size_t)(n * 0.95)];
}

// ============================================================================
// READER BENCHMARKS
// ============================================================================

static CategoryResults benchmark_reader_modes() {
    CategoryResults cat;
    cat.category_name = "reader_modes";

    // Test data
    struct TestCase {
        std::string name;
        std::string xml;
        int iterations;
    };
    std::vector<TestCase> tests = {
        {"small_10elem", generate_xml(10, 1, 2), ITERATIONS_SMALL},
        {"medium_100elem", generate_xml(100, 2, 5), ITERATIONS_MEDIUM},
        {"attrs_20", generate_xml(50, 1, 20), ITERATIONS_MEDIUM},
        {"deep_50", generate_deep_xml(50), ITERATIONS_MEDIUM},
        {"wide_500", generate_xml(500, 1, 1), ITERATIONS_MEDIUM},
    };

    for (const auto& tc : tests) {
        const char* xml = tc.xml.c_str();
        size_t len = tc.xml.size();
        std::vector<double> times;
        double median, p95;

        // === Taurus (copy) ===
        times.clear();
        for (int i = 0; i < tc.iterations; i++) {
            long long start = get_time_ns();
            TaurusDocument doc = taurus_parse_string(xml, len, NULL);
            long long end = get_time_ns();
            if (doc) {
                taurus_document_free(doc);
                times.push_back((end - start) / 1000.0);
            }
        }
        calculate_stats(times, median, p95);
        cat.results.push_back({"reader", tc.name, "taurus", "copy", median, p95, len, true, ""});

        // === Taurus (inplace) ===
        char* buf = (char*)malloc(len + 1);
        times.clear();
        for (int i = 0; i < tc.iterations; i++) {
            memcpy(buf, xml, len + 1);
            long long start = get_time_ns();
            TaurusDocument doc = taurus_parse_string_inplace(buf, len, NULL);
            long long end = get_time_ns();
            if (doc) {
                taurus_document_free(doc);
                times.push_back((end - start) / 1000.0);
            }
        }
        free(buf);
        calculate_stats(times, median, p95);
        cat.results.push_back({"reader", tc.name, "taurus", "inplace", median, p95, len, true, ""});

        // === pugixml (default) ===
        times.clear();
        for (int i = 0; i < tc.iterations; i++) {
            long long start = get_time_ns();
            pugi::xml_document doc;
            doc.load_buffer(xml, len, pugi::parse_default);
            long long end = get_time_ns();
            times.push_back((end - start) / 1000.0);
        }
        calculate_stats(times, median, p95);
        cat.results.push_back({"reader", tc.name, "pugixml", "default", median, p95, len, true, ""});

        // === pugixml (minimal) ===
        times.clear();
        for (int i = 0; i < tc.iterations; i++) {
            long long start = get_time_ns();
            pugi::xml_document doc;
            doc.load_buffer(xml, len, pugi::parse_minimal);
            long long end = get_time_ns();
            times.push_back((end - start) / 1000.0);
        }
        calculate_stats(times, median, p95);
        cat.results.push_back({"reader", tc.name, "pugixml", "minimal", median, p95, len, true, ""});

        // === libxml2 (default) ===
        times.clear();
        for (int i = 0; i < tc.iterations; i++) {
            long long start = get_time_ns();
            xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL, 0);
            long long end = get_time_ns();
            if (doc) {
                xmlFreeDoc(doc);
                times.push_back((end - start) / 1000.0);
            }
        }
        calculate_stats(times, median, p95);
        cat.results.push_back({"reader", tc.name, "libxml2", "default", median, p95, len, true, ""});

        // === libxml2 (NOBLANKS) ===
        times.clear();
        for (int i = 0; i < tc.iterations; i++) {
            long long start = get_time_ns();
            xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL, XML_PARSE_NOBLANKS);
            long long end = get_time_ns();
            if (doc) {
                xmlFreeDoc(doc);
                times.push_back((end - start) / 1000.0);
            }
        }
        calculate_stats(times, median, p95);
        cat.results.push_back({"reader", tc.name, "libxml2", "NOBLANKS", median, p95, len, true, ""});
    }

    return cat;
}

// ============================================================================
// WRITER BENCHMARKS
// ============================================================================

// Memory buffer for output
struct MemBuffer {
    char* data;
    size_t len, capacity;
};

static size_t mem_write_cb(void* ctx, const char* data, size_t len) {
    MemBuffer* buf = (MemBuffer*)ctx;
    if (buf->len + len >= buf->capacity) {
        buf->capacity = (buf->len + len + 1) * 2;
        buf->data = (char*)realloc(buf->data, buf->capacity);
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return len;
}

static void mem_buffer_init(MemBuffer* buf) {
    buf->data = (char*)malloc(65536);
    buf->data[0] = '\0';
    buf->len = 0;
    buf->capacity = 65536;
}

static void mem_buffer_reset(MemBuffer* buf) {
    buf->len = 0;
    if (buf->data) buf->data[0] = '\0';
}

static void mem_buffer_free(MemBuffer* buf) {
    if (buf->data) free(buf->data);
    buf->data = NULL;
}

static CategoryResults benchmark_writer_modes() {
    CategoryResults cat;
    cat.category_name = "writer_modes";

    MemBuffer buf;
    mem_buffer_init(&buf);

    struct TestCase {
        std::string name;
        int elements;
        int attrs;
        int iterations;
    };
    std::vector<TestCase> tests = {
        {"simple_100", 100, 0, ITERATIONS_SMALL},
        {"attrs_5", 100, 5, ITERATIONS_SMALL},
        {"attrs_20", 50, 20, ITERATIONS_MEDIUM},
        {"large_500", 500, 3, ITERATIONS_MEDIUM},
    };

    for (const auto& tc : tests) {
        std::vector<double> times;
        double median, p95;

        // === Taurus StAX (raw) ===
        times.clear();
        for (int i = 0; i < tc.iterations; i++) {
            mem_buffer_reset(&buf);
            long long start = get_time_ns();
            TaurusXMLWriter* w = taurus_writer_create_callback(mem_write_cb, &buf, "UTF-8");
            taurus_writer_start_document(w, NULL, NULL, -1);
            taurus_writer_start_element(w, "root");
            for (int e = 0; e < tc.elements; e++) {
                taurus_writer_start_element(w, "item");
                for (int a = 0; a < tc.attrs; a++) {
                    char n[32], v[32];
                    snprintf(n, 32, "attr%d", a);
                    snprintf(v, 32, "val%d", a);
                    taurus_writer_attribute(w, n, v);
                }
                taurus_writer_characters(w, "content");
                taurus_writer_end_element(w);
            }
            taurus_writer_end_element(w);
            taurus_writer_end_document(w);
            taurus_writer_free(w);
            long long end = get_time_ns();
            times.push_back((end - start) / 1000.0);
        }
        calculate_stats(times, median, p95);
        cat.results.push_back({"writer", tc.name, "taurus", "stax_raw", median, p95, buf.len, true, ""});

        // === Taurus StAX (pretty) ===
        times.clear();
        for (int i = 0; i < tc.iterations; i++) {
            mem_buffer_reset(&buf);
            long long start = get_time_ns();
            TaurusWriterOptions opts = TAURUS_WRITER_OPTIONS_DEFAULT;
            opts.indent = 2;
            opts.pretty_print = 1;
            TaurusXMLWriter* w = taurus_writer_create_callback_ex(mem_write_cb, &buf, &opts);
            taurus_writer_start_document(w, NULL, NULL, -1);
            taurus_writer_start_element(w, "root");
            for (int e = 0; e < tc.elements; e++) {
                taurus_writer_start_element(w, "item");
                for (int a = 0; a < tc.attrs; a++) {
                    char n[32], v[32];
                    snprintf(n, 32, "attr%d", a);
                    snprintf(v, 32, "val%d", a);
                    taurus_writer_attribute(w, n, v);
                }
                taurus_writer_characters(w, "content");
                taurus_writer_end_element(w);
            }
            taurus_writer_end_element(w);
            taurus_writer_end_document(w);
            taurus_writer_free(w);
            long long end = get_time_ns();
            times.push_back((end - start) / 1000.0);
        }
        calculate_stats(times, median, p95);
        cat.results.push_back({"writer", tc.name, "taurus", "stax_pretty", median, p95, buf.len, true, ""});

        // === pugixml (raw) ===
        times.clear();
        for (int i = 0; i < tc.iterations; i++) {
            long long start = get_time_ns();
            pugi::xml_document doc;
            doc.append_child(pugi::node_declaration).append_attribute("version") = "1.0";
            pugi::xml_node root = doc.append_child("root");
            for (int e = 0; e < tc.elements; e++) {
                pugi::xml_node item = root.append_child("item");
                for (int a = 0; a < tc.attrs; a++) {
                    char n[32], v[32];
                    snprintf(n, 32, "attr%d", a);
                    snprintf(v, 32, "val%d", a);
                    item.append_attribute(n) = v;
                }
                item.append_child(pugi::node_pcdata).set_value("content");
            }
            std::stringstream ss;
            doc.save(ss, "", pugi::format_raw);
            long long end = get_time_ns();
            times.push_back((end - start) / 1000.0);
        }
        calculate_stats(times, median, p95);
        cat.results.push_back({"writer", tc.name, "pugixml", "raw", median, p95, 0, true, ""});

        // === pugixml (indent) ===
        times.clear();
        for (int i = 0; i < tc.iterations; i++) {
            long long start = get_time_ns();
            pugi::xml_document doc;
            doc.append_child(pugi::node_declaration).append_attribute("version") = "1.0";
            pugi::xml_node root = doc.append_child("root");
            for (int e = 0; e < tc.elements; e++) {
                pugi::xml_node item = root.append_child("item");
                for (int a = 0; a < tc.attrs; a++) {
                    char n[32], v[32];
                    snprintf(n, 32, "attr%d", a);
                    snprintf(v, 32, "val%d", a);
                    item.append_attribute(n) = v;
                }
                item.append_child(pugi::node_pcdata).set_value("content");
            }
            std::stringstream ss;
            doc.save(ss, "  ", pugi::format_indent);
            long long end = get_time_ns();
            times.push_back((end - start) / 1000.0);
        }
        calculate_stats(times, median, p95);
        cat.results.push_back({"writer", tc.name, "pugixml", "indent", median, p95, 0, true, ""});

        // === libxml2 (streaming) ===
        times.clear();
        for (int i = 0; i < tc.iterations; i++) {
            long long start = get_time_ns();
            xmlBufferPtr xmlBuf = xmlBufferCreate();
            xmlTextWriterPtr writer = xmlNewTextWriterMemory(xmlBuf, 0);
            xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
            xmlTextWriterStartElement(writer, BAD_CAST "root");
            for (int e = 0; e < tc.elements; e++) {
                xmlTextWriterStartElement(writer, BAD_CAST "item");
                for (int a = 0; a < tc.attrs; a++) {
                    char n[32], v[32];
                    snprintf(n, 32, "attr%d", a);
                    snprintf(v, 32, "val%d", a);
                    xmlTextWriterWriteAttribute(writer, BAD_CAST n, BAD_CAST v);
                }
                xmlTextWriterWriteString(writer, BAD_CAST "content");
                xmlTextWriterEndElement(writer);
            }
            xmlTextWriterEndElement(writer);
            xmlTextWriterEndDocument(writer);
            xmlFreeTextWriter(writer);
            xmlBufferFree(xmlBuf);
            long long end = get_time_ns();
            times.push_back((end - start) / 1000.0);
        }
        calculate_stats(times, median, p95);
        cat.results.push_back({"writer", tc.name, "libxml2", "streaming", median, p95, 0, true, ""});

        // === libxml2 (DOM dump) ===
        times.clear();
        for (int i = 0; i < tc.iterations; i++) {
            long long start = get_time_ns();
            xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
            xmlNodePtr root = xmlNewNode(NULL, BAD_CAST "root");
            xmlDocSetRootElement(doc, root);
            for (int e = 0; e < tc.elements; e++) {
                xmlNodePtr item = xmlNewChild(root, NULL, BAD_CAST "item", NULL);
                for (int a = 0; a < tc.attrs; a++) {
                    char n[32], v[32];
                    snprintf(n, 32, "attr%d", a);
                    snprintf(v, 32, "val%d", a);
                    xmlNewProp(item, BAD_CAST n, BAD_CAST v);
                }
                xmlNodeAddContent(item, BAD_CAST "content");
            }
            xmlChar* outBuf;
            int outSize;
            xmlDocDumpMemory(doc, &outBuf, &outSize);
            if (outBuf) xmlFree(outBuf);
            xmlFreeDoc(doc);
            long long end = get_time_ns();
            times.push_back((end - start) / 1000.0);
        }
        calculate_stats(times, median, p95);
        cat.results.push_back({"writer", tc.name, "libxml2", "dom_dump", median, p95, 0, true, ""});
    }

    mem_buffer_free(&buf);
    return cat;
}

// ============================================================================
// DOM OPERATION BENCHMARKS
// ============================================================================

static CategoryResults benchmark_dom_operations() {
    CategoryResults cat;
    cat.category_name = "dom_operations";

    std::string xml = generate_xml(100, 2, 5);
    const char* xml_data = xml.c_str();
    size_t xml_len = xml.size();
    std::vector<double> times;
    double median, p95;

    // === Traversal (count all elements) ===

    // Taurus traversal
    times.clear();
    for (int i = 0; i < ITERATIONS_MEDIUM; i++) {
        TaurusDocument doc = taurus_parse_string(xml_data, xml_len, NULL);
        long long start = get_time_ns();
        TaurusElement root = taurus_document_root(doc);
        int count = 0;
        // Simple traversal using first_child/next_sibling
        TaurusNodeRef node = taurus_node_first_child((TaurusNodeRef)root);
        while (node) {
            count++;
            node = taurus_node_next_sibling(node);
        }
        long long end = get_time_ns();
        taurus_document_free(doc);
        times.push_back((end - start) / 1000.0);
    }
    calculate_stats(times, median, p95);
    cat.results.push_back({"dom", "traversal", "taurus", "first_child_next_sibling", median, p95, xml_len, true, ""});

    // pugixml traversal
    times.clear();
    for (int i = 0; i < ITERATIONS_MEDIUM; i++) {
        pugi::xml_document doc;
        doc.load_buffer(xml_data, xml_len);
        long long start = get_time_ns();
        int count = 0;
        for (pugi::xml_node child = doc.first_child().first_child(); child; child = child.next_sibling()) {
            count++;
        }
        long long end = get_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    calculate_stats(times, median, p95);
    cat.results.push_back({"dom", "traversal", "pugixml", "iterator", median, p95, xml_len, true, ""});

    // libxml2 traversal
    times.clear();
    for (int i = 0; i < ITERATIONS_MEDIUM; i++) {
        xmlDocPtr doc = xmlReadMemory(xml_data, (int)xml_len, NULL, NULL, 0);
        long long start = get_time_ns();
        int count = 0;
        for (xmlNodePtr child = xmlDocGetRootElement(doc)->children; child; child = child->next) {
            count++;
        }
        long long end = get_time_ns();
        xmlFreeDoc(doc);
        times.push_back((end - start) / 1000.0);
    }
    calculate_stats(times, median, p95);
    cat.results.push_back({"dom", "traversal", "libxml2", "children_list", median, p95, xml_len, true, ""});

    // === Attribute Access ===

    // Taurus attribute access
    times.clear();
    for (int i = 0; i < ITERATIONS_MEDIUM; i++) {
        TaurusDocument doc = taurus_parse_string(xml_data, xml_len, NULL);
        TaurusElement root = taurus_document_root(doc);
        TaurusElement first = taurus_element_first_child_any(root);
        long long start = get_time_ns();
        const char* val = taurus_element_attribute(first, "attr0");
        long long end = get_time_ns();
        taurus_document_free(doc);
        times.push_back((end - start) / 1000.0);
    }
    calculate_stats(times, median, p95);
    cat.results.push_back({"dom", "attr_get", "taurus", "get_attribute", median, p95, xml_len, true, ""});

    // pugixml attribute access
    times.clear();
    for (int i = 0; i < ITERATIONS_MEDIUM; i++) {
        pugi::xml_document doc;
        doc.load_buffer(xml_data, xml_len);
        pugi::xml_node root = doc.first_child();
        pugi::xml_node first = root.first_child();
        long long start = get_time_ns();
        pugi::xml_attribute attr = first.attribute("attr0");
        long long end = get_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    calculate_stats(times, median, p95);
    cat.results.push_back({"dom", "attr_get", "pugixml", "attribute", median, p95, xml_len, true, ""});

    // libxml2 attribute access
    times.clear();
    for (int i = 0; i < ITERATIONS_MEDIUM; i++) {
        xmlDocPtr doc = xmlReadMemory(xml_data, (int)xml_len, NULL, NULL, 0);
        xmlNodePtr root = xmlDocGetRootElement(doc);
        xmlNodePtr first = root->children;
        long long start = get_time_ns();
        xmlChar* val = xmlGetProp(first, BAD_CAST "attr0");
        long long end = get_time_ns();
        if (val) xmlFree(val);
        xmlFreeDoc(doc);
        times.push_back((end - start) / 1000.0);
    }
    calculate_stats(times, median, p95);
    cat.results.push_back({"dom", "attr_get", "libxml2", "xmlGetProp", median, p95, xml_len, true, ""});

    return cat;
}

// ============================================================================
// XPATH BENCHMARKS
// ============================================================================

static CategoryResults benchmark_xpath() {
    CategoryResults cat;
    cat.category_name = "xpath";

    std::string xml = generate_xml(100, 2, 5);
    const char* xml_data = xml.c_str();
    size_t xml_len = xml.size();
    std::vector<double> times;
    double median, p95;

    // Simple XPath: /root/item
    const char* simple_path = "/root/item";

    // Taurus XPath
    times.clear();
    for (int i = 0; i < ITERATIONS_MEDIUM; i++) {
        TaurusDocument doc = taurus_parse_string(xml_data, xml_len, NULL);
        TaurusElement root = taurus_document_root(doc);
        long long start = get_time_ns();
        TaurusXPathResult result = taurus_xpath_eval(doc, root, simple_path);
        long long end = get_time_ns();
        taurus_xpath_result_free(result);
        taurus_document_free(doc);
        times.push_back((end - start) / 1000.0);
    }
    calculate_stats(times, median, p95);
    cat.results.push_back({"xpath", "simple_path", "taurus", "xpath_eval", median, p95, xml_len, true, ""});

    // libxml2 XPath
    times.clear();
    for (int i = 0; i < ITERATIONS_MEDIUM; i++) {
        xmlDocPtr doc = xmlReadMemory(xml_data, (int)xml_len, NULL, NULL, 0);
        long long start = get_time_ns();
        xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
        xmlXPathObjectPtr result = xmlXPathEvalExpression(BAD_CAST simple_path, ctx);
        long long end = get_time_ns();
        xmlXPathFreeObject(result);
        xmlXPathFreeContext(ctx);
        xmlFreeDoc(doc);
        times.push_back((end - start) / 1000.0);
    }
    calculate_stats(times, median, p95);
    cat.results.push_back({"xpath", "simple_path", "libxml2", "xmlXPathEval", median, p95, xml_len, true, ""});

    // Complex XPath: /root/item[@attr0='val0']
    const char* complex_path = "/root/item[@attr0='val0']";

    // Taurus XPath complex
    times.clear();
    for (int i = 0; i < ITERATIONS_MEDIUM; i++) {
        TaurusDocument doc = taurus_parse_string(xml_data, xml_len, NULL);
        TaurusElement root = taurus_document_root(doc);
        long long start = get_time_ns();
        TaurusXPathResult result = taurus_xpath_eval(doc, root, complex_path);
        long long end = get_time_ns();
        taurus_xpath_result_free(result);
        taurus_document_free(doc);
        times.push_back((end - start) / 1000.0);
    }
    calculate_stats(times, median, p95);
    cat.results.push_back({"xpath", "predicate_path", "taurus", "xpath_eval", median, p95, xml_len, true, ""});

    // libxml2 XPath complex
    times.clear();
    for (int i = 0; i < ITERATIONS_MEDIUM; i++) {
        xmlDocPtr doc = xmlReadMemory(xml_data, (int)xml_len, NULL, NULL, 0);
        long long start = get_time_ns();
        xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
        xmlXPathObjectPtr result = xmlXPathEvalExpression(BAD_CAST complex_path, ctx);
        long long end = get_time_ns();
        xmlXPathFreeObject(result);
        xmlXPathFreeContext(ctx);
        xmlFreeDoc(doc);
        times.push_back((end - start) / 1000.0);
    }
    calculate_stats(times, median, p95);
    cat.results.push_back({"xpath", "predicate_path", "libxml2", "xmlXPathEval", median, p95, xml_len, true, ""});

    return cat;
}

// ============================================================================
// OUTPUT FORMATTERS
// ============================================================================

static void print_table_header() {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                              TAURUS MASTER BENCHMARK SUITE                                              ║\n");
    printf("║                    Comparing Taurus, pugixml, and libxml2                                              ║\n");
    printf("╚════════════════════════════════════════════════════════════════════════════════════════════════════════╝\n");
}

static void print_category_table(const CategoryResults& cat) {
    printf("\n┌──────────────────────────────────────────────────────────────────────────────────────────────────────────┐\n");
    printf("│ %-104s │\n", (std::string("Category: ") + cat.category_name).c_str());
    printf("├──────────────────┬──────────────────┬─────────────────┬───────────┬───────────┬─────────────────────────┤\n");
    printf("│ Test             │ Library          │ Mode            │ Time (us) │ P95 (us)  │ Speedup vs libxml2      │\n");
    printf("├──────────────────┼──────────────────┼─────────────────┼───────────┼───────────┼─────────────────────────┤\n");

    // Group by test name to calculate speedups
    std::map<std::string, std::vector<const BenchmarkResult*>> by_test;
    for (const auto& r : cat.results) {
        by_test[r.test_name].push_back(&r);
    }

    for (const auto& [test_name, results] : by_test) {
        // Find libxml2 default time for this test
        double libxml2_time = 0;
        for (const auto* r : results) {
            if (r->library == "libxml2" && (r->mode == "default" || r->mode == "streaming" || r->mode == "dom_dump")) {
                libxml2_time = r->time_us;
                break;
            }
        }

        bool first = true;
        for (const auto* r : results) {
            double speedup = (libxml2_time > 0 && r->time_us > 0) ? libxml2_time / r->time_us : 0;

            const char* time_unit = "us";
            double time_display = r->time_us;
            if (r->time_us >= 1000) {
                time_display = r->time_us / 1000.0;
                time_unit = "ms";
            }

            double p95_display = r->time_p95_us;
            const char* p95_unit = "us";
            if (r->time_p95_us >= 1000) {
                p95_display = r->time_p95_us / 1000.0;
                p95_unit = "ms";
            }

            if (first) {
                printf("│ %-16s │ %-16s │ %-15s │ %7.2f %-2s │ %7.2f %-2s │ %6.2fx                  │\n",
                       test_name.substr(0, 16).c_str(),
                       r->library.substr(0, 16).c_str(),
                       r->mode.substr(0, 15).c_str(),
                       time_display, time_unit,
                       p95_display, p95_unit,
                       speedup);
                first = false;
            } else {
                printf("│ %-16s │ %-16s │ %-15s │ %7.2f %-2s │ %7.2f %-2s │ %6.2fx                  │\n",
                       "",
                       r->library.substr(0, 16).c_str(),
                       r->mode.substr(0, 15).c_str(),
                       time_display, time_unit,
                       p95_display, p95_unit,
                       speedup);
            }
        }
        printf("├──────────────────┼──────────────────┼─────────────────┼───────────┼───────────┼─────────────────────────┤\n");
    }
    printf("└──────────────────┴──────────────────┴─────────────────┴───────────┴───────────┴─────────────────────────┘\n");
}

static void print_summary() {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║  SUMMARY                                                                                                ║\n");
    printf("╠════════════════════════════════════════════════════════════════════════════════════════════════════════╣\n");

    // Calculate average speedups per library/mode
    std::map<std::string, std::vector<double>> speedups;
    double libxml2_time = 0;

    for (const auto& cat : g_all_results) {
        for (const auto& r : cat.results) {
            if (r.library == "libxml2" && (r.mode == "default" || r.mode == "streaming")) {
                libxml2_time = r.time_us;
            }
            if (libxml2_time > 0 && r.time_us > 0) {
                std::string key = r.library + " (" + r.mode + ")";
                speedups[key].push_back(libxml2_time / r.time_us);
            }
        }
    }

    printf("║  Average Speedups vs libxml2:                                                                           ║\n");
    for (const auto& [key, vals] : speedups) {
        double avg = 0;
        for (double v : vals) avg += v;
        avg /= vals.size();
        printf("║    %-36s: %6.2fx                                                                          ║\n", key.c_str(), avg);
    }
    printf("╚════════════════════════════════════════════════════════════════════════════════════════════════════════╝\n");
}

static void print_json_output() {
    printf("{\n");
    printf("  \"benchmark_results\": {\n");

    bool first_cat = true;
    for (const auto& cat : g_all_results) {
        if (!first_cat) printf(",\n");
        first_cat = false;

        printf("    \"%s\": [\n", cat.category_name.c_str());

        bool first_result = true;
        for (const auto& r : cat.results) {
            if (!first_result) printf(",\n");
            first_result = false;

            printf("      {\n");
            printf("        \"test\": \"%s\",\n", r.test_name.c_str());
            printf("        \"library\": \"%s\",\n", r.library.c_str());
            printf("        \"mode\": \"%s\",\n", r.mode.c_str());
            printf("        \"time_us\": %.2f,\n", r.time_us);
            printf("        \"time_p95_us\": %.2f,\n", r.time_p95_us);
            printf("        \"bytes\": %zu,\n", r.bytes_processed);
            printf("        \"success\": %s\n", r.success ? "true" : "false");
            printf("      }");
        }

        printf("\n    ]");
    }

    printf("\n  },\n");
    printf("  \"metadata\": {\n");
    printf("    \"quick_mode\": %s\n", g_quick_mode ? "true" : "false");
    printf("  }\n");
    printf("}\n");
}

// ============================================================================
// MAIN
// ============================================================================

static void print_usage(const char* prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  --json         Output results in JSON format\n");
    printf("  --quick        Quick mode (fewer iterations)\n");
    printf("  --full         Full mode (more iterations, longer runtime)\n");
    printf("  --category N   Run only specific category (reader, writer, dom, xpath)\n");
    printf("  --help         Show this help\n");
}

int main(int argc, char** argv) {
    std::string only_category;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            g_json_output = true;
        } else if (strcmp(argv[i], "--quick") == 0) {
            g_quick_mode = true;
        } else if (strcmp(argv[i], "--full") == 0) {
            g_quick_mode = false;
        } else if (strcmp(argv[i], "--category") == 0 && i + 1 < argc) {
            only_category = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Initialize libxml2
    xmlInitParser();

    if (!g_json_output) {
        print_table_header();
        printf("\nRunning benchmarks... (mode: %s)\n", g_quick_mode ? "quick" : "full");
    }

    // Run benchmarks
    if (only_category.empty() || only_category == "reader") {
        if (!g_json_output) printf("\n[1/4] Reader Modes Benchmark...\n");
        g_all_results.push_back(benchmark_reader_modes());
    }

    if (only_category.empty() || only_category == "writer") {
        if (!g_json_output) printf("[2/4] Writer Modes Benchmark...\n");
        g_all_results.push_back(benchmark_writer_modes());
    }

    if (only_category.empty() || only_category == "dom") {
        if (!g_json_output) printf("[3/4] DOM Operations Benchmark...\n");
        g_all_results.push_back(benchmark_dom_operations());
    }

    if (only_category.empty() || only_category == "xpath") {
        if (!g_json_output) printf("[4/4] XPath Benchmark...\n");
        g_all_results.push_back(benchmark_xpath());
    }

    // Output results
    if (g_json_output) {
        print_json_output();
    } else {
        for (const auto& cat : g_all_results) {
            print_category_table(cat);
        }
        print_summary();
    }

    // Cleanup
    xmlCleanupParser();

    return 0;
}
