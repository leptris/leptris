/**
 * @file cli_test_base.h
 * @brief Base test fixture for CLI tests
 *
 * Provides utilities for executing CLI commands and validating output.
 */

#ifndef TAURUS_CLI_TEST_BASE_H
#define TAURUS_CLI_TEST_BASE_H

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace taurus {
namespace test {

/**
 * Result of a CLI command execution
 */
struct CLIResult {
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
    
    CLIResult() : exit_code(-1) {}
};

/**
 * Base test fixture for CLI tests
 * 
 * Provides common utilities for:
 * - Executing CLI commands
 * - Loading test fixtures
 * - Asserting on output
 */
class CLITestBase : public ::testing::Test {
protected:
    void SetUp() override;
    void TearDown() override;
    
    /**
     * Execute CLI command with arguments
     * @param args Command arguments (not including binary name)
     * @return Result containing exit code and output
     */
    CLIResult Execute(const std::vector<std::string>& args);
    
    /**
     * Execute CLI command with stdin input
     * @param args Command arguments
     * @param stdin_data Data to pipe to stdin
     * @return Result containing exit code and output
     */
    CLIResult ExecuteWithStdin(const std::vector<std::string>& args, 
                               const std::string& stdin_data);
    
    /**
     * Load fixture file path
     * @param filename Fixture filename
     * @return Full path to fixture
     */
    std::string FixturePath(const std::string& filename);
    
    /**
     * Read fixture file content
     * @param filename Fixture filename
     * @return File content
     */
    std::string ReadFixture(const std::string& filename);
    
    /**
     * Create temporary file with content
     * @param content File content
     * @return Path to temporary file
     */
    std::string CreateTempFile(const std::string& content);
    
    /**
     * Assertion helpers
     */
    void AssertSuccess(const CLIResult& result);
    void AssertFailure(const CLIResult& result, int expected_code);
    void AssertContains(const std::string& text, const std::string& pattern);
    void AssertNotContains(const std::string& text, const std::string& pattern);
    
    /**
     * Paths
     */
    std::string cli_path_;
    std::string fixtures_dir_;
    std::vector<std::string> temp_files_;
};

} // namespace test
} // namespace taurus

#endif // TAURUS_CLI_TEST_BASE_H