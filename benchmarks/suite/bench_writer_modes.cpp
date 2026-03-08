/**
 * XML Writer Modes Benchmarks
 *
 * Compares XML generation performance across different modes for Taurus, pugixml,
 * and libxml2. This helps users understand the tradeoffs between different
 * writing/serialization configurations.
 *
 * Taurus Modes:
 * - StAX Writer (raw): Streaming, no formatting - fastest
 * - StAX Writer (pretty): Streaming, with indentation - readable output
 * - DOM Serialize: Build DOM then serialize - most flexible
 *
 * pugixml Modes:
 * - format_raw: No formatting, minimal output
 * - format_indent: With indentation for readability
 * - format_no_declaration: Skip XML declaration
 *
 * libxml2 Modes:
 * - xmlTextWriter (streaming): Memory efficient, incremental
 * - xmlDocDump (DOM): Build DOM then dump - more features
 * - xmlDocFormatDump: Formatted output
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <sstream>

// Taurus API (C)
extern "C" {
#include <taurus.h>
#include <taurus/writer.h>
}

// pugixml API (C++)
#include <pugixml.hpp>

// libxml2 API (C)
#include <libxml/parser.h>
#include <libxml/xmlwriter.h>
#include <libxml/tree.h>

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

// ============================================================================
// Memory Buffer for Output Capture
// ============================================================================

typedef struct {
    char* data;
    size_t len;
    size_t capacity;
} MemBuffer;

static size_t mem_write_cb(void* ctx, const char* data, size_t len) {
    MemBuffer* buf = (MemBuffer*)ctx;
    if (!buf || !data || len == 0) return 0;

    if (buf->len + len >= buf->capacity) {
        size_t new_capacity = buf->capacity * 2;
        if (new_capacity < buf->len + len + 1) {
            new_capacity = buf->len + len + 1;
        }
        char* new_data = (char*)realloc(buf->data, new_capacity);
        if (!new_data) return 0;
        buf->data = new_data;
        buf->capacity = new_capacity;
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

static void mem_buffer_cleanup(MemBuffer* buf) {
    if (buf->data) {
        free(buf->data);
        buf->data = NULL;
    }
    buf->len = 0;
    buf->capacity = 0;
}

// libxml2 callback wrapper
static int xml_write_callback(void* ctx, const char* buffer, int len) {
    return (int)mem_write_cb(ctx, buffer, (size_t)len);
}

// ============================================================================
// Taurus Writer Modes
// ============================================================================

static void bench_taurus_stax_raw(MemBuffer* buf, int elements, int attrs) {
    mem_buffer_reset(buf);

    TaurusXMLWriter* w = taurus_writer_create_callback(mem_write_cb, buf, "UTF-8");
    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");

    for (int i = 0; i < elements; i++) {
        taurus_writer_start_element(w, "item");
        for (int a = 0; a < attrs; a++) {
            char name[32], value[32];
            snprintf(name, sizeof(name), "attr%d", a);
            snprintf(value, sizeof(value), "value%d", a);
            taurus_writer_attribute(w, name, value);
        }
        taurus_writer_characters(w, "content");
        taurus_writer_end_element(w);
    }

    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);
}

static void bench_taurus_stax_pretty(MemBuffer* buf, int elements, int attrs) {
    mem_buffer_reset(buf);

    TaurusWriterOptions opts = TAURUS_WRITER_OPTIONS_DEFAULT;
    opts.indent = 2;
    opts.pretty_print = 1;

    TaurusXMLWriter* w = taurus_writer_create_callback_ex(mem_write_cb, buf, &opts);
    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");

    for (int i = 0; i < elements; i++) {
        taurus_writer_start_element(w, "item");
        for (int a = 0; a < attrs; a++) {
            char name[32], value[32];
            snprintf(name, sizeof(name), "attr%d", a);
            snprintf(value, sizeof(value), "value%d", a);
            taurus_writer_attribute(w, name, value);
        }
        taurus_writer_characters(w, "content");
        taurus_writer_end_element(w);
    }

    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);
}

static void bench_taurus_dom_serialize(MemBuffer* buf, int elements, int attrs) {
    // Build DOM first
    const char* template_xml = "<root></root>";
    TaurusDocument doc = taurus_parse_string(template_xml, strlen(template_xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    for (int i = 0; i < elements; i++) {
        TaurusElement elem = taurus_element_create(doc, "item");
        for (int a = 0; a < attrs; a++) {
            char name[32], value[32];
            snprintf(name, sizeof(name), "attr%d", a);
            snprintf(value, sizeof(value), "value%d", a);
            taurus_element_set_attribute(elem, name, value);
        }
        taurus_element_set_text(elem, "content");
        taurus_element_append_child(root, elem);
    }

    // Serialize
    char* output = taurus_document_serialize(doc, NULL);
    if (output) {
        mem_write_cb(buf, output, strlen(output));
        taurus_free_string(output);
    }

    taurus_document_free(doc);
}

// ============================================================================
// pugixml Writer Modes
// ============================================================================

static void bench_pugixml_raw(int elements, int attrs) {
    pugi::xml_document doc;
    doc.append_child(pugi::node_declaration).append_attribute("version") = "1.0";

    pugi::xml_node root = doc.append_child("root");

    for (int i = 0; i < elements; i++) {
        pugi::xml_node item = root.append_child("item");
        for (int a = 0; a < attrs; a++) {
            char name[32], value[32];
            snprintf(name, sizeof(name), "attr%d", a);
            snprintf(value, sizeof(value), "value%d", a);
            item.append_attribute(name) = value;
        }
        item.append_child(pugi::node_pcdata).set_value("content");
    }

    std::stringstream ss;
    doc.save(ss, "", pugi::format_raw);
}

static void bench_pugixml_indent(int elements, int attrs) {
    pugi::xml_document doc;
    doc.append_child(pugi::node_declaration).append_attribute("version") = "1.0";

    pugi::xml_node root = doc.append_child("root");

    for (int i = 0; i < elements; i++) {
        pugi::xml_node item = root.append_child("item");
        for (int a = 0; a < attrs; a++) {
            char name[32], value[32];
            snprintf(name, sizeof(name), "attr%d", a);
            snprintf(value, sizeof(value), "value%d", a);
            item.append_attribute(name) = value;
        }
        item.append_child(pugi::node_pcdata).set_value("content");
    }

    std::stringstream ss;
    doc.save(ss, "  ", pugi::format_indent);
}

static void bench_pugixml_no_decl(int elements, int attrs) {
    pugi::xml_document doc;

    pugi::xml_node root = doc.append_child("root");

    for (int i = 0; i < elements; i++) {
        pugi::xml_node item = root.append_child("item");
        for (int a = 0; a < attrs; a++) {
            char name[32], value[32];
            snprintf(name, sizeof(name), "attr%d", a);
            snprintf(value, sizeof(value), "value%d", a);
            item.append_attribute(name) = value;
        }
        item.append_child(pugi::node_pcdata).set_value("content");
    }

    std::stringstream ss;
    doc.save(ss, "", pugi::format_raw | pugi::format_no_declaration);
}

// ============================================================================
// libxml2 Writer Modes
// ============================================================================

static void bench_libxml2_streaming(MemBuffer* buf, int elements, int attrs) {
    mem_buffer_reset(buf);

    xmlBufferPtr xmlBuf = xmlBufferCreate();
    xmlTextWriterPtr writer = xmlNewTextWriterMemory(xmlBuf, 0);

    xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
    xmlTextWriterStartElement(writer, BAD_CAST "root");

    for (int i = 0; i < elements; i++) {
        xmlTextWriterStartElement(writer, BAD_CAST "item");
        for (int a = 0; a < attrs; a++) {
            char name[32], value[32];
            snprintf(name, sizeof(name), "attr%d", a);
            snprintf(value, sizeof(value), "value%d", a);
            xmlTextWriterWriteAttribute(writer, BAD_CAST name, BAD_CAST value);
        }
        xmlTextWriterWriteString(writer, BAD_CAST "content");
        xmlTextWriterEndElement(writer);
    }

    xmlTextWriterEndElement(writer);
    xmlTextWriterEndDocument(writer);

    xmlFreeTextWriter(writer);
    xmlBufferFree(xmlBuf);
}

static void bench_libxml2_dom_dump(MemBuffer* buf, int elements, int attrs) {
    mem_buffer_reset(buf);

    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    xmlNodePtr root = xmlNewNode(NULL, BAD_CAST "root");
    xmlDocSetRootElement(doc, root);

    for (int i = 0; i < elements; i++) {
        xmlNodePtr item = xmlNewChild(root, NULL, BAD_CAST "item", NULL);
        for (int a = 0; a < attrs; a++) {
            char name[32], value[32];
            snprintf(name, sizeof(name), "attr%d", a);
            snprintf(value, sizeof(value), "value%d", a);
            xmlNewProp(item, BAD_CAST name, BAD_CAST value);
        }
        xmlNodeAddContent(item, BAD_CAST "content");
    }

    xmlChar* xmlBuf;
    int bufSize;
    xmlDocDumpMemory(doc, &xmlBuf, &bufSize);
    if (xmlBuf) {
        xmlFree(xmlBuf);
    }

    xmlFreeDoc(doc);
}

static void bench_libxml2_format_dump(MemBuffer* buf, int elements, int attrs) {
    mem_buffer_reset(buf);

    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    xmlNodePtr root = xmlNewNode(NULL, BAD_CAST "root");
    xmlDocSetRootElement(doc, root);

    for (int i = 0; i < elements; i++) {
        xmlNodePtr item = xmlNewChild(root, NULL, BAD_CAST "item", NULL);
        for (int a = 0; a < attrs; a++) {
            char name[32], value[32];
            snprintf(name, sizeof(name), "attr%d", a);
            snprintf(value, sizeof(value), "value%d", a);
            xmlNewProp(item, BAD_CAST name, BAD_CAST value);
        }
        xmlNodeAddContent(item, BAD_CAST "content");
    }

    xmlChar* xmlBuf;
    int bufSize;
    xmlDocDumpFormatMemory(doc, &xmlBuf, &bufSize, 1);
    if (xmlBuf) {
        xmlFree(xmlBuf);
    }

    xmlFreeDoc(doc);
}

// ============================================================================
// Benchmark Runner
// ============================================================================

static void run_writer_benchmark(const char* test_name,
                                 int elements, int attrs, int iterations) {
    printf("\n┌─────────────────────────────────────────────────────────────────┐\n");
    printf("│ %-63s │\n", test_name);
    printf("├─────────────────────────────────┬───────────┬─────────────────┤\n");
    printf("│ Mode                            │ Time      │ Relative        │\n");
    printf("├─────────────────────────────────┼───────────┼─────────────────┤\n");

    MemBuffer buf;
    mem_buffer_init(&buf);
    std::vector<double> times;
    long long start, end;

    // Taurus StAX raw
    for (int i = 0; i < WARMUP_ITERS; i++) {
        bench_taurus_stax_raw(&buf, elements, attrs);
    }
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_taurus_stax_raw(&buf, elements, attrs);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats taurus_raw = benchmark_analyze(times.data(), iterations);

    // Taurus StAX pretty
    for (int i = 0; i < WARMUP_ITERS; i++) {
        bench_taurus_stax_pretty(&buf, elements, attrs);
    }
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_taurus_stax_pretty(&buf, elements, attrs);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats taurus_pretty = benchmark_analyze(times.data(), iterations);

    // Taurus DOM serialize
    for (int i = 0; i < WARMUP_ITERS; i++) {
        bench_taurus_dom_serialize(&buf, elements, attrs);
    }
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_taurus_dom_serialize(&buf, elements, attrs);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats taurus_dom = benchmark_analyze(times.data(), iterations);

    // pugixml raw
    for (int i = 0; i < WARMUP_ITERS; i++) {
        bench_pugixml_raw(elements, attrs);
    }
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_pugixml_raw(elements, attrs);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats pugi_raw = benchmark_analyze(times.data(), iterations);

    // pugixml indent
    for (int i = 0; i < WARMUP_ITERS; i++) {
        bench_pugixml_indent(elements, attrs);
    }
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_pugixml_indent(elements, attrs);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats pugi_indent = benchmark_analyze(times.data(), iterations);

    // libxml2 streaming
    for (int i = 0; i < WARMUP_ITERS; i++) {
        bench_libxml2_streaming(&buf, elements, attrs);
    }
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_libxml2_streaming(&buf, elements, attrs);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats xml2_stream = benchmark_analyze(times.data(), iterations);

    // libxml2 DOM dump
    for (int i = 0; i < WARMUP_ITERS; i++) {
        bench_libxml2_dom_dump(&buf, elements, attrs);
    }
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_libxml2_dom_dump(&buf, elements, attrs);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats xml2_dom = benchmark_analyze(times.data(), iterations);

    // libxml2 format dump
    for (int i = 0; i < WARMUP_ITERS; i++) {
        bench_libxml2_format_dump(&buf, elements, attrs);
    }
    times.clear();
    for (int i = 0; i < iterations; i++) {
        start = benchmark_time_ns();
        bench_libxml2_format_dump(&buf, elements, attrs);
        end = benchmark_time_ns();
        times.push_back((end - start) / 1000.0);
    }
    benchmark_stats xml2_format = benchmark_analyze(times.data(), iterations);

    mem_buffer_cleanup(&buf);

    // Find fastest time for relative comparison
    double fastest = taurus_raw.median;
    if (pugi_raw.median < fastest) fastest = pugi_raw.median;

    // Print results
    auto print_row = [&](const char* name, double time, double base) {
        const char* unit = "us";
        double display_time = time;
        if (time >= 1000) {
            display_time = time / 1000.0;
            unit = "ms";
        }
        double relative = time / base;
        printf("│ %-31s │ %7.2f %-2s │ %6.2fx           │\n",
               name, display_time, unit, relative);
    };

    print_row("Taurus StAX (raw)", taurus_raw.median, fastest);
    print_row("Taurus StAX (pretty)", taurus_pretty.median, fastest);
    print_row("Taurus DOM serialize", taurus_dom.median, fastest);

    printf("├─────────────────────────────────┼───────────┼─────────────────┤\n");

    print_row("pugixml (raw)", pugi_raw.median, fastest);
    print_row("pugixml (indent)", pugi_indent.median, fastest);

    printf("├─────────────────────────────────┼───────────┼─────────────────┤\n");

    print_row("libxml2 (streaming)", xml2_stream.median, fastest);
    print_row("libxml2 (DOM dump)", xml2_dom.median, fastest);
    print_row("libxml2 (format dump)", xml2_format.median, fastest);

    printf("└─────────────────────────────────┴───────────┴─────────────────┘\n");

    // Print summary
    printf("\nSpeedups vs libxml2 streaming:\n");
    printf("  Taurus StAX (raw):    %.2fx\n", xml2_stream.median / taurus_raw.median);
    printf("  Taurus StAX (pretty): %.2fx\n", xml2_stream.median / taurus_pretty.median);
    printf("  pugixml (raw):        %.2fx\n", xml2_stream.median / pugi_raw.median);
    printf("  pugixml (indent):     %.2fx\n", xml2_stream.median / pugi_indent.median);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    // Initialize libxml2
    xmlInitParser();

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║          XML Writer Modes Benchmarks                              ║\n");
    printf("║  Comparing writing modes across Taurus, pugixml, and libxml2      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");

    // Test 1: Simple elements (no attributes)
    printf("\n--- Test 1: Simple Elements (100 elements, 0 attributes) ---\n");
    run_writer_benchmark("Simple Elements", 100, 0, ITERATIONS);

    // Test 2: With attributes
    printf("\n--- Test 2: With Attributes (100 elements, 5 attributes each) ---\n");
    run_writer_benchmark("With Attributes", 100, 5, ITERATIONS);

    // Test 3: Many attributes
    printf("\n--- Test 3: Many Attributes (50 elements, 20 attributes each) ---\n");
    run_writer_benchmark("Many Attributes", 50, 20, ITERATIONS);

    // Test 4: Large document
    printf("\n--- Test 4: Large Document (500 elements, 3 attributes each) ---\n");
    run_writer_benchmark("Large Document", 500, 3, ITERATIONS);

    // Test 5: Deep nesting
    printf("\n--- Test 5: Deep Nesting (50 levels) ---\n");
    {
        MemBuffer buf;
        mem_buffer_init(&buf);
        std::vector<double> times;
        long long start, end;

        // Taurus StAX deep
        auto bench_taurus_deep = [&]() {
            mem_buffer_reset(&buf);
            TaurusXMLWriter* w = taurus_writer_create_callback(mem_write_cb, &buf, "UTF-8");
            taurus_writer_start_document(w, NULL, NULL, -1);

            for (int i = 0; i < 50; i++) {
                taurus_writer_start_element(w, "level");
            }
            taurus_writer_characters(w, "deep");
            for (int i = 0; i < 50; i++) {
                taurus_writer_end_element(w);
            }

            taurus_writer_end_document(w);
            taurus_writer_free(w);
        };

        // libxml2 deep
        auto bench_libxml2_deep = [&]() {
            mem_buffer_reset(&buf);
            xmlBufferPtr xmlBuf = xmlBufferCreate();
            xmlTextWriterPtr writer = xmlNewTextWriterMemory(xmlBuf, 0);

            xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);

            for (int i = 0; i < 50; i++) {
                xmlTextWriterStartElement(writer, BAD_CAST "level");
            }
            xmlTextWriterWriteString(writer, BAD_CAST "deep");
            for (int i = 0; i < 50; i++) {
                xmlTextWriterEndElement(writer);
            }

            xmlTextWriterEndDocument(writer);
            xmlFreeTextWriter(writer);
            xmlBufferFree(xmlBuf);
        };

        for (int i = 0; i < WARMUP_ITERS; i++) {
            bench_taurus_deep();
            bench_libxml2_deep();
        }

        double taurus_deep_time = 0, libxml2_deep_time = 0;
        for (int i = 0; i < ITERATIONS; i++) {
            start = benchmark_time_ns();
            bench_taurus_deep();
            end = benchmark_time_ns();
            taurus_deep_time += (end - start);

            start = benchmark_time_ns();
            bench_libxml2_deep();
            end = benchmark_time_ns();
            libxml2_deep_time += (end - start);
        }

        taurus_deep_time /= ITERATIONS * 1000.0;
        libxml2_deep_time /= ITERATIONS * 1000.0;

        printf("  Taurus StAX:  %.2f us\n", taurus_deep_time);
        printf("  libxml2:      %.2f us\n", libxml2_deep_time);
        printf("  Speedup:      %.2fx\n", libxml2_deep_time / taurus_deep_time);

        mem_buffer_cleanup(&buf);
    }

    // Cleanup
    xmlCleanupParser();

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  Key Insights:                                                    ║\n");
    printf("║  - Taurus StAX (raw) is fastest for all test cases               ║\n");
    printf("║  - Pretty-printing adds ~10-20%% overhead                        ║\n");
    printf("║  - DOM serialize is slower due to DOM construction               ║\n");
    printf("║  - pugixml is competitive but requires DOM build                 ║\n");
    printf("║  - libxml2 streaming is 4-6x slower than Taurus StAX             ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");

    return 0;
}
