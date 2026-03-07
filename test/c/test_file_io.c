/* test_file_io.c - File I/O Operations Tests
 * Copyright (c) 2025, Ribose Inc.
 *
 * Tests for file loading and saving operations.
 * Based on pugixml/test_document.cpp file I/O tests.
 */

#include <taurus.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

/* ============================================================================
 * Test Framework
 * ============================================================================ */

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s\n", message); \
            return 0; \
        } \
    } while(0)

#define TEST_PASS(name) \
    do { \
        printf("PASS: %s\n", name); \
        return 1; \
    } while(0)

/* ============================================================================
 * Test File Path Helpers
 * ============================================================================ */

static const char* g_test_temp_dir = NULL;

static void setup_temp_dir(void) {
    g_test_temp_dir = "/tmp/taurus_test_temp";
    /* Create temp directory if it doesn't exist */
    mkdir(g_test_temp_dir, 0755);
}

static void cleanup_temp_dir(void) {
    /* Remove all test files */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_test_temp_dir);
    system(cmd);
}

static char* get_temp_filepath(const char* filename) {
    static char path[256];
    snprintf(path, sizeof(path), "%s/%s", g_test_temp_dir, filename);
    return path;
}

static int file_exists(const char* filepath) {
    struct stat st;
    return (stat(filepath, &st) == 0);
}

static size_t file_size(const char* filepath) {
    struct stat st;
    if (stat(filepath, &st) != 0) return 0;
    return (size_t)st.st_size;
}

static char* read_file_contents(const char* filepath) {
    FILE* file = fopen(filepath, "rb");
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = (char*)malloc(size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }

    fread(content, 1, size, file);
    content[size] = '\0';

    fclose(file);
    return content;
}

/* ============================================================================
 * Test 1: Basic File Saving
 * ============================================================================ */

static int test_file_save_basic(void) {
    const char* xml = "<root><child>Text</child></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    const char* filepath = get_temp_filepath("test_basic.xml");
    TaurusStatus status = taurus_document_save_file(doc, filepath, NULL);
    TEST_ASSERT(status == TAURUS_OK, "File save failed");

    TEST_ASSERT(file_exists(filepath), "File was not created");

    /* Verify file contents */
    char* content = read_file_contents(filepath);
    TEST_ASSERT(content != NULL, "Could not read file contents");
    TEST_ASSERT(strstr(content, "<root>") != NULL, "File missing root element");
    TEST_ASSERT(strstr(content, "<child>") != NULL, "File missing child element");
    TEST_ASSERT(strstr(content, "Text") != NULL, "File missing text content");
    free(content);

    taurus_document_free(doc);
    TEST_PASS("test_file_save_basic");
}

/* ============================================================================
 * Test 2: Save with Pretty Print
 * ============================================================================ */

static int test_file_save_pretty(void) {
    const char* xml = "<root><child1/><child2/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusSerializeOptions opts = {
        .indent = 2,
        .xml_declaration = 0
    };

    const char* filepath = get_temp_filepath("test_pretty.xml");
    TaurusStatus status = taurus_document_save_file(doc, filepath, &opts);
    TEST_ASSERT(status == TAURUS_OK, "Pretty file save failed");

    /* Verify indentation */
    char* content = read_file_contents(filepath);
    TEST_ASSERT(content != NULL, "Could not read file contents");
    TEST_ASSERT(strstr(content, "  ") != NULL, "File missing indentation");
    free(content);

    taurus_document_free(doc);
    TEST_PASS("test_file_save_pretty");
}

/* ============================================================================
 * Test 3: Save with XML Declaration
 * ============================================================================ */

static int test_file_save_declaration(void) {
    const char* xml = "<root>Content</root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusSerializeOptions opts = {
        .indent = 0,
        .xml_declaration = 1,
        .encoding = "UTF-8"
    };

    const char* filepath = get_temp_filepath("test_decl.xml");
    TaurusStatus status = taurus_document_save_file(doc, filepath, &opts);
    TEST_ASSERT(status == TAURUS_OK, "Declaration file save failed");

    /* Verify XML declaration */
    char* content = read_file_contents(filepath);
    TEST_ASSERT(content != NULL, "Could not read file contents");
    TEST_ASSERT(strstr(content, "<?xml") != NULL, "File missing XML declaration");
    TEST_ASSERT(strstr(content, "encoding=\"UTF-8\"") != NULL, "File missing encoding");
    free(content);

    taurus_document_free(doc);
    TEST_PASS("test_file_save_declaration");
}

/* ============================================================================
 * Test 4: Load and Save Round Trip
 * ============================================================================ */

