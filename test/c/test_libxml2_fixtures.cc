#include <gtest/gtest.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

// Use internal API since public API wrapper doesn't exist yet
extern "C" {
struct taurus_document;
typedef int TaurusStatus;
const TaurusStatus TAURUS_OK = 0;
const TaurusStatus TAURUS_ERROR_NOT_FOUND = -6;
struct taurus_document* taurus_parse(const char* xml, size_t len);
struct taurus_document* taurus_parse_string(const char* xml, size_t len, TaurusStatus* status);
struct taurus_document* taurus_parse_string_with_encoding(const char* xml, size_t len, TaurusStatus* status);
void taurus_document_free(struct taurus_document* doc);
}

// Helper function to check if a file is a directory
static int is_directory(const char* path) {
    struct stat statbuf;
    if (stat(path, &statbuf) != 0) {
        return 0;
    }
    return S_ISDIR(statbuf.st_mode);
}

// Helper function to read file contents
static char* read_file(const char* filepath, size_t* out_len) {
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* content = (char*)malloc(fsize + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(content, 1, fsize, f);
    content[read] = '\0';
    fclose(f);

    if (out_len) {
        *out_len = read;
    }

    return content;
}

TEST(Libxml2Fixtures, ParseAllDocuments) {
    const char* fixtures_dir = "test/fixtures/libxml2";
    DIR* dir = opendir(fixtures_dir);
    ASSERT_NE(dir, nullptr) << "Could not open libxml2 fixtures directory";

    int total_files = 0;
    int parsed_files = 0;
    int failed_files = 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip directories and README
        if (entry->d_name[0] == '.' ||
            strcmp(entry->d_name, "README.adoc") == 0) {
            continue;
        }

        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", fixtures_dir, entry->d_name);

        // Skip directories
        if (is_directory(filepath)) {
            continue;
        }

        total_files++;

        // Try to parse the file
        size_t len;
        char* content = read_file(filepath, &len);
        if (!content) {
            fprintf(stderr, "WARNING: Could not read file: %s\n", entry->d_name);
            failed_files++;
            continue;
        }

        /* Use encoding-aware parser for UTF-16 support */
        TaurusStatus status;
        struct taurus_document* doc = taurus_parse_string_with_encoding(content, len, &status);
        free(content);

        if (doc) {
            parsed_files++;
            taurus_document_free(doc);
            // Optional: Print success for verbose testing
            // fprintf(stderr, "✓ Parsed: %s\n", entry->d_name);
        } else {
            failed_files++;
            fprintf(stderr, "✗ Failed to parse: %s\n", entry->d_name);
        }
    }

    closedir(dir);

    // Report statistics
    fprintf(stderr, "\n=== libxml2 Fixtures Parsing Statistics ===\n");
    fprintf(stderr, "Total files tested: %d\n", total_files);
    fprintf(stderr, "Successfully parsed: %d (%.1f%%)\n",
            parsed_files, 100.0 * parsed_files / total_files);
    fprintf(stderr, "Failed to parse: %d (%.1f%%)\n",
            failed_files, 100.0 * failed_files / total_files);
    fprintf(stderr, "==========================================\n\n");

    // We expect at least 95% success rate
    EXPECT_GT(parsed_files, total_files * 0.95)
        << "Expected at least 95% of fixtures to parse successfully";

    // We should have tested a reasonable number of files
    EXPECT_GT(total_files, 100)
        << "Expected to find 100+ test files in libxml2 fixtures";
}

// Test specific categories of files
TEST(Libxml2Fixtures, ParseNamespaceFiles) {
    const char* ns_files[] = {
        "test/fixtures/libxml2/ns",
        "test/fixtures/libxml2/ns2",
        "test/fixtures/libxml2/ns3",
        "test/fixtures/libxml2/ns4",
        "test/fixtures/libxml2/ns5",
        "test/fixtures/libxml2/ns6",
        "test/fixtures/libxml2/ns7",
        "test/fixtures/libxml2/nsclean.xml",
        NULL
    };

    for (int i = 0; ns_files[i] != NULL; i++) {
        size_t len;
        char* content = read_file(ns_files[i], &len);
        ASSERT_NE(content, nullptr) << "Could not read " << ns_files[i];

        struct taurus_document* doc = taurus_parse(content, len);
        free(content);

        EXPECT_NE(doc, nullptr) << "Failed to parse " << ns_files[i];
        if (doc) {
            taurus_document_free(doc);
        }
    }
}

TEST(Libxml2Fixtures, ParseAttributeFiles) {
    const char* att_files[] = {
        "test/fixtures/libxml2/att1",
        "test/fixtures/libxml2/att2",
        "test/fixtures/libxml2/att3",
        "test/fixtures/libxml2/att4",
        "test/fixtures/libxml2/att5",
        "test/fixtures/libxml2/attrib.xml",
        NULL
    };

    for (int i = 0; att_files[i] != NULL; i++) {
        size_t len;
        char* content = read_file(att_files[i], &len);
        ASSERT_NE(content, nullptr) << "Could not read " << att_files[i];

        struct taurus_document* doc = taurus_parse(content, len);
        free(content);

        EXPECT_NE(doc, nullptr) << "Failed to parse " << att_files[i];
        if (doc) {
            taurus_document_free(doc);
        }
    }
}

TEST(Libxml2Fixtures, ParseCDATAFiles) {
    const char* cdata_files[] = {
        "test/fixtures/libxml2/cdata",
        "test/fixtures/libxml2/cdata2",
        "test/fixtures/libxml2/cdata-2-byte-UTF-8.xml",
        "test/fixtures/libxml2/cdata-3-byte-UTF-8.xml",
        "test/fixtures/libxml2/cdata-4-byte-UTF-8.xml",
        "test/fixtures/libxml2/emptycdata.xml",
        "test/fixtures/libxml2/adjacent-cdata.xml",
        NULL
    };

    for (int i = 0; cdata_files[i] != NULL; i++) {
        size_t len;
        char* content = read_file(cdata_files[i], &len);
        ASSERT_NE(content, nullptr) << "Could not read " << cdata_files[i];

        struct taurus_document* doc = taurus_parse(content, len);
        free(content);

        EXPECT_NE(doc, nullptr) << "Failed to parse " << cdata_files[i];
        if (doc) {
            taurus_document_free(doc);
        }
    }
}

TEST(Libxml2Fixtures, ParseRealWorldDocuments) {
    const char* real_docs[] = {
        "test/fixtures/libxml2/rdf1",
        "test/fixtures/libxml2/svg1",
        "test/fixtures/libxml2/xhtml1",
        "test/fixtures/libxml2/slashdot.xml",
        "test/fixtures/libxml2/wap.xml",
        NULL
    };

    for (int i = 0; real_docs[i] != NULL; i++) {
        size_t len;
        char* content = read_file(real_docs[i], &len);
        ASSERT_NE(content, nullptr) << "Could not read " << real_docs[i];

        struct taurus_document* doc = taurus_parse(content, len);
        free(content);

        EXPECT_NE(doc, nullptr) << "Failed to parse " << real_docs[i];
        if (doc) {
            taurus_document_free(doc);
        }
    }
}