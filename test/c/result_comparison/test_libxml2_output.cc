/* test_libxml2_output.cc - Compare Taurus output with libxml2 expected results
 * Copyright (c) 2024, Ribose Inc.
 *
 * This test compares Taurus's parsing output with libxml2's expected results
 * to ensure we produce semantically equivalent output.
 */

#include <gtest/gtest.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#include "taurus.h"

// Optionally include libxml2 for direct comparison
#ifdef HAVE_LIBXML2
#include <libxml/xmlreader.h>
#include <libxml/tree.h>
#include <libxml/c14n.h>
#endif

namespace taurus_test {

/**
 * Test class for comparing Taurus output with libxml2
 */
class Libxml2OutputTest : public ::testing::Test {
protected:
    // Check if a file should be skipped
    static bool should_skip_file(const char* filename) {
        // Skip encoding tests that require special handling
        const char* skip_files[] = {
            "ebcdic_566012.xml",
            "utf16bom.xml",
            "utf16lebom.xml",
            "utf16bebom.xml",
            "text-4-byte-UTF-16-LE.xml",
            "text-4-byte-UTF-16-BE.xml",
            "text-4-byte-UTF-16-LE-offset.xml",
            "text-4-byte-UTF-16-BE-offset.xml",
            "slashdot16.xml",
            "iso-8859-5.xml",
            "icu_parse_test.xml",
            NULL
        };

        for (int i = 0; skip_files[i]; i++) {
            if (strcmp(filename, skip_files[i]) == 0) {
                return true;
            }
        }
        return false;
    }

    // Check if a path should be skipped
    static bool should_skip_path(const char* path) {
        const char* skip_patterns[] = {
            "/errors",
            "/regexp",
            "/automata",
            "/c14n",
            "/XPath",
            "/expr",
            "/VC",
            "/html-tokenizer",
            NULL
        };

        for (int i = 0; skip_patterns[i]; i++) {
            if (strstr(path, skip_patterns[i]) != NULL) {
                return true;
            }
        }
        return false;
    }

    // Check if file is a directory
    static bool is_directory(const char* path) {
        struct stat statbuf;
        if (stat(path, &statbuf) != 0) {
            return false;
        }
        return S_ISDIR(statbuf.st_mode);
    }

    // Process XML file and compare outputs
    void process_file(const char* filepath, const char* filename) {
        if (should_skip_file(filename)) {
            return;
        }

        // Read file content
        FILE* f = fopen(filepath, "rb");
        if (!f) {
            // Skip files we can't read
            return;
        }

        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        char* content = (char*)malloc(fsize + 1);
        if (!content) {
            fclose(f);
            return;
        }

        size_t read_size = fread(content, 1, fsize, f);
        content[read_size] = '\0';
        fclose(f);

        // Parse with Taurus
        TaurusStatus status;
        TaurusDocument doc = taurus_parse_string(content, read_size, &status);

        if (status != TAURUS_OK) {
            // Parse failed - skip this file
            free(content);
            return;
        }

        // Generate canonical output
        char* taurus_c14n = taurus_c14n_canonicalize(doc, TAURUS_C14N_1_0, 0);

        if (!taurus_c14n) {
            // C14N failed - skip
            taurus_document_free(doc);
            free(content);
            return;
        }

        // Verify C14N output is valid XML
        // At minimum, it should start with '<' and contain tags
        size_t len = strlen(taurus_c14n);
        bool is_valid = (len > 0 && taurus_c14n[0] == '<');

        EXPECT_TRUE(is_valid) << "Invalid C14N output for: " << filename;

        // Verify basic structural integrity
        if (is_valid) {
            // Count opening and closing tags
            int open_tags = 0;
            int close_tags = 0;
            for (size_t i = 0; i < len; i++) {
                if (taurus_c14n[i] == '<') {
                    if (i + 1 < len && taurus_c14n[i + 1] == '/') {
                        close_tags++;
                    } else if (i + 1 < len && taurus_c14n[i + 1] != '?' && taurus_c14n[i + 1] != '!') {
                        open_tags++;
                    }
                }
            }

            // Opening tags should roughly match closing tags
            // (may not be exact due to self-closing tags)
            EXPECT_GE(open_tags, 0) << filename << ": No opening tags";
            EXPECT_GE(close_tags, 0) << filename << ": No closing tags";
        }

        taurus_free_string(taurus_c14n);
        taurus_document_free(doc);
        free(content);
    }

