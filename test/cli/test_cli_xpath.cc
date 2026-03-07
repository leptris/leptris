/**
 * @file test_cli_xpath.cc
 * @brief Tests for the 'xpath' CLI command
 */

#include "cli_test_base.h"

using namespace taurus::test;

class XPathCommandTest : public CLITestBase {};

// ============================================================================
// Basic Query Tests
// ============================================================================

TEST_F(XPathCommandTest, SimplePathQuery) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//book"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "id=\"1\"");
    AssertContains(result.stdout_output, "id=\"2\"");
}

TEST_F(XPathCommandTest, DescendantSearch) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//title"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "Great Gatsby");
    AssertContains(result.stdout_output, "Brief History");
}

TEST_F(XPathCommandTest, AttributeAccess) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//book/@id"});
    AssertSuccess(result);
    // Should contain attribute values
    EXPECT_FALSE(result.stdout_output.empty());
}

TEST_F(XPathCommandTest, TextExtraction) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//title/text()"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "Great Gatsby");
    AssertContains(result.stdout_output, "Brief History");
}

TEST_F(XPathCommandTest, MultipleResults) {
    auto result = Execute({"xpath", FixturePath("large.xml"), "//item"});
    AssertSuccess(result);
    // Should have multiple item elements
    AssertContains(result.stdout_output, "id=\"1\"");
    AssertContains(result.stdout_output, "id=\"5\"");
}

TEST_F(XPathCommandTest, NestedPath) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "/library/book/title"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "title");
}

// ============================================================================
// Predicate Tests
// ============================================================================

TEST_F(XPathCommandTest, PositionPredicateFirst) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//book[1]"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "id=\"1\"");
    AssertNotContains(result.stdout_output, "id=\"2\"");
}

TEST_F(XPathCommandTest, PositionPredicateLast) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//book[last()]"});
    AssertSuccess(result);
    // Should contain the last book
    AssertContains(result.stdout_output, "id=\"4\"");
}

TEST_F(XPathCommandTest, PositionPredicateSpecific) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//book[2]"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "id=\"2\"");
    AssertNotContains(result.stdout_output, "id=\"1\"");
}

TEST_F(XPathCommandTest, BooleanPredicateAttribute) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//book[@id]"});
    AssertSuccess(result);
    // Should contain all books with id attribute
    AssertContains(result.stdout_output, "book");
}

TEST_F(XPathCommandTest, BooleanPredicateElement) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//book[title]"});
    AssertSuccess(result);
    // Should contain books with title elements
    AssertContains(result.stdout_output, "book");
}

TEST_F(XPathCommandTest, ComparisonPredicateGreaterThan) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//book[@price > 15]"});
    AssertSuccess(result);
    // Should only contain books with price > 15
    AssertContains(result.stdout_output, "id=\"2\"");  // 18.99
    AssertContains(result.stdout_output, "id=\"4\"");  // 16.99
}

TEST_F(XPathCommandTest, ComparisonPredicateLessThan) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//book[@price < 15]"});
    AssertSuccess(result);
    // Should contain books with price < 15
    AssertContains(result.stdout_output, "id=\"1\"");  // 12.99
    AssertContains(result.stdout_output, "id=\"3\"");  // 14.99
}

TEST_F(XPathCommandTest, ComparisonPredicateEquals) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//book[@category='fiction']"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "id=\"1\"");
    AssertContains(result.stdout_output, "id=\"3\"");
}

TEST_F(XPathCommandTest, MultiplePredicates) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//book[1][@id]"});
    AssertSuccess(result);
    // First book with id attribute
    AssertContains(result.stdout_output, "id=\"1\"");
}

TEST_F(XPathCommandTest, NestedPredicate) {
    auto result = Execute({"xpath", FixturePath("large.xml"), 
                          "//item[@quantity > 100][@price < 30]"});
    AssertSuccess(result);
    // Items with quantity > 100 AND price < 30
    EXPECT_FALSE(result.stdout_output.empty());
}

// ============================================================================
// Function Tests
// ============================================================================

TEST_F(XPathCommandTest, CountFunction) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "count(//book)"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "4");
}

TEST_F(XPathCommandTest, LastFunction) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//book[position()=last()]"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "id=\"4\"");
}

TEST_F(XPathCommandTest, StringFunction) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "string(//book[1]/title)"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "Great Gatsby");
}

