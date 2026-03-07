/**
 * @file test_cli_parse.cc
 * @brief Tests for the 'parse' CLI command
 */

#include "cli_test_base.h"

using namespace taurus::test;

class ParseCommandTest : public CLITestBase {};

// ============================================================================
// Basic Parsing Tests
// ============================================================================

TEST_F(ParseCommandTest, ParseSimpleXML) {
    auto result = Execute({"parse", FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "<root>");
    AssertContains(result.stdout_output, "<item");
    AssertContains(result.stdout_output, "id=\"1\"");
}

TEST_F(ParseCommandTest, ParseWithAttributes) {
    auto result = Execute({"parse", FixturePath("books.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "id=\"1\"");
    AssertContains(result.stdout_output, "category=\"fiction\"");
}

TEST_F(ParseCommandTest, ParseWithNamespaces) {
    auto result = Execute({"parse", FixturePath("namespaces.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "xmlns=");
    AssertContains(result.stdout_output, "xmlns:ex=");
}

TEST_F(ParseCommandTest, ParseLargeDocument) {
    auto result = Execute({"parse", FixturePath("large.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "<inventory>");
    AssertContains(result.stdout_output, "<item");
}

TEST_F(ParseCommandTest, ParsePreservesStructure) {
    auto result = Execute({"parse", FixturePath("simple.xml")});
    AssertSuccess(result);
    
    // Should have proper XML structure
    AssertContains(result.stdout_output, "<?xml");
    AssertContains(result.stdout_output, "<root>");
    AssertContains(result.stdout_output, "</root>");
}

// ============================================================================
// Format Options Tests
// ============================================================================

TEST_F(ParseCommandTest, ParseToXML) {
    auto result = Execute({"parse", "--format", "xml", FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "<?xml");
    AssertContains(result.stdout_output, "<root>");
}

TEST_F(ParseCommandTest, ParseToJSON) {
    auto result = Execute({"parse", "--format", "json", FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "\"name\"");
    AssertContains(result.stdout_output, "\"root\"");
}

TEST_F(ParseCommandTest, ParseToText) {
    auto result = Execute({"parse", "--format", "text", FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "root");
    AssertContains(result.stdout_output, "item");
}

TEST_F(ParseCommandTest, FormatJSONWithAttributes) {
    auto result = Execute({"parse", "--format", "json", FixturePath("books.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "\"attributes\"");
    AssertContains(result.stdout_output, "\"id\"");
}

TEST_F(ParseCommandTest, FormatOptionShorthand) {
    auto result = Execute({"parse", "-f", "json", FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "\"name\"");
}

// ============================================================================
// Stdin Input Tests
// ============================================================================

TEST_F(ParseCommandTest, ParseFromStdin) {
    std::string xml = "<root><item id=\"1\">test</item></root>";
    auto result = ExecuteWithStdin({"parse", "-"}, xml);
    AssertSuccess(result);
    AssertContains(result.stdout_output, "<root>");
    AssertContains(result.stdout_output, "id=\"1\"");
}

TEST_F(ParseCommandTest, ParseFromStdinWithFormat) {
    std::string xml = "<root><item>test</item></root>";
    auto result = ExecuteWithStdin({"parse", "--format", "json", "-"}, xml);
    AssertSuccess(result);
    AssertContains(result.stdout_output, "\"name\"");
    AssertContains(result.stdout_output, "\"root\"");
}

TEST_F(ParseCommandTest, ParseEmptyStdin) {
    auto result = ExecuteWithStdin({"parse", "-"}, "");
    // Should fail with parse error (exit code 1)
    EXPECT_NE(result.exit_code, 0);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(ParseCommandTest, ParseMalformedXML) {
    auto result = Execute({"parse", FixturePath("malformed.xml")});
    // Should fail with parse error (exit code 1 or 2)
    EXPECT_NE(result.exit_code, 0);
    EXPECT_TRUE(result.exit_code == 1 || result.exit_code == 2)
        << "Expected exit code 1 or 2, got " << result.exit_code;
}

TEST_F(ParseCommandTest, ParseMissingFile) {
    auto result = Execute({"parse", "nonexistent.xml"});
    // Should fail with I/O error (exit code 3)
    EXPECT_NE(result.exit_code, 0);
}

TEST_F(ParseCommandTest, ParseInvalidFormat) {
    auto result = Execute({"parse", "--format", "invalid", FixturePath("simple.xml")});
    // Should fail with invalid arguments (exit code 4)
    EXPECT_NE(result.exit_code, 0);
}

TEST_F(ParseCommandTest, ParseMissingArgument) {
    auto result = Execute({"parse"});
    // Should fail - no file specified
    EXPECT_NE(result.exit_code, 0);
}

// ============================================================================
// Options Tests
// ============================================================================

TEST_F(ParseCommandTest, ParseWithVerbose) {
    auto result = Execute({"parse", "--verbose", FixturePath("simple.xml")});
    AssertSuccess(result);
    // Verbose output should be in stderr
    EXPECT_FALSE(result.stderr_output.empty());
}

TEST_F(ParseCommandTest, ParseWithQuiet) {
    auto result = Execute({"parse", "--quiet", FixturePath("simple.xml")});
    AssertSuccess(result);
    // Quiet mode should suppress warnings (stderr should be minimal)
}

TEST_F(ParseCommandTest, HelpOption) {
    auto result = Execute({"parse", "--help"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "parse");
    AssertContains(result.stdout_output, "Usage");
}
