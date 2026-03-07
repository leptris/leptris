# CLI Testing Architecture

This directory contains comprehensive integration tests for the Taurus CLI tool using Google Test framework.

## Overview

The CLI tests execute the `taurus` binary as a subprocess and validate its behavior, output, and exit codes. This provides end-to-end testing of the CLI functionality.

## Architecture

### Base Test Infrastructure

**`cli_test_base.h/cc`** - Base test fixture providing common utilities:
- `Execute()` - Run CLI commands and capture output
- `ExecuteWithStdin()` - Run CLI commands with stdin input
- `FixturePath()` - Get path to test fixtures
- `CreateTempFile()` - Create temporary files for testing
- `AssertSuccess()`, `AssertFailure()` - Validate exit codes
- `AssertContains()`, `AssertNotContains()` - Validate output content

### Test Files

Each CLI command has its own comprehensive test suite:

1. **`test_cli_parse.cc`** (23 tests)
   - Basic XML parsing
   - Format options (XML, JSON, text)
   - Stdin input
   - Error handling
   - Options (verbose, quiet, help)

2. **`test_cli_xpath.cc`** (40 tests)
   - Basic XPath queries
   - Position predicates (`[1]`, `[last()]`)
   - Boolean predicates (`[@attr]`, `[element]`)
   - Comparison predicates (`[@price > 20]`)
   - XPath functions (`count()`, `string()`, `contains()`, etc.)
   - Output modes (--count, --boolean)
   - Format options
   - Error handling

3. **`test_cli_format.cc`** (27 tests)
   - Default formatting
   - Custom indentation (2, 4, 8 spaces)
   - Compact mode
   - Output formats (XML, JSON, text)
   - Output to file
   - Stdin input
   - Error handling

**Total: 90 CLI integration tests**

### Test Fixtures

The `fixtures/` directory contains XML test files:
- `simple.xml` - Basic XML for quick tests
- `books.xml` - Realistic data with attributes and nested elements
- `namespaces.xml` - XML with namespace declarations
- `malformed.xml` - Invalid XML for error testing
- `large.xml` - Larger document for performance testing

## Running Tests

### Build and Run All Tests

```bash
# Configure with CLI testing enabled
mkdir -p build
cd build
cmake .. -DTAURUS_BUILD_CLI=ON -DBUILD_TESTING=ON

# Build
make

# Run all CLI tests
ctest -R test_cli --output-on-failure

# Or run directly
./test_cli
```

### Run Specific Test Suites

```bash
# Parse command tests only
./test_cli --gtest_filter="ParseCommandTest.*"

# XPath command tests only
./test_cli --gtest_filter="XPathCommandTest.*"

# Format command tests only
./test_cli --gtest_filter="FormatCommandTest.*"
```

### Run Individual Tests

```bash
# Run a specific test
./test_cli --gtest_filter="ParseCommandTest.ParseSimpleXML"

# Run tests matching a pattern
./test_cli --gtest_filter="*Predicate*"
```

### Verbose Output

```bash
# Show all test output
./test_cli --gtest_print_time=1

# Show only failing tests
./test_cli --gtest_brief=1
```

## Adding New Tests

### 1. Add Test Method

Add a new test to the appropriate test file:

```cpp
TEST_F(ParseCommandTest, MyNewTest) {
    auto result = Execute({"parse", FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "expected text");
}
```

### 2. Add Test Fixture (if needed)

If your test needs a new XML file, add it to `fixtures/`:

```bash
cat > test/cli/fixtures/my_test.xml << 'EOF'
<?xml version="1.0"?>
<test>content</test>
EOF
```

Then use it in your test:

```cpp
auto result = Execute({"parse", FixturePath("my_test.xml")});
```

### 3. Rebuild and Run

```bash
cd build
make test_cli
./test_cli --gtest_filter="*MyNewTest"
```

## Testing Patterns

### Testing Success Cases

```cpp
TEST_F(XPathCommandTest, QueryWorks) {
    auto result = Execute({"xpath", FixturePath("books.xml"), "//book"});
    AssertSuccess(result);  // Exit code 0
    AssertContains(result.stdout_output, "expected content");
}
```

### Testing Error Cases

