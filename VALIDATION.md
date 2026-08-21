# Leptris Validation Commands

## Quick Validation (Run all tests and benchmarks)

```bash
# Run the validation script
./scripts/validate.sh
```

## Individual Test Commands

### Run all C tests
```bash
cd build
ctest --test-dir build --output-on-failure
```

### Run specific test suites
```bash
# DOM tests
./build/test/c/test_dom

# XPath tests
./build/test/xpath/test_xpath

# Parser tests
./build/test/test_parse

# CLI tests
./build/test/cli/test_cli_commands
```

### Run benchmarks
```bash
# DOM benchmark
./build/benchmarks/dom_benchmark benchmarks/fixtures/small.xml 1000

# DOM modify benchmark
./build/benchmarks/bench_dom_pugixml

# DOM benchmark v2 (parse once, measure operations)
./build/benchmarks/dom_benchmark_v2
```

## Memory Leak Detection

### macOS
```bash
leaks --atExit -- ./build/test/c/test_dom
```

### Linux (valgrind)
```bash
valgrind --leak-check=full --error-exitcode=1 ./build/test/c/test_dom
```

## CLI Testing

```bash
# Parse and display XML
./build/cli/leptris parse test/fixtures/libxml2/svg1

# XPath query
./build/cli/leptris xpath test/fixtures/libxml2/svg1 "//svg"

# Format XML
./build/cli/leptris format --indent 2 test/fixtures/libxml2/svg1
```

## Build Commands

### Minimal build (no optional features)
```bash
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DLEPTRIS_BUILD_CLI=ON \
    -DLEPTRIS_ENABLE_UTF8PROC=OFF \
    -DLEPTRIS_ENABLE_ICONV=OFF
cmake --build build
```

### Full build with all features
```bash
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DLEPTRIS_BUILD_CLI=ON \
    -DLEPTRIS_ENABLE_UTF8PROC=ON \
    -DLEPTRIS_ENABLE_ICONV=ON \
    -DLEPTRIS_BUILD_BENCHMARKS=ON \
    -DBUILD_TESTING=ON
cmake --build build
```

### Build with vcpkg dependencies
```bash
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DLEPTRIS_BUILD_CLI=ON
cmake --build build
```

## Expected Results

### Test Coverage
- XPath W3C Conformance: 438/438 tests (100%)
- CLI Tests: 88/88 tests (100%)
- DOM Tests: 105/106 tests (99.1%)
- Total: 777+ tests

### Performance Targets
- XPath vs libxml2: ≥1.5x faster ✅
- DOM parsing: Competitive with pugixml (target: ≥1.2x)
- Memory: Compact element structure (~96 bytes vs 152+ bytes in legacy design)

## Troubleshooting

### Build failures
```bash
# Clean build directory
rm -rf build
cmake -B build -S . [options]
```

### Test failures
```bash
# Run tests with verbose output
ctest --test-dir build --output-on-failure --verbose

# Run individual test for detailed output
./build/test/c/test_dom --gtest_filter=TestName.TestCase
```

### Benchmark failures

Benchmarks require pugixml for comparison. Install it first:

```bash
# Install pugixml (macOS)
brew install pugixml

# Install pugixml (Ubuntu/Debian)
sudo apt-get install libpugixml-dev

# Or build from source
git clone https://github.com/zeux/pugixml.git
cd pugixml
cmake -B build -S .
cmake --build build
sudo cmake --install build
```
