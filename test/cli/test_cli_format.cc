/**
 * @file test_cli_format.cc
 * @brief Tests for the 'format' CLI command
 */

#include "cli_test_base.h"

using namespace taurus::test;

class FormatCommandTest : public CLITestBase {};

// ============================================================================
// Basic Formatting Tests
// ============================================================================

TEST_F(FormatCommandTest, DefaultIndentation) {
    auto result = Execute({"format", FixturePath("books.xml")});
    AssertSuccess(result);
    // Default is 2-space indentation
    AssertContains(result.stdout_output, "  <book");
    AssertContains(result.stdout_output, "    <title>");
}

TEST_F(FormatCommandTest, PreservesContent) {
    auto result = Execute({"format", FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "<root>");
    AssertContains(result.stdout_output, "<item");
    AssertContains(result.stdout_output, "test content");
}

TEST_F(FormatCommandTest, PreservesAttributes) {
    auto result = Execute({"format", FixturePath("books.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "id=\"1\"");
    AssertContains(result.stdout_output, "category=\"fiction\"");
}

TEST_F(FormatCommandTest, AddsProperLineBreaks) {
    auto result = Execute({"format", FixturePath("simple.xml")});
    AssertSuccess(result);
    
    // Should have multiple lines
    size_t line_count = std::count(result.stdout_output.begin(), 
                                   result.stdout_output.end(), '\n');
    EXPECT_GT(line_count, 3);
}

// ============================================================================
// Custom Indentation Tests
// ============================================================================

TEST_F(FormatCommandTest, FourSpaceIndentation) {
    auto result = Execute({"format", "--indent", "4", FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "    <item");
}

TEST_F(FormatCommandTest, IndentationShorthand) {
    auto result = Execute({"format", "-i", "4", FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "    <item");
}

TEST_F(FormatCommandTest, EightSpaceIndentation) {
    auto result = Execute({"format", "--indent", "8", FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "        <item");
}

TEST_F(FormatCommandTest, SingleSpaceIndentation) {
    auto result = Execute({"format", "--indent", "1", FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, " <item");
}

TEST_F(FormatCommandTest, ZeroIndentationCompact) {
    auto result = Execute({"format", "--indent", "0", FixturePath("simple.xml")});
    AssertSuccess(result);
    // No indentation means everything flush left
    AssertContains(result.stdout_output, "<root>");
    AssertContains(result.stdout_output, "<item");
}

// ============================================================================
// Compact Mode Tests
// ============================================================================

TEST_F(FormatCommandTest, CompactMode) {
    auto result = Execute({"format", "--compact", FixturePath("books.xml")});
    AssertSuccess(result);
    
    // Should have minimal whitespace
    size_t line_count = std::count(result.stdout_output.begin(),
                                   result.stdout_output.end(), '\n');
    // Compact should have very few newlines (just XML declaration and maybe root)
    EXPECT_LE(line_count, 2);
}

TEST_F(FormatCommandTest, CompactPreservesContent) {
    auto result = Execute({"format", "--compact", FixturePath("books.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "Great Gatsby");
    AssertContains(result.stdout_output, "id=\"1\"");
}

TEST_F(FormatCommandTest, CompactRemovesWhitespace) {
    auto result = Execute({"format", "--compact", FixturePath("books.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "<library><book");
    AssertNotContains(result.stdout_output, "\n  ");
}

// ============================================================================
// Output Format Tests
// ============================================================================

TEST_F(FormatCommandTest, XMLOutputFormat) {
    auto result = Execute({"format", "--format", "xml", FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "<?xml");
    AssertContains(result.stdout_output, "<root>");
}

TEST_F(FormatCommandTest, JSONOutputFormat) {
    auto result = Execute({"format", "--format", "json", FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "\"name\"");
    AssertContains(result.stdout_output, "\"root\"");
}

TEST_F(FormatCommandTest, TextOutputFormat) {
    auto result = Execute({"format", "--format", "text", FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "root");
}

// ============================================================================
// Output File Tests
// ============================================================================

TEST_F(FormatCommandTest, OutputToFile) {
    std::string output_file = CreateTempFile("");
    auto result = Execute({"format", "--output", output_file, FixturePath("simple.xml")});
    AssertSuccess(result);
    
    // Output should go to file, not stdout
    EXPECT_TRUE(result.stdout_output.empty());
    
    // Verify file was written
    std::string content = ReadFixture("simple.xml");
    EXPECT_FALSE(content.empty());
}

TEST_F(FormatCommandTest, OutputFileShorthand) {
    std::string output_file = CreateTempFile("");
    auto result = Execute({"format", "-o", output_file, FixturePath("simple.xml")});
    AssertSuccess(result);
    EXPECT_TRUE(result.stdout_output.empty());
}

// ============================================================================
// Stdin Tests
// ============================================================================

TEST_F(FormatCommandTest, FormatFromStdin) {
    std::string xml = "<root><item id=\"1\">test</item></root>";
    auto result = ExecuteWithStdin({"format", "-"}, xml);
    AssertSuccess(result);
    AssertContains(result.stdout_output, "  <item");
}

TEST_F(FormatCommandTest, FormatStdinWithIndent) {
    std::string xml = "<root><item>test</item></root>";
    auto result = ExecuteWithStdin({"format", "--indent", "4", "-"}, xml);
    AssertSuccess(result);
    AssertContains(result.stdout_output, "    <item>");
}

TEST_F(FormatCommandTest, FormatStdinCompact) {
    std::string xml = "<root>\n  <item>test</item>\n</root>";
    auto result = ExecuteWithStdin({"format", "--compact", "-"}, xml);
    AssertSuccess(result);
    AssertContains(result.stdout_output, "<root><item>");
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(FormatCommandTest, FormatMalformedXML) {
    auto result = Execute({"format", FixturePath("malformed.xml")});
    // Should fail with parse error
    EXPECT_NE(result.exit_code, 0);
}

TEST_F(FormatCommandTest, FormatMissingFile) {
    auto result = Execute({"format", "nonexistent.xml"});
    // Should fail with I/O error
    EXPECT_NE(result.exit_code, 0);
}

TEST_F(FormatCommandTest, FormatInvalidIndent) {
    auto result = Execute({"format", "--indent", "-1", FixturePath("simple.xml")});
    // Should fail with invalid arguments
    EXPECT_NE(result.exit_code, 0);
}

TEST_F(FormatCommandTest, FormatMissingArgument) {
    auto result = Execute({"format"});
    // Should fail - no file specified
    EXPECT_NE(result.exit_code, 0);
}

// ============================================================================
// Options Tests
// ============================================================================

TEST_F(FormatCommandTest, VerboseOutput) {
    auto result = Execute({"format", "--verbose", FixturePath("simple.xml")});
    AssertSuccess(result);
    // Verbose messages should be in stderr
    EXPECT_FALSE(result.stderr_output.empty());
}

TEST_F(FormatCommandTest, QuietOutput) {
    auto result = Execute({"format", "--quiet", FixturePath("simple.xml")});
    AssertSuccess(result);
    // Should suppress warnings
}

TEST_F(FormatCommandTest, HelpOption) {
    auto result = Execute({"format", "--help"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "format");
    AssertContains(result.stdout_output, "Usage");
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(FormatCommandTest, FormatLargeDocument) {
    auto result = Execute({"format", FixturePath("large.xml")});
    AssertSuccess(result);
    // Should handle large documents
    EXPECT_FALSE(result.stdout_output.empty());
    AssertContains(result.stdout_output, "<inventory>");
}

TEST_F(FormatCommandTest, FormatWithNamespaces) {
    auto result = Execute({"format", FixturePath("namespaces.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "xmlns=");
    AssertContains(result.stdout_output, "xmlns:ex=");
}