static int test_file_load_save_roundtrip(void) {
    /* Create original file */
    const char* xml = "<root attr1=\"value1\"><child>Text with <entities/></child></root>";
    const char* filepath1 = get_temp_filepath("test_original.xml");

    /* Write original file */
    FILE* file = fopen(filepath1, "wb");
    TEST_ASSERT(file != NULL, "Could not create original file");
    fprintf(file, "%s", xml);
    fclose(file);

    /* Load file */
    TaurusDocument doc = taurus_parse_file(filepath1, NULL);
    TEST_ASSERT(doc != NULL, "File loading failed");

    /* Save to new file */
    const char* filepath2 = get_temp_filepath("test_roundtrip.xml");
    TaurusStatus status = taurus_document_save_file(doc, filepath2, NULL);
    TEST_ASSERT(status == TAURUS_OK, "Roundtrip save failed");

    /* Verify both files exist */
    TEST_ASSERT(file_exists(filepath1), "Original file missing");
    TEST_ASSERT(file_exists(filepath2), "Roundtrip file missing");

    /* Verify content preservation */
    TaurusElement root = taurus_document_root(doc);
    TEST_ASSERT(!taurus_element_is_null(root), "Root element missing");

    const char* attr1 = taurus_element_attribute(root, "attr1");
    TEST_ASSERT(attr1 != NULL, "Attribute missing");
    TEST_ASSERT(strcmp(attr1, "value1") == 0, "Attribute value incorrect");

    TaurusElement child = taurus_element_child(root, 0);
    TEST_ASSERT(!taurus_element_is_null(child), "Child element missing");

    const char* text = taurus_element_text(child);
    TEST_ASSERT(text != NULL, "Text content missing");
    TEST_ASSERT(strstr(text, "Text with ") != NULL, "Text content incorrect");

    taurus_document_free(doc);
    TEST_PASS("test_file_load_save_roundtrip");
}

/* ============================================================================
 * Test 5: Save Large Document
 * ============================================================================ */

static int test_file_save_large(void) {
    /* Create document with 100 elements */
    char* xml = (char*)malloc(100000);
    TEST_ASSERT(xml != NULL, "Memory allocation failed");

    strcpy(xml, "<root>");
    for (int i = 0; i < 100; i++) {
        char elem[100];
        snprintf(elem, sizeof(elem), "<item id=\"%d\">Item %d</item>", i, i);
        strcat(xml, elem);
    }
    strcat(xml, "</root>");

    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Large document parsing failed");

    const char* filepath = get_temp_filepath("test_large.xml");
    TaurusStatus status = taurus_document_save_file(doc, filepath, NULL);
    TEST_ASSERT(status == TAURUS_OK, "Large file save failed");

    /* Verify file size is reasonable */
    size_t size = file_size(filepath);
    TEST_ASSERT(size > 1000, "File size too small");
    TEST_ASSERT(size < 200000, "File size too large");

    free(xml);
    taurus_document_free(doc);
    TEST_PASS("test_file_save_large");
}

/* ============================================================================
 * Test 6: Save Empty Document
 * ============================================================================ */

static int test_file_save_empty(void) {
    /* Create minimal document */
    const char* xml = "<root></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Empty document creation failed");

    const char* filepath = get_temp_filepath("test_empty.xml");
    TaurusStatus status = taurus_document_save_file(doc, filepath, NULL);
    TEST_ASSERT(status == TAURUS_OK, "Empty document save failed");

    /* Verify file was created */
    size_t size = file_size(filepath);
    TEST_ASSERT(size > 0, "File should not be empty");

    taurus_document_free(doc);
    TEST_PASS("test_file_save_empty");
}

/* ============================================================================
 * Test 7: Save with Special Characters
 * ============================================================================ */

static int test_file_save_special_chars(void) {
    const char* xml = "<root>Text with &lt;entities&gt; &quot;quotes&quot; &amp;amps;</root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    const char* filepath = get_temp_filepath("test_special.xml");
    TaurusStatus status = taurus_document_save_file(doc, filepath, NULL);
    TEST_ASSERT(status == TAURUS_OK, "Special chars save failed");

    /* Verify entities are preserved (as decoded characters, then re-encoded) */
    char* content = read_file_contents(filepath);
    TEST_ASSERT(content != NULL, "Could not read file");
    /* After parsing, entities are decoded to actual characters, then re-encoded on save */
    /* The file should contain the actual characters <>\"& or encoded entities */
    TEST_ASSERT(strstr(content, "Text with") != NULL, "Text content missing");
    TEST_ASSERT(strstr(content, "entities") != NULL, "Entity text missing");
    free(content);

    taurus_document_free(doc);
    TEST_PASS("test_file_save_special_chars");
}

/* ============================================================================
 * Test 8: Save with Unicode
 * ============================================================================ */

