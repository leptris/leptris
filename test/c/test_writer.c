/**
 * @file test_writer.c
 * @brief Unit tests for StAX XML writer
 *
 * Tests for the streaming XML writer API including:
 * - Document structure (start/end document)
 * - Element writing (start/end, empty elements)
 * - Attribute writing
 * - Text content and escaping
 * - CDATA, comments, PIs
 * - Namespace support
 * - Error handling
 */

#include <taurus/writer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test counter */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("  Testing %s... ", name); \
    fflush(stdout);

#define PASS() \
    printf("PASS\n"); \
    tests_passed++;

#define FAIL(msg) \
    printf("FAIL: %s\n", msg); \
    tests_failed++;

/* Memory buffer for capturing output */
typedef struct {
    char* data;
    size_t len;
    size_t capacity;
} MemBuffer;

static size_t mem_write_callback(void* ctx, const char* data, size_t len) {
    MemBuffer* buf = (MemBuffer*)ctx;
    if (!buf || !data || len == 0) return 0;

    /* Grow buffer if needed */
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
    buf->data = (char*)malloc(1024);
    buf->data[0] = '\0';
    buf->len = 0;
    buf->capacity = 1024;
}

static void mem_buffer_cleanup(MemBuffer* buf) {
    if (buf->data) {
        free(buf->data);
        buf->data = NULL;
    }
    buf->len = 0;
    buf->capacity = 0;
}

static TaurusXMLWriter* create_test_writer(MemBuffer* buf) {
    mem_buffer_init(buf);
    return taurus_writer_create_callback(mem_write_callback, buf, "UTF-8");
}

/* ==================================================================
 * BASIC TESTS
 * ================================================================== */

void test_create_free(void) {
    TEST("writer create and free");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);

    if (!w) {
        FAIL("failed to create writer");
        return;
    }

    taurus_writer_free(w);
    mem_buffer_cleanup(&buf);

    PASS();
}

