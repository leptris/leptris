/**
 * XML Writer Benchmarks
 *
 * Compares Taurus StAX writer performance vs libxml2 xmlTextWriter.
 * Target: >= 1.0x vs libxml2 (parity or better)
 *
 * Tests:
 * 1. Write simple elements
 * 2. Write elements with attributes
 * 3. Write text content (escaped)
 * 4. Write text content (raw/no escaping)
 * 5. Deep nesting
 * 6. Large document generation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>

// Taurus API (C)
extern "C" {
#include <taurus.h>
#include <taurus/writer.h>
}

// libxml2 API (C)
#include <libxml/parser.h>
#include <libxml/xmlwriter.h>

// Benchmark utilities
extern "C" {
#include "../utils.h"
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

// Memory buffer for capturing output
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
// Test 1: Write Simple Elements
// ============================================================================

static void bench_taurus_simple_elements(void) {
    MemBuffer buf;
    mem_buffer_init(&buf);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        buf.len = 0;

        TaurusXMLWriter* w = taurus_writer_create_callback(mem_write_cb, &buf, "UTF-8");
        taurus_writer_start_document(w, NULL, NULL, -1);
        taurus_writer_start_element(w, "root");

        for (int i = 0; i < 100; i++) {
            taurus_writer_start_element(w, "item");
            taurus_writer_end_element(w);
        }

        taurus_writer_end_element(w);
        taurus_writer_end_document(w);
        taurus_writer_free(w);
    }

    mem_buffer_cleanup(&buf);
}

static void bench_libxml2_simple_elements(void) {
    MemBuffer buf;
    mem_buffer_init(&buf);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        buf.len = 0;

        xmlBufferPtr xmlBuf = xmlBufferCreate();
        xmlTextWriterPtr writer = xmlNewTextWriterMemory(xmlBuf, 0);

        xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
        xmlTextWriterStartElement(writer, BAD_CAST "root");

        for (int i = 0; i < 100; i++) {
            xmlTextWriterStartElement(writer, BAD_CAST "item");
            xmlTextWriterEndElement(writer);
        }

        xmlTextWriterEndElement(writer);
        xmlTextWriterEndDocument(writer);

        xmlFreeTextWriter(writer);
        xmlBufferFree(xmlBuf);
    }

    mem_buffer_cleanup(&buf);
}

// ============================================================================
// Test 2: Write Elements with Attributes
// ============================================================================

static void bench_taurus_with_attributes(void) {
    MemBuffer buf;
    mem_buffer_init(&buf);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        buf.len = 0;

        TaurusXMLWriter* w = taurus_writer_create_callback(mem_write_cb, &buf, "UTF-8");
        taurus_writer_start_document(w, NULL, NULL, -1);
        taurus_writer_start_element(w, "root");

        for (int i = 0; i < 100; i++) {
            taurus_writer_start_element(w, "item");
            taurus_writer_attribute(w, "id", "123");
            taurus_writer_attribute(w, "name", "test");
            taurus_writer_attribute(w, "value", "data");
            taurus_writer_end_element(w);
        }

        taurus_writer_end_element(w);
        taurus_writer_end_document(w);
        taurus_writer_free(w);
    }

    mem_buffer_cleanup(&buf);
}

static void bench_libxml2_with_attributes(void) {
    MemBuffer buf;
    mem_buffer_init(&buf);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        buf.len = 0;

        xmlBufferPtr xmlBuf = xmlBufferCreate();
        xmlTextWriterPtr writer = xmlNewTextWriterMemory(xmlBuf, 0);

        xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
        xmlTextWriterStartElement(writer, BAD_CAST "root");

        for (int i = 0; i < 100; i++) {
            xmlTextWriterStartElement(writer, BAD_CAST "item");
            xmlTextWriterWriteAttribute(writer, BAD_CAST "id", BAD_CAST "123");
            xmlTextWriterWriteAttribute(writer, BAD_CAST "name", BAD_CAST "test");
            xmlTextWriterWriteAttribute(writer, BAD_CAST "value", BAD_CAST "data");
            xmlTextWriterEndElement(writer);
        }

        xmlTextWriterEndElement(writer);
        xmlTextWriterEndDocument(writer);

        xmlFreeTextWriter(writer);
        xmlBufferFree(xmlBuf);
    }

    mem_buffer_cleanup(&buf);
}

// ============================================================================
// Test 3: Write Text Content (Escaped)
// ============================================================================

static void bench_taurus_escaped_text(void) {
    MemBuffer buf;
    mem_buffer_init(&buf);

    const char* text_with_entities = "This <text> has & \"special\" 'chars' in it.";

    for (int iter = 0; iter < ITERATIONS; iter++) {
        buf.len = 0;

        TaurusXMLWriter* w = taurus_writer_create_callback(mem_write_cb, &buf, "UTF-8");
        taurus_writer_start_document(w, NULL, NULL, -1);
        taurus_writer_start_element(w, "root");

        for (int i = 0; i < 100; i++) {
            taurus_writer_start_element(w, "item");
            taurus_writer_characters(w, text_with_entities);
            taurus_writer_end_element(w);
        }

        taurus_writer_end_element(w);
        taurus_writer_end_document(w);
        taurus_writer_free(w);
    }

    mem_buffer_cleanup(&buf);
}

static void bench_libxml2_escaped_text(void) {
    MemBuffer buf;
    mem_buffer_init(&buf);

    const char* text_with_entities = "This <text> has & \"special\" 'chars' in it.";

    for (int iter = 0; iter < ITERATIONS; iter++) {
        buf.len = 0;

        xmlBufferPtr xmlBuf = xmlBufferCreate();
        xmlTextWriterPtr writer = xmlNewTextWriterMemory(xmlBuf, 0);

        xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
        xmlTextWriterStartElement(writer, BAD_CAST "root");

        for (int i = 0; i < 100; i++) {
            xmlTextWriterStartElement(writer, BAD_CAST "item");
            xmlTextWriterWriteString(writer, BAD_CAST text_with_entities);
            xmlTextWriterEndElement(writer);
        }

        xmlTextWriterEndElement(writer);
        xmlTextWriterEndDocument(writer);

        xmlFreeTextWriter(writer);
        xmlBufferFree(xmlBuf);
    }

    mem_buffer_cleanup(&buf);
}

// ============================================================================
// Test 4: Write Text Content (Raw - No Escaping)
// ============================================================================

static void bench_taurus_raw_text(void) {
    MemBuffer buf;
    mem_buffer_init(&buf);

    const char* plain_text = "This is plain text without any special characters that need escaping.";

    for (int iter = 0; iter < ITERATIONS; iter++) {
        buf.len = 0;

        TaurusXMLWriter* w = taurus_writer_create_callback(mem_write_cb, &buf, "UTF-8");
        taurus_writer_start_document(w, NULL, NULL, -1);
        taurus_writer_start_element(w, "root");

        for (int i = 0; i < 100; i++) {
            taurus_writer_start_element(w, "item");
            taurus_writer_characters(w, plain_text);
            taurus_writer_end_element(w);
        }

        taurus_writer_end_element(w);
        taurus_writer_end_document(w);
        taurus_writer_free(w);
    }

    mem_buffer_cleanup(&buf);
}

static void bench_libxml2_raw_text(void) {
    MemBuffer buf;
    mem_buffer_init(&buf);

    const char* plain_text = "This is plain text without any special characters that need escaping.";

    for (int iter = 0; iter < ITERATIONS; iter++) {
        buf.len = 0;

        xmlBufferPtr xmlBuf = xmlBufferCreate();
        xmlTextWriterPtr writer = xmlNewTextWriterMemory(xmlBuf, 0);

        xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
        xmlTextWriterStartElement(writer, BAD_CAST "root");

        for (int i = 0; i < 100; i++) {
            xmlTextWriterStartElement(writer, BAD_CAST "item");
            xmlTextWriterWriteString(writer, BAD_CAST plain_text);
            xmlTextWriterEndElement(writer);
        }

        xmlTextWriterEndElement(writer);
        xmlTextWriterEndDocument(writer);

        xmlFreeTextWriter(writer);
        xmlBufferFree(xmlBuf);
    }

    mem_buffer_cleanup(&buf);
}

// ============================================================================
// Test 5: Deep Nesting
// ============================================================================

static void bench_taurus_deep_nesting(void) {
    MemBuffer buf;
    mem_buffer_init(&buf);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        buf.len = 0;

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
    }

    mem_buffer_cleanup(&buf);
}

static void bench_libxml2_deep_nesting(void) {
    MemBuffer buf;
    mem_buffer_init(&buf);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        buf.len = 0;

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
    }

    mem_buffer_cleanup(&buf);
}

// ============================================================================
// Test 6: Large Document Generation
// ============================================================================

static void bench_taurus_large_doc(void) {
    MemBuffer buf;
    mem_buffer_init(&buf);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        buf.len = 0;

        TaurusXMLWriter* w = taurus_writer_create_callback(mem_write_cb, &buf, "UTF-8");
        taurus_writer_start_document(w, NULL, NULL, -1);
        taurus_writer_start_element(w, "catalog");

        for (int i = 0; i < 500; i++) {
            taurus_writer_start_element(w, "item");
            taurus_writer_attribute(w, "id", "12345");
            taurus_writer_attribute(w, "category", "books");
            taurus_writer_start_element(w, "title");
            taurus_writer_characters(w, "Book Title Here");
            taurus_writer_end_element(w);
            taurus_writer_start_element(w, "author");
            taurus_writer_characters(w, "Author Name");
            taurus_writer_end_element(w);
            taurus_writer_start_element(w, "price");
            taurus_writer_characters(w, "29.99");
            taurus_writer_end_element(w);
            taurus_writer_end_element(w);
        }

        taurus_writer_end_element(w);
        taurus_writer_end_document(w);
        taurus_writer_free(w);
    }

    mem_buffer_cleanup(&buf);
}

static void bench_libxml2_large_doc(void) {
    MemBuffer buf;
    mem_buffer_init(&buf);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        buf.len = 0;

        xmlBufferPtr xmlBuf = xmlBufferCreate();
        xmlTextWriterPtr writer = xmlNewTextWriterMemory(xmlBuf, 0);

        xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
        xmlTextWriterStartElement(writer, BAD_CAST "catalog");

        for (int i = 0; i < 500; i++) {
            xmlTextWriterStartElement(writer, BAD_CAST "item");
            xmlTextWriterWriteAttribute(writer, BAD_CAST "id", BAD_CAST "12345");
            xmlTextWriterWriteAttribute(writer, BAD_CAST "category", BAD_CAST "books");
            xmlTextWriterStartElement(writer, BAD_CAST "title");
            xmlTextWriterWriteString(writer, BAD_CAST "Book Title Here");
            xmlTextWriterEndElement(writer);
            xmlTextWriterStartElement(writer, BAD_CAST "author");
            xmlTextWriterWriteString(writer, BAD_CAST "Author Name");
            xmlTextWriterEndElement(writer);
            xmlTextWriterStartElement(writer, BAD_CAST "price");
            xmlTextWriterWriteString(writer, BAD_CAST "29.99");
            xmlTextWriterEndElement(writer);
            xmlTextWriterEndElement(writer);
        }

        xmlTextWriterEndElement(writer);
        xmlTextWriterEndDocument(writer);

        xmlFreeTextWriter(writer);
        xmlBufferFree(xmlBuf);
    }

    mem_buffer_cleanup(&buf);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("\n=== XML Writer Benchmarks: Taurus vs libxml2 ===\n\n");

    std::vector<double> taurus_times(ITERATIONS);
    std::vector<double> libxml2_times(ITERATIONS);
    long long start, end;

    benchmark_print_header("libxml2");

    // Test 1: Simple Elements
    printf("Running simple elements benchmark...\n");

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        bench_taurus_simple_elements();
        bench_libxml2_simple_elements();
    }

    // Taurus
    for (int i = 0; i < ITERATIONS; i++) {
        start = benchmark_time_ns();
        bench_taurus_simple_elements();
        end = benchmark_time_ns();
        taurus_times[i] = (end - start) / 1000.0;
    }

    // libxml2
    for (int i = 0; i < ITERATIONS; i++) {
        start = benchmark_time_ns();
        bench_libxml2_simple_elements();
        end = benchmark_time_ns();
        libxml2_times[i] = (end - start) / 1000.0;
    }

    benchmark_stats taurus_stats = benchmark_analyze(taurus_times.data(), ITERATIONS);
    benchmark_stats libxml2_stats = benchmark_analyze(libxml2_times.data(), ITERATIONS);
    benchmark_print_result("Simple Elements", taurus_stats, libxml2_stats, "libxml2");

    // Test 2: With Attributes
    printf("Running attributes benchmark...\n");

    for (int i = 0; i < WARMUP_ITERS; i++) {
        bench_taurus_with_attributes();
        bench_libxml2_with_attributes();
    }

    for (int i = 0; i < ITERATIONS; i++) {
        start = benchmark_time_ns();
        bench_taurus_with_attributes();
        end = benchmark_time_ns();
        taurus_times[i] = (end - start) / 1000.0;
    }

    for (int i = 0; i < ITERATIONS; i++) {
        start = benchmark_time_ns();
        bench_libxml2_with_attributes();
        end = benchmark_time_ns();
        libxml2_times[i] = (end - start) / 1000.0;
    }

    taurus_stats = benchmark_analyze(taurus_times.data(), ITERATIONS);
    libxml2_stats = benchmark_analyze(libxml2_times.data(), ITERATIONS);
    benchmark_print_result("With Attributes", taurus_stats, libxml2_stats, "libxml2");

    // Test 3: Escaped Text
    printf("Running escaped text benchmark...\n");

    for (int i = 0; i < WARMUP_ITERS; i++) {
        bench_taurus_escaped_text();
        bench_libxml2_escaped_text();
    }

    for (int i = 0; i < ITERATIONS; i++) {
        start = benchmark_time_ns();
        bench_taurus_escaped_text();
        end = benchmark_time_ns();
        taurus_times[i] = (end - start) / 1000.0;
    }

    for (int i = 0; i < ITERATIONS; i++) {
        start = benchmark_time_ns();
        bench_libxml2_escaped_text();
        end = benchmark_time_ns();
        libxml2_times[i] = (end - start) / 1000.0;
    }

    taurus_stats = benchmark_analyze(taurus_times.data(), ITERATIONS);
    libxml2_stats = benchmark_analyze(libxml2_times.data(), ITERATIONS);
    benchmark_print_result("Escaped Text", taurus_stats, libxml2_stats, "libxml2");

    // Test 4: Raw Text
    printf("Running raw text benchmark...\n");

    for (int i = 0; i < WARMUP_ITERS; i++) {
        bench_taurus_raw_text();
        bench_libxml2_raw_text();
    }

    for (int i = 0; i < ITERATIONS; i++) {
        start = benchmark_time_ns();
        bench_taurus_raw_text();
        end = benchmark_time_ns();
        taurus_times[i] = (end - start) / 1000.0;
    }

    for (int i = 0; i < ITERATIONS; i++) {
        start = benchmark_time_ns();
        bench_libxml2_raw_text();
        end = benchmark_time_ns();
        libxml2_times[i] = (end - start) / 1000.0;
    }

    taurus_stats = benchmark_analyze(taurus_times.data(), ITERATIONS);
    libxml2_stats = benchmark_analyze(libxml2_times.data(), ITERATIONS);
    benchmark_print_result("Raw Text", taurus_stats, libxml2_stats, "libxml2");

    // Test 5: Deep Nesting
    printf("Running deep nesting benchmark...\n");

    for (int i = 0; i < WARMUP_ITERS; i++) {
        bench_taurus_deep_nesting();
        bench_libxml2_deep_nesting();
    }

    for (int i = 0; i < ITERATIONS; i++) {
        start = benchmark_time_ns();
        bench_taurus_deep_nesting();
        end = benchmark_time_ns();
        taurus_times[i] = (end - start) / 1000.0;
    }

    for (int i = 0; i < ITERATIONS; i++) {
        start = benchmark_time_ns();
        bench_libxml2_deep_nesting();
        end = benchmark_time_ns();
        libxml2_times[i] = (end - start) / 1000.0;
    }

    taurus_stats = benchmark_analyze(taurus_times.data(), ITERATIONS);
    libxml2_stats = benchmark_analyze(libxml2_times.data(), ITERATIONS);
    benchmark_print_result("Deep Nesting", taurus_stats, libxml2_stats, "libxml2");

    // Test 6: Large Document
    printf("Running large document benchmark...\n");

    for (int i = 0; i < WARMUP_ITERS; i++) {
        bench_taurus_large_doc();
        bench_libxml2_large_doc();
    }

    for (int i = 0; i < ITERATIONS; i++) {
        start = benchmark_time_ns();
        bench_taurus_large_doc();
        end = benchmark_time_ns();
        taurus_times[i] = (end - start) / 1000.0;
    }

    for (int i = 0; i < ITERATIONS; i++) {
        start = benchmark_time_ns();
        bench_libxml2_large_doc();
        end = benchmark_time_ns();
        libxml2_times[i] = (end - start) / 1000.0;
    }

    taurus_stats = benchmark_analyze(taurus_times.data(), ITERATIONS);
    libxml2_stats = benchmark_analyze(libxml2_times.data(), ITERATIONS);
    benchmark_print_result("Large Document", taurus_stats, libxml2_stats, "libxml2");

    benchmark_print_footer();

    return 0;
}