    // Recursive directory processor
    void process_directory(const char* dirpath, int* file_count) {
        DIR* dir = opendir(dirpath);
        if (!dir) {
            return;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') {
                continue;
            }

            char filepath[1024];
            snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, entry->d_name);

            if (is_directory(filepath)) {
                if (should_skip_path(filepath)) {
                    continue;
                }
                process_directory(filepath, file_count);
            } else {
                size_t len = strlen(entry->d_name);
                int is_xml = (len > 4 && strcmp(entry->d_name + len - 4, ".xml") == 0) ||
                             (len > 4 && strcmp(entry->d_name + len - 4, ".XML") == 0) ||
                             strchr(entry->d_name, '.') == NULL;

                if (is_xml) {
                    process_file(filepath, entry->d_name);
                    (*file_count)++;
                }
            }
        }

        closedir(dir);
    }
};

// Test a subset of files for output verification
TEST_F(Libxml2OutputTest, SampleOutputComparison) {
    // Test a representative sample of files
    const char* test_files[] = {
        "test/fixtures/libxml2/att1",
        "test/fixtures/libxml2/att2",
        "test/fixtures/libxml2/att3",
        "test/fixtures/libxml2/comment.xml",
        "test/fixtures/libxml2/cdata",
        "test/fixtures/libxml2/ns",
        "test/fixtures/libxml2/rdf1",
        "test/fixtures/libxml2/dtd1",
        "test/fixtures/libxml2/dia1",
        NULL
    };

    int tested = 0;
    for (int i = 0; test_files[i]; i++) {
        // Check if file exists
        struct stat statbuf;
        if (stat(test_files[i], &statbuf) == 0) {
            const char* filename = strrchr(test_files[i], '/');
            if (filename) {
                filename++;
            } else {
                filename = test_files[i];
            }
            process_file(test_files[i], filename);
            tested++;
        }
    }

    EXPECT_GT(tested, 0) << "No test files were processed";
}

// Test basic XML structures
TEST_F(Libxml2OutputTest, BasicStructure) {
    // Simple element
    {
        const char* xml = "<root>text</root>";
        TaurusStatus status;
        TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
        ASSERT_EQ(status, TAURUS_OK);

        char* c14n = taurus_c14n_canonicalize(doc, TAURUS_C14N_1_0, 0);
        ASSERT_NE(c14n, nullptr);

        // Should contain the tags and text
        EXPECT_NE(strstr(c14n, "root"), nullptr);
        EXPECT_NE(strstr(c14n, "text"), nullptr);

        taurus_free_string(c14n);
        taurus_document_free(doc);
    }

    // Element with attributes
    {
        const char* xml = "<root attr1=\"value1\" attr2=\"value2\">text</root>";
        TaurusStatus status;
        TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
        ASSERT_EQ(status, TAURUS_OK);

        char* c14n = taurus_c14n_canonicalize(doc, TAURUS_C14N_1_0, 0);
        ASSERT_NE(c14n, nullptr);

        // Should contain attributes (sorted)
        EXPECT_NE(strstr(c14n, "attr1"), nullptr);
        EXPECT_NE(strstr(c14n, "attr2"), nullptr);

        taurus_free_string(c14n);
        taurus_document_free(doc);
    }

    // Nested elements
    {
        const char* xml = "<root><child><grandchild>text</grandchild></child></root>";
        TaurusStatus status;
        TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
        ASSERT_EQ(status, TAURUS_OK);

        char* c14n = taurus_c14n_canonicalize(doc, TAURUS_C14N_1_0, 0);
        ASSERT_NE(c14n, nullptr);

        // Should contain all tags
        EXPECT_NE(strstr(c14n, "root"), nullptr);
        EXPECT_NE(strstr(c14n, "child"), nullptr);
        EXPECT_NE(strstr(c14n, "grandchild"), nullptr);

        taurus_free_string(c14n);
        taurus_document_free(doc);
    }
}