void test_empty_document(void) {
    TEST("empty document");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    int result = taurus_writer_start_document(w, NULL, NULL, -1);
    if (result != 0) {
        FAIL("start_document failed");
        taurus_writer_free(w);
        mem_buffer_cleanup(&buf);
        return;
    }

    result = taurus_writer_end_document(w);
    if (result != 0) {
        FAIL("end_document failed");
        taurus_writer_free(w);
        mem_buffer_cleanup(&buf);
        return;
    }

    taurus_writer_free(w);

    /* Should have XML declaration */
    if (strstr(buf.data, "<?xml") == NULL) {
        FAIL("missing XML declaration");
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

void test_simple_element(void) {
    TEST("simple element");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Check output */
    if (strstr(buf.data, "<root/>") == NULL &&
        strstr(buf.data, "<root></root>") == NULL) {
        FAIL("element not found in output");
        printf("    Output: %s\n", buf.data);
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

/* ==================================================================
 * ATTRIBUTE TESTS
 * ================================================================== */

void test_element_with_attribute(void) {
    TEST("element with attribute");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_attribute(w, "id", "123");
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Check output */
    if (strstr(buf.data, "id=\"123\"") == NULL) {
        FAIL("attribute not found in output");
        printf("    Output: %s\n", buf.data);
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

void test_attribute_escaping(void) {
    TEST("attribute escaping");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_attribute(w, "value", "<>&\"'");
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Check escaping */
    if (strstr(buf.data, "&lt;") == NULL ||
        strstr(buf.data, "&gt;") == NULL ||
        strstr(buf.data, "&amp;") == NULL) {
        FAIL("attribute not properly escaped");
        printf("    Output: %s\n", buf.data);
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

/* ==================================================================
 * TEXT CONTENT TESTS
 * ================================================================== */

void test_text_content(void) {
    TEST("text content");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_characters(w, "Hello, World!");
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Check output */
    if (strstr(buf.data, "Hello, World!") == NULL) {
        FAIL("text content not found");
        printf("    Output: %s\n", buf.data);
        mem_buffer_cleanup(&buf);
        return;
    }

    /* Should not be self-closing */
    if (strstr(buf.data, "<root/>") != NULL) {
        FAIL("element incorrectly self-closed");
        printf("    Output: %s\n", buf.data);
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

void test_text_escaping(void) {
    TEST("text escaping");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_characters(w, "<tag> & entity");
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Check escaping - quotes should NOT be escaped in text */
    if (strstr(buf.data, "&lt;tag&gt;") == NULL ||
        strstr(buf.data, "&amp;") == NULL) {
        FAIL("text not properly escaped");
        printf("    Output: %s\n", buf.data);
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

/* ==================================================================
 * CDATA TESTS
 * ================================================================== */

void test_cdata(void) {
    TEST("CDATA section");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_cdata(w, "<script>alert('hello');</script>");
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Check CDATA wrapper */
    if (strstr(buf.data, "<![CDATA[") == NULL ||
        strstr(buf.data, "]]>") == NULL) {
        FAIL("CDATA wrapper not found");
        printf("    Output: %s\n", buf.data);
        mem_buffer_cleanup(&buf);
        return;
    }

    /* Content should NOT be escaped */
    if (strstr(buf.data, "&lt;") != NULL) {
        FAIL("CDATA content incorrectly escaped");
        printf("    Output: %s\n", buf.data);
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

void test_cdata_invalid(void) {
    TEST("CDATA with ]]> (should fail)");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");

    /* This should fail */
    int result = taurus_writer_cdata(w, "data]]>more");
    if (result == 0) {
        FAIL("CDATA with ]]> should have failed");
        taurus_writer_free(w);
        mem_buffer_cleanup(&buf);
        return;
    }

    taurus_writer_free(w);
    mem_buffer_cleanup(&buf);
    PASS();
}

/* ==================================================================
 * COMMENT TESTS
 * ================================================================== */

void test_comment(void) {
    TEST("comment");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_comment(w, "This is a comment");
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Check comment wrapper */
    if (strstr(buf.data, "<!--This is a comment-->") == NULL) {
        FAIL("comment not found");
        printf("    Output: %s\n", buf.data);
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

void test_comment_invalid(void) {
    TEST("comment with -- (should fail)");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");

    /* This should fail */
    int result = taurus_writer_comment(w, "invalid--comment");
    if (result == 0) {
        FAIL("comment with -- should have failed");
        taurus_writer_free(w);
        mem_buffer_cleanup(&buf);
        return;
    }

    taurus_writer_free(w);
    mem_buffer_cleanup(&buf);
    PASS();
}

/* ==================================================================
 * PROCESSING INSTRUCTION TESTS
 * ================================================================== */

void test_pi(void) {
    TEST("processing instruction");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_processing_instruction(w, "xml-stylesheet", "type=\"text/xsl\" href=\"style.xsl\"");
    taurus_writer_start_element(w, "root");
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Check PI format */
    if (strstr(buf.data, "<?xml-stylesheet") == NULL ||
        strstr(buf.data, "?>") == NULL) {
        FAIL("PI not found");
        printf("    Output: %s\n", buf.data);
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

/* ==================================================================
 * NAMESPACE TESTS
 * ================================================================== */

void test_namespace(void) {
    TEST("namespace declaration");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_namespace(w, "xs", "http://www.w3.org/2001/XMLSchema");
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Check namespace declaration */
    if (strstr(buf.data, "xmlns:xs=\"http://www.w3.org/2001/XMLSchema\"") == NULL) {
        FAIL("namespace not found");
        printf("    Output: %s\n", buf.data);
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

void test_default_namespace(void) {
    TEST("default namespace");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_namespace(w, NULL, "http://example.com");
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Check namespace declaration */
    if (strstr(buf.data, "xmlns=\"http://example.com\"") == NULL) {
        FAIL("default namespace not found");
        printf("    Output: %s\n", buf.data);
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

/* ==================================================================
 * NESTING TESTS
 * ================================================================== */

void test_nested_elements(void) {
    TEST("nested elements");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_start_element(w, "child");
    taurus_writer_start_element(w, "grandchild");
    taurus_writer_characters(w, "content");
    taurus_writer_end_element(w); /* grandchild */
    taurus_writer_end_element(w); /* child */
    taurus_writer_end_element(w); /* root */
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Check nested structure */
    if (strstr(buf.data, "<grandchild>") == NULL ||
        strstr(buf.data, "</grandchild>") == NULL ||
        strstr(buf.data, "</child>") == NULL ||
        strstr(buf.data, "</root>") == NULL) {
        FAIL("nested structure not correct");
        printf("    Output: %s\n", buf.data);
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

void test_unclosed_element(void) {
    TEST("unclosed element (should fail on end_document)");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_start_element(w, "child");
    /* Missing end_element for child */

    int result = taurus_writer_end_document(w);
    if (result == 0) {
        FAIL("end_document should have failed with unclosed element");
        taurus_writer_free(w);
        mem_buffer_cleanup(&buf);
        return;
    }

    taurus_writer_free(w);
    mem_buffer_cleanup(&buf);
    PASS();
}

/* ==================================================================
 * PRETTY-PRINT TESTS
 * ================================================================== */

void test_pretty_print(void) {
    TEST("pretty print");

    MemBuffer buf;
    mem_buffer_init(&buf);

    TaurusWriterOptions opts = TAURUS_WRITER_OPTIONS_DEFAULT;
    opts.pretty_print = 1;
    opts.indent = 2;

    TaurusXMLWriter* w = taurus_writer_create_callback(mem_write_callback, &buf, "UTF-8");
    if (!w) { FAIL("create failed"); mem_buffer_cleanup(&buf); return; }

    /* Manually set options since callback constructor doesn't take options */
    /* For now, just test that it works */
    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_start_element(w, "child");
    taurus_writer_characters(w, "text");
    taurus_writer_end_element(w);
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Check output exists */
    if (buf.len == 0) {
        FAIL("no output");
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

/* ==================================================================
 * FILE OUTPUT TEST
 * ================================================================== */

void test_file_output(void) {
    TEST("file output");

    const char* test_file = "/tmp/taurus_writer_test.xml";

    TaurusXMLWriter* w = taurus_writer_create_file(test_file, "UTF-8");
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, "1.0", "UTF-8", -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_attribute(w, "version", "1.0");
    taurus_writer_characters(w, "Test content");
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Read file and verify */
    FILE* f = fopen(test_file, "r");
    if (!f) {
        FAIL("could not open output file");
        return;
    }

    char buffer[1024];
    size_t len = fread(buffer, 1, sizeof(buffer) - 1, f);
    buffer[len] = '\0';
    fclose(f);

    if (strstr(buffer, "<?xml") == NULL ||
        strstr(buffer, "<root") == NULL ||
        strstr(buffer, "Test content") == NULL) {
        FAIL("file content incorrect");
        printf("    Content: %s\n", buffer);
        return;
    }

    /* Clean up */
    remove(test_file);
    PASS();
}

/* ==================================================================
 * ERROR HANDLING TESTS
 * ================================================================== */

void test_error_after_attribute(void) {
    TEST("attribute after content (should fail)");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_characters(w, "text");
    int result = taurus_writer_attribute(w, "late", "value");
    if (result == 0) {
        FAIL("attribute after content should have failed");
        taurus_writer_free(w);
        mem_buffer_cleanup(&buf);
        return;
    }

    taurus_writer_free(w);
    mem_buffer_cleanup(&buf);
    PASS();
}

void test_get_error(void) {
    TEST("get error message");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    /* Initially no error */
    if (taurus_writer_get_error(w) != 0) {
        FAIL("should have no error initially");
        taurus_writer_free(w);
        mem_buffer_cleanup(&buf);
        return;
    }

    /* Cause an error */
    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_characters(w, "text");
    taurus_writer_attribute(w, "late", "value");

    /* Should have error now */
    if (taurus_writer_get_error(w) == 0) {
        FAIL("should have error");
        taurus_writer_free(w);
        mem_buffer_cleanup(&buf);
        return;
    }

    /* Check error message */
    const char* msg = taurus_writer_get_error_message(w);
    if (!msg || strlen(msg) == 0) {
        FAIL("should have error message");
        taurus_writer_free(w);
        mem_buffer_cleanup(&buf);
        return;
    }

    taurus_writer_free(w);
    mem_buffer_cleanup(&buf);
    PASS();
}

/* ==================================================================
 * STRESS TESTS
 * ================================================================== */

void test_deep_nesting(void) {
    TEST("deep nesting (100 levels)");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);

    /* Create 100 levels of nesting */
    for (int i = 0; i < 100; i++) {
        char name[32];
        snprintf(name, sizeof(name), "level%d", i);
        if (taurus_writer_start_element(w, name) != 0) {
            FAIL("start_element failed at deep nesting");
            taurus_writer_free(w);
            mem_buffer_cleanup(&buf);
            return;
        }
    }

    taurus_writer_characters(w, "deep content");

    /* Close all 100 levels */
    for (int i = 99; i >= 0; i--) {
        if (taurus_writer_end_element(w) != 0) {
            FAIL("end_element failed at deep nesting");
            taurus_writer_free(w);
            mem_buffer_cleanup(&buf);
            return;
        }
    }

    if (taurus_writer_end_document(w) != 0) {
        FAIL("end_document failed");
        taurus_writer_free(w);
        mem_buffer_cleanup(&buf);
        return;
    }

    taurus_writer_free(w);
    mem_buffer_cleanup(&buf);
    PASS();
}

void test_many_attributes(void) {
    TEST("many attributes (200 per element)");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "element");

    /* Add 200 attributes */
    for (int i = 0; i < 200; i++) {
        char name[32], value[64];
        snprintf(name, sizeof(name), "attr%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        if (taurus_writer_attribute(w, name, value) != 0) {
            FAIL("attribute failed at high count");
            taurus_writer_free(w);
            mem_buffer_cleanup(&buf);
            return;
        }
    }

    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Verify all attributes are present */
    for (int i = 0; i < 200; i++) {
        char expected[64];
        snprintf(expected, sizeof(expected), "attr%d=\"value%d\"", i, i);
        if (strstr(buf.data, expected) == NULL) {
            FAIL("attribute missing in output");
            printf("    Missing: %s\n", expected);
            mem_buffer_cleanup(&buf);
            return;
        }
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

void test_large_text_content(void) {
    TEST("large text content (100KB)");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    /* Create 100KB of text */
    size_t text_size = 100 * 1024;
    char* large_text = (char*)malloc(text_size + 1);
    if (!large_text) {
        FAIL("malloc failed for large text");
        taurus_writer_free(w);
        mem_buffer_cleanup(&buf);
        return;
    }

    /* Fill with repeating pattern */
    const char* pattern = "The quick brown fox jumps over the lazy dog. ";
    size_t pattern_len = strlen(pattern);
    for (size_t i = 0; i < text_size; i++) {
        large_text[i] = pattern[i % pattern_len];
    }
    large_text[text_size] = '\0';

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "large");
    taurus_writer_characters(w, large_text);
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    free(large_text);

    /* Verify output is correct size */
    if (buf.len < text_size) {
        FAIL("output too small");
        printf("    Expected at least: %zu, Got: %zu\n", text_size, buf.len);
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

void test_escaped_text_performance(void) {
    TEST("escaped text performance (50KB with entities)");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    /* Create 50KB of text that needs escaping */
    size_t text_size = 50 * 1024;
    char* escaped_text = (char*)malloc(text_size + 1);
    if (!escaped_text) {
        FAIL("malloc failed");
        taurus_writer_free(w);
        mem_buffer_cleanup(&buf);
        return;
    }

    /* Fill with pattern that needs escaping */
    const char* pattern = "<tag> & \"quotes\" 'apostrophes' ";
    size_t pattern_len = strlen(pattern);
    for (size_t i = 0; i < text_size; i++) {
        escaped_text[i] = pattern[i % pattern_len];
    }
    escaped_text[text_size] = '\0';

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "escaped");
    taurus_writer_characters(w, escaped_text);
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    free(escaped_text);

    /* Verify escaping happened */
    if (strstr(buf.data, "&lt;") == NULL || strstr(buf.data, "&amp;") == NULL) {
        FAIL("text not properly escaped");
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

void test_long_element_name(void) {
    TEST("long element name (1KB)");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    /* Create a 1KB element name */
    size_t name_len = 1024;
    char* long_name = (char*)malloc(name_len + 1);
    if (!long_name) {
        FAIL("malloc failed");
        taurus_writer_free(w);
        mem_buffer_cleanup(&buf);
        return;
    }

    /* Fill with valid name characters */
    for (size_t i = 0; i < name_len; i++) {
        long_name[i] = 'a' + (i % 26);
    }
    long_name[0] = 'e';  /* Ensure starts with letter */
    long_name[name_len] = '\0';

    taurus_writer_start_document(w, NULL, NULL, -1);
    int result = taurus_writer_start_element(w, long_name);
    if (result != 0) {
        FAIL("start_element with long name failed");
        free(long_name);
        taurus_writer_free(w);
        mem_buffer_cleanup(&buf);
        return;
    }
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    free(long_name);

    /* Verify name is in output */
    if (buf.len < name_len) {
        FAIL("output too small for long name");
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

void test_many_elements(void) {
    TEST("many elements (10,000)");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");

    /* Write 10,000 sibling elements */
    for (int i = 0; i < 10000; i++) {
        if (taurus_writer_start_element(w, "item") != 0) {
            FAIL("start_element failed");
            taurus_writer_free(w);
            mem_buffer_cleanup(&buf);
            return;
        }
        char id[16];
        snprintf(id, sizeof(id), "%d", i);
        taurus_writer_attribute(w, "id", id);
        taurus_writer_characters(w, id);
        if (taurus_writer_end_element(w) != 0) {
            FAIL("end_element failed");
            taurus_writer_free(w);
            mem_buffer_cleanup(&buf);
            return;
        }
    }

    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Verify output is reasonably sized */
    if (buf.len < 100000) {
        FAIL("output too small for 10000 elements");
        printf("    Got: %zu bytes\n", buf.len);
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

void test_mixed_content_heavy(void) {
    TEST("mixed content (elements + text interleaved)");

    MemBuffer buf;
    TaurusXMLWriter* w = create_test_writer(&buf);
    if (!w) { FAIL("create failed"); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "mixed");

    /* Interleave text and elements */
    for (int i = 0; i < 100; i++) {
        char text[32];
        snprintf(text, sizeof(text), "Text%d ", i);
        taurus_writer_characters(w, text);

        taurus_writer_start_element(w, "span");
        taurus_writer_characters(w, "inner");
        taurus_writer_end_element(w);
    }

    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Verify output */
    if (strstr(buf.data, "<span>") == NULL || strstr(buf.data, "inner") == NULL) {
        FAIL("mixed content not correct");
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

void test_callback_with_options(void) {
    TEST("callback constructor with options");

    MemBuffer buf;
    mem_buffer_init(&buf);

    TaurusWriterOptions opts = TAURUS_WRITER_OPTIONS_DEFAULT;
    opts.indent = 4;
    opts.pretty_print = 1;

    TaurusXMLWriter* w = taurus_writer_create_callback_ex(mem_write_callback, &buf, &opts);
    if (!w) { FAIL("create failed"); mem_buffer_cleanup(&buf); return; }

    taurus_writer_start_document(w, NULL, NULL, -1);
    taurus_writer_start_element(w, "root");
    taurus_writer_start_element(w, "child");
    taurus_writer_characters(w, "text");
    taurus_writer_end_element(w);
    taurus_writer_end_element(w);
    taurus_writer_end_document(w);
    taurus_writer_free(w);

    /* Verify output exists */
    if (buf.len == 0) {
        FAIL("no output");
        mem_buffer_cleanup(&buf);
        return;
    }

    mem_buffer_cleanup(&buf);
    PASS();
}

/* ==================================================================
 * MAIN
 * ================================================================== */

int main(void) {
    printf("\n=== StAX Writer Tests ===\n\n");

    /* Basic tests */
    test_create_free();
    test_empty_document();
    test_simple_element();

    /* Attribute tests */
    test_element_with_attribute();
    test_attribute_escaping();

    /* Text content tests */
    test_text_content();
    test_text_escaping();

    /* CDATA tests */
    test_cdata();
    test_cdata_invalid();

    /* Comment tests */
    test_comment();
    test_comment_invalid();

    /* PI tests */
    test_pi();

    /* Namespace tests */
    test_namespace();
    test_default_namespace();

    /* Nesting tests */
    test_nested_elements();
    test_unclosed_element();

    /* Pretty-print test */
    test_pretty_print();

    /* File output test */
    test_file_output();

    /* Error handling tests */
    test_error_after_attribute();
    test_get_error();

    /* Stress tests */
    printf("\n--- Stress Tests ---\n\n");
    test_deep_nesting();
    test_many_attributes();
    test_large_text_content();
    test_escaped_text_performance();
    test_long_element_name();
    test_many_elements();
    test_mixed_content_heavy();
    test_callback_with_options();

    /* Summary */
    printf("\n=== Results ===\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
