#!/bin/bash
# Taurus CLI Integration Tests
# Tests all three commands (parse, xpath, format) with various options
#
# KNOWN ISSUE (v0.5.1): Test hangs after first assertion on some systems
# All CLI commands work correctly when run manually - this is a test framework issue
# Workaround: Run CLI tests manually (see docs/SESSION_102_SUMMARY.md)

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0

# Helper functions
assert_exit_code() {
    local expected=$1
    local actual=$2
    local name=$3
    if [ "$actual" -eq "$expected" ]; then
        echo -e "${GREEN}✓${NC} $name"
        ((TESTS_PASSED++))
    else
        echo -e "${RED}✗${NC} $name (expected exit $expected, got $actual)"
        ((TESTS_FAILED++))
    fi
}

assert_contains() {
    local haystack=$1
    local needle=$2
    local name=$3
    if echo "$haystack" | grep -q "$needle"; then
        echo -e "${GREEN}✓${NC} $name"
        ((TESTS_PASSED++))
    else
        echo -e "${RED}✗${NC} $name (output doesn't contain '$needle')"
        echo "  Output was: $haystack"
        ((TESTS_FAILED++))
    fi
}

assert_not_contains() {
    local haystack=$1
    local needle=$2
    local name=$3
    if ! echo "$haystack" | grep -q "$needle"; then
        echo -e "${GREEN}✓${NC} $name"
        ((TESTS_PASSED++))
    else
        echo -e "${RED}✗${NC} $name (output should not contain '$needle')"
        ((TESTS_FAILED++))
    fi
}

# Setup
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../../build"
CLI="$BUILD_DIR/cli/taurus"

if [ ! -f "$CLI" ]; then
    echo -e "${RED}Error:${NC} CLI binary not found at $CLI"
    echo "Please run: cd build && cmake --build ."
    exit 1
fi

cd "$BUILD_DIR"

# Create test files
echo '<root><book id="1"><title>The Book</title><author>John Doe</author></book><book id="2"><title>Another Book</title></book></root>' > test.xml
echo '<invalid>unclosed' > invalid.xml

echo -e "${YELLOW}Running Taurus CLI Integration Tests${NC}"
echo "======================================"
echo ""

# Parse command tests
echo -e "${YELLOW}Parse Command Tests:${NC}"

# Test 1: Basic parsing
result=$($CLI parse test.xml 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "parse: basic parsing"
assert_contains "$result" "<root>" "parse: outputs XML"
assert_contains "$result" 'id="1"' "parse: includes attributes"

# Test 2: JSON format
result=$($CLI parse --format json test.xml 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "parse: JSON format option"
assert_contains "$result" '"name":"root"' "parse: JSON output"
assert_contains "$result" '"id":"1"' "parse: JSON includes attributes"

# Test 3: Text format
result=$($CLI parse --format text test.xml 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "parse: text format option"
assert_contains "$result" "root" "parse: text output"
assert_contains "$result" 'id="1"' "parse: text includes attributes"

# Test 4: --noout option
result=$($CLI parse --noout test.xml 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "parse: --noout suppresses output"
assert_not_contains "$result" "<root>" "parse: --noout no XML output"

# Test 5: Missing file
$CLI parse nonexistent.xml 2>/dev/null
exit_code=$?
assert_exit_code 3 $exit_code "parse: missing file returns I/O error (3)"

# Test 6: Invalid XML
$CLI parse invalid.xml 2>/dev/null
exit_code=$?
assert_exit_code 1 $exit_code "parse: invalid XML returns parse error (1)"

echo ""

# XPath command tests
echo -e "${YELLOW}XPath Command Tests:${NC}"

# Test 7: Basic XPath query
result=$($CLI xpath test.xml "//book" 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "xpath: basic query"
assert_contains "$result" '<book id="1">' "xpath: finds elements"

# Test 8: XPath with attribute predicate
result=$($CLI xpath test.xml "//book[@id='1']" 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "xpath: attribute predicate"
assert_contains "$result" '<book id="1">' "xpath: filters by attribute"
assert_not_contains "$result" '<book id="2">' "xpath: doesn't include non-matching"

# Test 9: --count mode
result=$($CLI xpath --count test.xml "//book" 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "xpath: --count option"
assert_contains "$result" "2" "xpath: count returns correct number"

# Test 10: --boolean mode
result=$($CLI xpath --boolean test.xml "//book[@id='1']" 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "xpath: --boolean option"
assert_contains "$result" "true" "xpath: boolean returns true for match"

# Test 11: --boolean mode (no match)
result=$($CLI xpath --boolean test.xml "//book[@id='999']" 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "xpath: --boolean no match"
assert_contains "$result" "false" "xpath: boolean returns false for no match"

# Test 12: JSON format with XPath
result=$($CLI xpath --format json test.xml "//title" 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "xpath: JSON format"
assert_contains "$result" '"name":"title"' "xpath: JSON output"

# Test 13: Missing file
$CLI xpath nonexistent.xml "//book" 2>/dev/null
exit_code=$?
assert_exit_code 3 $exit_code "xpath: missing file returns I/O error (3)"

echo ""

# Format command tests
echo -e "${YELLOW}Format Command Tests:${NC}"

# Test 14: Basic formatting
result=$($CLI format test.xml 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "format: basic formatting"
assert_contains "$result" "<root>" "format: outputs XML"

# Test 15: JSON format
result=$($CLI format --format json test.xml 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "format: JSON output"
assert_contains "$result" '"name":"root"' "format: JSON structure"

# Test 16: Text format
result=$($CLI format --format text test.xml 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "format: text output"
assert_contains "$result" "root" "format: text structure"

# Test 17: Custom indent
result=$($CLI format --indent 4 test.xml 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "format: custom indent"

# Test 18: Compact mode
result=$($CLI format --compact test.xml 2>&1)
exit_code=$?
assert_exit_code 0 $exit_code "format: compact mode"

echo ""

# Cleanup
rm -f test.xml invalid.xml

# Summary
echo "======================================"
echo -e "${GREEN}Tests passed: $TESTS_PASSED${NC}"
if [ $TESTS_FAILED -gt 0 ]; then
    echo -e "${RED}Tests failed: $TESTS_FAILED${NC}"
else
    echo -e "${GREEN}Tests failed: $TESTS_FAILED${NC}"
fi
echo ""

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed! ✅${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed ❌${NC}"
    exit 1
fi