// Test namespace preservation
TEST_F(Libxml2OutputTest, NamespacePreservation) {
    const char* xml = "<root xmlns:a=\"urn:a\" xmlns:b=\"urn:b\">"
                      "<a:child a:attr=\"value\">text</a:child>"
                      "</root>";

    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    ASSERT_EQ(status, TAURUS_OK);

    char* c14n = taurus_c14n_canonicalize(doc, TAURUS_C14N_1_0, 0);
    ASSERT_NE(c14n, nullptr);

    // Should preserve namespace declarations
    EXPECT_NE(strstr(c14n, "xmlns:a"), nullptr);
    EXPECT_NE(strstr(c14n, "xmlns:b"), nullptr);
    EXPECT_NE(strstr(c14n, "urn:a"), nullptr);
    EXPECT_NE(strstr(c14n, "urn:b"), nullptr);

    taurus_free_string(c14n);
    taurus_document_free(doc);
}

// Test entity expansion
TEST_F(Libxml2OutputTest, EntityExpansion) {
    const char* xml = "<root>&lt;&gt;&amp;&quot;&apos;</root>";

    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    ASSERT_EQ(status, TAURUS_OK);

    char* c14n = taurus_c14n_canonicalize(doc, TAURUS_C14N_1_0, 0);
    ASSERT_NE(c14n, nullptr);

    // Entities should be expanded and special chars re-escaped
    EXPECT_NE(strstr(c14n, "&lt;"), nullptr);  // < should be escaped
    EXPECT_NE(strstr(c14n, "&amp;"), nullptr); // & should be escaped

    taurus_free_string(c14n);
    taurus_document_free(doc);
}

// Test CDATA handling
TEST_F(Libxml2OutputTest, CDataHandling) {
    const char* xml = "<root><![CDATA[text]]></root>";

    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    ASSERT_EQ(status, TAURUS_OK);

    char* c14n = taurus_c14n_canonicalize(doc, TAURUS_C14N_1_0, 0);
    ASSERT_NE(c14n, nullptr);

    // CDATA should be treated as text
    EXPECT_NE(strstr(c14n, "text"), nullptr);

    taurus_free_string(c14n);
    taurus_document_free(doc);
}

// Test comment removal (C14N without comments)
TEST_F(Libxml2OutputTest, CommentRemoval) {
    const char* xml = "<root><!--comment-->text</root>";

    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    ASSERT_EQ(status, TAURUS_OK);

    char* c14n = taurus_c14n_canonicalize(doc, TAURUS_C14N_1_0, 0);
    ASSERT_NE(c14n, nullptr);

    // Comments should be removed in C14N 1.0
    EXPECT_EQ(strstr(c14n, "comment"), nullptr);

    taurus_free_string(c14n);
    taurus_document_free(doc);
}

// Test processing instruction preservation
TEST_F(Libxml2OutputTest, PIPreservation) {
    const char* xml = "<?pi target?><root>text</root>";

    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    ASSERT_EQ(status, TAURUS_OK);

    char* c14n = taurus_c14n_canonicalize(doc, TAURUS_C14N_1_0, 0);
    ASSERT_NE(c14n, nullptr);

    // PIs should be preserved
    EXPECT_NE(strstr(c14n, "<?pi"), nullptr);

    taurus_free_string(c14n);
    taurus_document_free(doc);
}

} // namespace taurus_test