TEST_F(XPathCommandTest, ContainsFunction) {
    auto result = Execute({"xpath", FixturePath("books.xml"), 
                          "//book[contains(title, 'History')]"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "id=\"2\"");
}

TEST_F(XPathCommandTest, StartsWithFunction) {
    auto result = Execute({"xpath", FixturePath("books.xml"),
                          "//book[starts-with(title, 'The')]"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "Great Gatsby");
    AssertContains(result.stdout_output, "Selfish Gene");
}

// ============================================================================
// Output Format Tests
// ============================================================================

TEST_F(XPathCommandTest, XMLOutputFormat) {
    auto result = Execute({"xpath", "--format", "xml", 
                          FixturePath("books.xml"), "//book"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "<book");
}

TEST_F(XPathCommandTest, JSONOutputFormat) {
    auto result = Execute({"xpath", "--format", "json",
                          FixturePath("books.xml"), "//book[1]"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "\"name\"");
    AssertContains(result.stdout_output, "\"book\"");
}

TEST_F(XPathCommandTest, TextOutputFormat) {
    auto result = Execute({"xpath", "--format", "text",
                          FixturePath("books.xml"), "//title"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "Great Gatsby");
}

TEST_F(XPathCommandTest, CountOutputMode) {
    auto result = Execute({"xpath", "--count", FixturePath("books.xml"), "//book"});
    AssertSuccess(result);
    EXPECT_EQ(result.stdout_output, "4\n");
}

TEST_F(XPathCommandTest, CountModeWithShorthand) {
    auto result = Execute({"xpath", "-c", FixturePath("books.xml"), "//title"});
    AssertSuccess(result);
    EXPECT_EQ(result.stdout_output, "4\n");
}

TEST_F(XPathCommandTest, CountModeEmptyResult) {
    auto result = Execute({"xpath", "--count", FixturePath("books.xml"), "//nonexistent"});
    AssertSuccess(result);
    EXPECT_EQ(result.stdout_output, "0\n");
}

TEST_F(XPathCommandTest, BooleanOutputMode) {
    auto result = Execute({"xpath", "--boolean", FixturePath("books.xml"), "//book"});
    AssertSuccess(result);
    EXPECT_EQ(result.stdout_output, "true\n");
}

TEST_F(XPathCommandTest, BooleanModeWithShorthand) {
    auto result = Execute({"xpath", "-b", FixturePath("books.xml"), "//nonexistent"});
    AssertSuccess(result);
    EXPECT_EQ(result.stdout_output, "false\n");
}

TEST_F(XPathCommandTest, BooleanModeFalse) {
    auto result = Execute({"xpath", "--boolean", FixturePath("books.xml"), "//nonexistent"});
    AssertSuccess(result);
    EXPECT_EQ(result.stdout_output, "false\n");
}

// ============================================================================
// Stdin Tests
// ============================================================================

TEST_F(XPathCommandTest, QueryFromStdin) {
    std::string xml = "<root><item id=\"1\">test</item><item id=\"2\">more</item></root>";
    auto result = ExecuteWithStdin({"xpath", "-", "//item"}, xml);
    AssertSuccess(result);
    AssertContains(result.stdout_output, "id=\"1\"");
    AssertContains(result.stdout_output, "id=\"2\"");
}

TEST_F(XPathCommandTest, StdinWithCount) {
    std::string xml = "<root><item>a</item><item>b</item></root>";
    auto result = ExecuteWithStdin({"xpath", "--count", "-", "//item"}, xml);
    AssertSuccess(result);
    EXPECT_EQ(result.stdout_output, "2\n");
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(XPathCommandTest, InvalidXPathExpression) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "///invalid["});
    // Should fail with XPath error (exit code 2)
    EXPECT_NE(result.exit_code, 0);
}

TEST_F(XPathCommandTest, EmptyResultSet) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//nonexistent"});
    AssertSuccess(result);
    // Empty result is not an error, but output should be minimal
    EXPECT_TRUE(result.stdout_output.empty() || result.stdout_output == "\n");
}

TEST_F(XPathCommandTest, MissingFileError) {
    auto result = Execute({"xpath", "nonexistent.xml", "//book"});
    // Should fail with I/O error (exit code 3)
    EXPECT_NE(result.exit_code, 0);
}

TEST_F(XPathCommandTest, MissingXPathArgument) {
    auto result = Execute({"xpath", FixturePath("books.xml")});
    // Should fail - no XPath specified
    EXPECT_NE(result.exit_code, 0);
}

// ============================================================================
// Options Tests
// ============================================================================

TEST_F(XPathCommandTest, VerboseOutput) {
    auto result = Execute({"xpath", "--verbose", FixturePath("books.xml"), "//book"});
    AssertSuccess(result);
    // Verbose messages should be in stderr
    EXPECT_FALSE(result.stderr_output.empty());
}

TEST_F(XPathCommandTest, QuietOutput) {
    auto result = Execute({"xpath", "--quiet", FixturePath("books.xml"), "//nonexistent"});
    AssertSuccess(result);
    // Quiet mode should suppress warnings
}

TEST_F(XPathCommandTest, HelpOption) {
    auto result = Execute({"xpath", "--help"});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "xpath");
    AssertContains(result.stdout_output, "Usage");
}