```cpp
TEST_F(ParseCommandTest, HandlesInvalidInput) {
    auto result = Execute({"parse", "nonexistent.xml"});
    EXPECT_NE(result.exit_code, 0);  // Should fail
    // Or test specific exit code
    AssertFailure(result, 3);  // I/O error
}
```

### Testing Stdin Input

```cpp
TEST_F(FormatCommandTest, FormatsFromStdin) {
    std::string xml = "<root><item>test</item></root>";
    auto result = ExecuteWithStdin({"format", "-"}, xml);
    AssertSuccess(result);
    AssertContains(result.stdout_output, "  <item>");
}
```

### Testing Output Formats

```cpp
TEST_F(ParseCommandTest, OutputsJSON) {
    auto result = Execute({"parse", "--format", "json", 
                          FixturePath("simple.xml")});
    AssertSuccess(result);
    AssertContains(result.stdout_output, "\"name\"");
}
```

## Design Principles

### Object-Oriented Architecture

- **Base class** (`CLITestBase`) provides common utilities
- **Derived classes** (`ParseCommandTest`, etc.) inherit and extend
- **MECE design**: Each test file covers one command comprehensively

### Separation of Concerns

- **Setup/teardown** in base class
- **Test logic** in derived classes
- **Fixtures** separate from test code
- **Assertions** abstracted into helper methods

### Test Independence

- Each test is self-contained
- No shared state between tests
- Temporary files cleaned up automatically
- Tests can run in any order

### Clear Test Names

- Test names describe what they test
- Grouped by feature (Basic, Predicates, Functions, etc.)
- Easy to identify failing tests

## Debugging Tests

### View Test Output

```bash
# See what CLI printed
./test_cli --gtest_filter="ParseCommandTest.ParseSimpleXML" 2>&1
```

### Check Exit Code

```cpp
TEST_F(ParseCommandTest, DebugExitCode) {
    auto result = Execute({"parse", "test.xml"});
    std::cout << "Exit code: " << result.exit_code << std::endl;
    std::cout << "STDOUT: " << result.stdout_output << std::endl;
    std::cout << "STDERR: " << result.stderr_output << std::endl;
}
```

### Verify CLI Binary

```bash
# Make sure CLI was built
ls -l build/cli/taurus

# Test manually
cd build
./cli/taurus parse test/cli/fixtures/simple.xml
```

## Continuous Integration

The CLI tests run automatically on:
- **GitHub Actions** - Ubuntu and macOS
- **Every push** to main/develop branches
- **Every pull request**

See `.github/workflows/build.yml` for CI configuration.

## Common Issues

### CLI Binary Not Found

**Error**: `CLI binary not found at: ...`

**Solution**: Ensure CLI is built:
```bash
cmake .. -DTAURUS_BUILD_CLI=ON
make taurus-cli
```

### Fixtures Not Found

**Error**: `Fixtures directory not found: ...`

**Solution**: Run tests from build directory:
```bash
cd build
./test_cli
```

### Tests Timeout

**Error**: Test exceeds 60 second timeout

**Solution**: Optimize test or increase timeout in `test/CMakeLists.txt`:
```cmake
set_tests_properties(test_cli PROPERTIES TIMEOUT 120)
```

### Google Test Not Found

**Error**: `Google Test not found - CLI tests will not be built`

**Solution**: Google Test should be in `test/vendor/googletest`. If missing:
```bash
git submodule update --init --recursive
```

## Best Practices

1. ✅ **Test both success and failure cases**
2. ✅ **Use descriptive test names**
3. ✅ **Keep tests focused** - one behavior per test
4. ✅ **Use assertion helpers** for consistent error messages
5. ✅ **Clean up resources** in teardown
6. ✅ **Test edge cases** (empty input, malformed XML, etc.)
7. ✅ **Test all command options** (--format, --count, etc.)
8. ✅ **Document complex test logic** with comments

## Performance

CLI tests are **integration tests** and slower than unit tests:
- Average: ~50ms per test
- Total suite: ~5 seconds for 90 tests
- Acceptable for CI/CD pipelines

For faster iteration during development, run specific test files:
```bash
./test_cli --gtest_filter="ParseCommandTest.*"  # ~1 second
```

## Future Enhancements

Planned improvements:
- [ ] Performance benchmarking tests
- [ ] Unicode/encoding tests
- [ ] Large file stress tests
- [ ] Concurrent execution tests
- [ ] Shell script compatibility tests