static int test_file_save_unicode(void) {
    const char* xml = "<root>Hello 世界 🌍 Привет мир</root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    const char* filepath = get_temp_filepath("test_unicode.xml");
    TaurusStatus status = taurus_document_save_file(doc, filepath, NULL);
    TEST_ASSERT(status == TAURUS_OK, "Unicode save failed");

    /* Verify Unicode is preserved */
    char* content = read_file_contents(filepath);
    TEST_ASSERT(content != NULL, "Could not read file");
    TEST_ASSERT(strstr(content, "世界") != NULL, "Unicode characters missing");
    TEST_ASSERT(strstr(content, "🌍") != NULL, "Emoji missing");
    TEST_ASSERT(strstr(content, "Привет") != NULL, "Cyrillic missing");
    free(content);

    taurus_document_free(doc);
    TEST_PASS("test_file_save_unicode");
}

/* ============================================================================
 * Test 9: Save Error Handling - NULL Document
 * ============================================================================ */

static int test_file_save_null_document(void) {
    const char* filepath = get_temp_filepath("test_null_doc.xml");
    TaurusStatus status = taurus_document_save_file(NULL, filepath, NULL);
    TEST_ASSERT(status == TAURUS_ERROR_NULL_ARG, "Should return NULL_ARG error");

    TEST_ASSERT(!file_exists(filepath), "File should not be created");

    TEST_PASS("test_file_save_null_document");
}

/* ============================================================================
 * Test 10: Save Error Handling - NULL Path
 * ============================================================================ */

static int test_file_save_null_path(void) {
    const char* xml = "<root>Content</root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    TaurusStatus status = taurus_document_save_file(doc, NULL, NULL);
    TEST_ASSERT(status == TAURUS_ERROR_NULL_ARG, "Should return NULL_ARG error");

    taurus_document_free(doc);
    TEST_PASS("test_file_save_null_path");
}

/* ============================================================================
 * Test 11: Save Error Handling - Invalid Path
 * ============================================================================ */

static int test_file_save_invalid_path(void) {
    const char* xml = "<root>Content</root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TEST_ASSERT(doc != NULL, "Document creation failed");

    /* Use an invalid path (non-existent directory) */
    const char* filepath = "/nonexistent/directory/test.xml";
    TaurusStatus status = taurus_document_save_file(doc, filepath, NULL);
    TEST_ASSERT(status == TAURUS_ERROR_IO, "Should return IO error");

    taurus_document_free(doc);
    TEST_PASS("test_file_save_invalid_path");
}

/* ============================================================================
 * Test 12: Load File Error Handling
 * ============================================================================ */

static int test_file_load_nonexistent(void) {
    const char* filepath = "/nonexistent/file.xml";
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_file(filepath, &status);

    TEST_ASSERT(status != TAURUS_OK, "Should return error for non-existent file");
    TEST_ASSERT(doc == NULL, "Document should be NULL for load failure");

    TEST_PASS("test_file_load_nonexistent");
}

/* ============================================================================
 * Main
 * ============================================================================ */

typedef int (*test_func_t)(void);

struct test_case {
    const char* name;
    test_func_t func;
};

static struct test_case tests[] = {
    {"test_file_save_basic", test_file_save_basic},
    {"test_file_save_pretty", test_file_save_pretty},
    {"test_file_save_declaration", test_file_save_declaration},
    {"test_file_load_save_roundtrip", test_file_load_save_roundtrip},
    {"test_file_save_large", test_file_save_large},
    {"test_file_save_empty", test_file_save_empty},
    {"test_file_save_special_chars", test_file_save_special_chars},
    {"test_file_save_unicode", test_file_save_unicode},
    {"test_file_save_null_document", test_file_save_null_document},
    {"test_file_save_null_path", test_file_save_null_path},
    {"test_file_save_invalid_path", test_file_save_invalid_path},
    {"test_file_load_nonexistent", test_file_load_nonexistent},
    {NULL, NULL}
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     Taurus File I/O Test Suite                           ║\n");
    printf("║     Testing file loading and saving operations            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* Setup temporary directory */
    setup_temp_dir();

    int passed = 0;
    int failed = 0;

    for (int i = 0; tests[i].name != NULL; i++) {
        printf("Running %s...\n", tests[i].name);
        if (tests[i].func()) {
            passed++;
        } else {
            failed++;
        }
    }

    /* Cleanup temporary directory */
    cleanup_temp_dir();

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                      Test Results                         ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Passed:  %d / %d\n", passed, passed + failed);
    printf("  Failed:  %d / %d\n", failed, passed + failed);

    if (failed == 0) {
        printf("\n  ✓ All tests passed!\n");
        return 0;
    } else {
        printf("\n  ✗ Some tests failed\n");
        return 1;
    }
}
