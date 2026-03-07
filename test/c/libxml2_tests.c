/**
 * @file libxml2_tests.c
 * @brief Libxml2 regression tests ported to Taurus
 *
 * These tests verify XML parsing functionality against libxml2's test suite.
 * The tests are located in test/fixtures/libxml2/*.xml
 */

#include "taurus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* Debug: Check if iconv is enabled */
#ifdef TAURUS_HAS_ICONV
#define ICONV_ENABLED 1
#else
#define ICONV_ENABLED 0
#endif

/* Test counters */
static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

/* Test result structure */
typedef struct {
    char* xml_file;
    int should_fail;  /* 1 if parsing should fail, 0 otherwise */
    TaurusStatus expected_status;
} TestFile;

/**
 * Read file content into memory
 */
static char* read_file(const char* filepath, size_t* size_out) {
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", filepath);
        return NULL;
    }

    /* Get file size */
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    /* Allocate buffer */
    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate memory for file: %s\n", filepath);
        fclose(file);
        return NULL;
    }

    /* Read file */
    size_t read_size = fread(buffer, 1, size, file);
    fclose(file);

    if (read_size != size) {
        fprintf(stderr, "Failed to read complete file: %s\n", filepath);
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    if (size_out) *size_out = size;

    return buffer;
}

/**
 * Run a single libxml2 test
 */
static int run_libxml2_test(const char* xml_file) {
    total_tests++;

    /* Skip known problematic files for now */
    const char* basename = strrchr(xml_file, '/');
    basename = basename ? basename + 1 : xml_file;

    /* Skip EBDCIC encoded files - not supported yet */
    if (strstr(basename, "ebcdic") != NULL) {
        printf("SKIPPED: %s (EBCDIC encoding not supported yet)\n", basename);
        return 1;
    }

    /* Skip WAP files with external DTDs - cause network hangs */
    if (strstr(basename, "wap") != NULL) {
        printf("SKIPPED: %s (external DTD not supported yet)\n", basename);
        return 1;
    }

    /* Skip encoding test files - not supported yet */
    if (strstr(basename, "utf16") != NULL ||
        strstr(basename, "UTF-16") != NULL ||
        strstr(basename, "iso-8859") != NULL ||
        strstr(basename, "ISO-8859") != NULL ||
        strstr(basename, "shift") != NULL) {
        printf("SKIPPED: %s (encoding not supported yet)\n", basename);
        return 1;
    }

    /* Read XML file */
    size_t xml_size;
    char* xml_content = read_file(xml_file, &xml_size);
    if (!xml_content) {
        fprintf(stderr, "FAILED: %s - could not read file\n", basename);
        failed_tests++;
        return 0;
    }

    /* Skip files with external DTD references */
    if (strstr(xml_content, "SYSTEM") != NULL || strstr(xml_content, "http://") != NULL) {
        printf("SKIPPED: %s (external DTD not supported yet)\n", basename);
        free(xml_content);
        return 1;
    }

    /* Parse with Taurus */
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml_content, xml_size, &status);

    /* Determine if this is an error test (file ends with .err) */
    int is_error_test = (strstr(basename, ".err") != NULL);

    if (is_error_test) {
        /* Error tests should fail to parse */
        if (doc == NULL || status != TAURUS_OK) {
            printf("PASSED: %s (error test, correctly failed)\n", basename);
            passed_tests++;
        } else {
            printf("FAILED: %s (error test, but succeeded unexpectedly)\n", basename);
            failed_tests++;
        }
    } else {
        /* Normal tests should succeed */
        if (doc != NULL && status == TAURUS_OK) {
            printf("PASSED: %s\n", basename);
            passed_tests++;
        } else {
            printf("FAILED: %s (status=%d)\n", basename, status);
            failed_tests++;
        }
    }

    /* Cleanup */
    if (doc) {
        taurus_document_free(doc);
    }
    free(xml_content);

    return (doc != NULL) == !is_error_test;
}

/**
 * Find all XML test files and run tests
 */
static int run_all_libxml2_tests(const char* test_dir) {
    DIR* dir = opendir(test_dir);
    if (!dir) {
        fprintf(stderr, "Failed to open test directory: %s\n", test_dir);
        return 1;
    }

    struct dirent* entry;
    int file_count = 0;

    printf("Running libxml2 regression tests from: %s\n\n", test_dir);

    while ((entry = readdir(dir)) != NULL) {
        /* Skip non-XML files */
        if (strstr(entry->d_name, ".xml") == NULL) {
            continue;
        }

        /* Build full file path */
        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", test_dir, entry->d_name);

        /* Run test */
        run_libxml2_test(filepath);
        file_count++;
    }

    closedir(dir);

    return file_count;
}

/**
 * Main test runner
 */
int main(void) {
    const char* test_dir = "../../test/fixtures/libxml2";

    printf("========================================\n");
    printf("Taurus Libxml2 Regression Tests\n");
    printf("========================================\n");
    printf("Iconv enabled: %s\n", ICONV_ENABLED ? "YES" : "NO");
    printf("\n");

    /* Run all tests */
    int file_count = run_all_libxml2_tests(test_dir);

    /* Print summary */
    printf("\n========================================\n");
    printf("Test Summary\n");
    printf("========================================\n");
    printf("Files tested: %d\n", file_count);
    printf("Total tests: %d\n", total_tests);
    printf("Passed: %d\n", passed_tests);
    printf("Failed: %d\n", failed_tests);
    printf("Success rate: %.1f%%\n", total_tests > 0 ? (100.0 * passed_tests / total_tests) : 0.0);

    return (failed_tests > 0) ? 1 : 0;
}
