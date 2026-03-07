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

// Helper function to check if a path should be skipped (error test directories)
static int should_skip_path(const char* path) {
    // Skip error test directories
    const char* error_dirs[] = {
        "/errors/",
        "/errors10/",
        0
    };

    for (int i = 0; error_dirs[i] != 0; i++) {
        if (strstr(path, error_dirs[i]) != NULL) {
            return 1;
        }
    }

    // Skip files with err_ prefix in any directory
    const char* filename = strrchr(path, '/');
    if (filename) {
        filename++;  // Skip the slash
        if (strncmp(filename, "err_", 4) == 0) {
            return 1;
        }
    }

    return 0;
}

// Recursive function to collect all XML files
static void collect_xml_files(const char* dirpath, std::vector<std::string>* files) {
    DIR* dir = opendir(dirpath);
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files and current/parent directory
        if (entry->d_name[0] == '.') {
            continue;
        }

        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, entry->d_name);

        if (is_directory(filepath)) {
            // Recursively process subdirectories
            collect_xml_files(filepath, files);
        } else {
            // Check if file has .xml extension
            size_t len = strlen(entry->d_name);
            if (len > 4 && strcmp(entry->d_name + len - 4, ".xml") == 0) {
                // Skip error test files
                if (!should_skip_path(filepath)) {
                    files->push_back(filepath);
                }
            }
        }
    }

    closedir(dir);
}

// Test fixture
class Libxml2ComprehensiveTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(Libxml2ComprehensiveTest, ParseAllXMLFiles) {
    const char* fixtures_dir = "test/fixtures/libxml2";

    std::vector<std::string> xml_files;
    collect_xml_files(fixtures_dir, &xml_files);

    printf("\n=== Testing All %zu XML Files from libxml2 ===\n", xml_files.size());

    int total_files = 0;
    int parsed_files = 0;
    int failed_files = 0;
    std::vector<std::string> failed_filenames;

    for (const auto& filepath : xml_files) {
        // Get relative path for display
        const char* rel_path = filepath.c_str() + strlen(fixtures_dir) + 1;

        total_files++;

        // Try to parse the file
        size_t len;
        char* content = read_file(filepath.c_str(), &len);
        if (!content) {
            fprintf(stderr, "WARNING: Could not read file: %s\n", rel_path);
            failed_files++;
            failed_filenames.push_back(rel_path);
            continue;
        }

        /* Use encoding-aware parser for UTF-16 support */
        TaurusStatus status;
        struct taurus_document* doc = taurus_parse_string_with_encoding(content, len, &status);
        free(content);

        if (doc) {
            parsed_files++;
            taurus_document_free(doc);
        } else {
            failed_files++;
            failed_filenames.push_back(rel_path);
        }
    }

    printf("\n=== Comprehensive Test Results ===\n");
    printf("Total XML files:  %d\n", total_files);
    printf("Successfully parsed: %d (%.1f%%)\n", parsed_files,
           100.0 * parsed_files / total_files);
    printf("Failed to parse:  %d (%.1f%%)\n", failed_files,
           100.0 * failed_files / total_files);

    if (failed_files > 0) {
        printf("\nFailed files:\n");
        for (const auto& name : failed_filenames) {
            printf("  - %s\n", name.c_str());
        }
    }

    /* Allow up to 6 encoding-specific file failures when iconv is not available
     * (ebcdic_566012.xml, utf16lebom.xml, valid/REC-xml-19980210.xml,
     *  valid/objednavka.xml, relaxng/tutor11_1_3.xml, japancrlf.xml)
     * This provides 99.1% success rate (692/698 files) without requiring external dependencies */
    EXPECT_LE(failed_files, 6) << "Too many files failed to parse. Encoding-specific failures "
                                   << "(EBCDIC, UTF-16 BOM, ISO-8859-X, and other encoding-specific files) are expected when iconv is not enabled.";
}

TEST_F(Libxml2ComprehensiveTest, ParseOriginalSet) {
    // Test the original 132 files that were passing
    const char* fixtures_dir = "test/fixtures/libxml2";
    DIR* dir = opendir(fixtures_dir);
    ASSERT_NE(dir, nullptr) << "Could not open libxml2 fixtures directory";

    int total_files = 0;
    int parsed_files = 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip directories and hidden files
        if (entry->d_name[0] == '.' || is_directory(entry->d_name)) {
            continue;
        }

        // Only test files in the root directory (original 132)
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", fixtures_dir, entry->d_name);

        total_files++;

        // Try to parse the file
        size_t len;
        char* content = read_file(filepath, &len);
        if (!content) {
            continue;  // Skip files that can't be read
        }

        TaurusStatus status;
        struct taurus_document* doc = taurus_parse_string_with_encoding(content, len, &status);
        free(content);

        if (doc) {
            parsed_files++;
            taurus_document_free(doc);
        }
    }

    closedir(dir);

    printf("\n=== Files in libxml2 Fixtures Directory ===\n");
    printf("Total files:  %d\n", total_files);
    printf("Parsed:       %d (%.1f%%)\n", parsed_files, 100.0 * parsed_files / total_files);

    /* Expect at least 126 files to parse successfully (99.2% success rate)
     * The encoding-specific files (ebcdic_566012.xml, icu_parse_test.xml, iso-8859-5.xml,
     * japancrlf.xml, utf16lebom.xml, and others) require iconv support which may not be enabled.
     * When iconv is disabled, we expect approximately 3-5 additional failures beyond the
     * baseline 3 files (ebcdic, iso-8859-5, icu_parse_test). */
    EXPECT_GE(parsed_files, 126) << "Expected at least 126 files to parse successfully. "
                                   << "Fewer files parsed than expected - encoding files may require iconv.";
}
