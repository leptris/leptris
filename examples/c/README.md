# Taurus C Examples

This directory contains comprehensive examples demonstrating the libtaurus API.

## Building Examples

```bash
cd build
cmake ..
make

# Run all examples
make run_examples

# Or run individually
./examples/c/basic_example
./examples/c/xpath_example
./examples/c/namespace_example
./examples/c/error_handling_example
./examples/c/dom_traversal_example
```

## Example Descriptions

### 1. basic_example.c (79 lines)

**Purpose**: Introduction to Taurus API basics

**Demonstrates**:
- Parsing XML documents
- Getting root element
- Basic XPath queries
- Memory cleanup

**Run**:
```bash
./basic_example
```

**Key APIs**:
- `taurus_parse()` - Parse XML string
- `taurus_document_root()` - Get root element
- `taurus_element_name()` - Get element name
- `taurus_xpath_eval()` - Evaluate XPath
- `taurus_document_free()` - Cleanup

---

### 2. xpath_example.c (144 lines)

**Purpose**: Comprehensive XPath 1.0 query examples

**Demonstrates**:
- Node-set queries (`//book`)
- Position predicates (`//book[1]`)
- Attribute queries (`//book/@price`)
- Boolean predicates (`[@category='fiction']`)
- XPath functions (`count()`)
- String results
- Complex predicates (`[year > 1990]`)
- Multiple predicates

**Run**:
```bash
./xpath_example
```

**Key APIs**:
- `taurus_xpath_eval()` - Evaluate XPath
- `taurus_xpath_result_nodeset_size()` - Get result count
- `taurus_xpath_result_nodeset_get()` - Get node from result
- `taurus_xpath_result_as_boolean()` - Convert to boolean
- `taurus_xpath_result_as_number()` - Convert to number
- `taurus_xpath_result_as_string()` - Convert to string
- `taurus_xpath_result_free()` - Free result

---

### 3. namespace_example.c (154 lines)

**Purpose**: XML Namespaces 1.0 support demonstration

**Demonstrates**:
- Element namespace information
- Namespace declarations
- Namespace resolution
- Default namespaces
- Namespace inheritance
- Prefixed elements
- XPath with namespaces

**Run**:
```bash
./namespace_example
```

**Key APIs**:
- `taurus_element_namespace()` - Get namespace URI
- `taurus_element_prefix()` - Get namespace prefix
- `taurus_element_namespace_count()` - Count declarations
- `taurus_element_namespace_decl()` - Get declaration
- `taurus_element_resolve_namespace()` - Resolve prefix
- `taurus_namespace_prefix()` - Get namespace prefix
- `taurus_namespace_uri()` - Get namespace URI

---

### 4. error_handling_example.c (187 lines)

**Purpose**: Proper error handling patterns

**Demonstrates**:
- NULL input handling
- Empty input handling
- Malformed XML detection
- Invalid XPath syntax
- Error message retrieval
- Error code handling
- Position tracking
- Error state clearing
- Safe NULL handling

**Run**:
```bash
./error_handling_example
```

**Key APIs**:
- `taurus_last_error()` - Get error message
- `taurus_last_error_code()` - Get error code
- `taurus_error_string()` - Convert code to string
- `taurus_parse_error_line()` - Get error line
- `taurus_parse_error_column()` - Get error column
- `taurus_clear_error()` - Clear error state
- `taurus_parse_options_init()` - Initialize options
- `taurus_parse_with_options()` - Parse with options

---

### 5. dom_traversal_example.c (212 lines)

**Purpose**: DOM tree navigation and querying

**Demonstrates**:
- Complete tree traversal
- Parent navigation
- Child access
- Attribute iteration
- Text content access
- Element property checks
- Combined XPath and DOM access

**Run**:
```bash
./dom_traversal_example
```

**Key APIs**:
- `taurus_element_name()` - Get element name
- `taurus_element_parent()` - Get parent
- `taurus_element_child_count()` - Count children
- `taurus_element_child()` - Get child by index
- `taurus_element_attribute_count()` - Count attributes
- `taurus_element_attribute()` - Get attribute by index
- `taurus_element_get_attribute()` - Get attribute by name
- `taurus_element_has_attribute()` - Check attribute exists
- `taurus_element_text()` - Get text content
- `taurus_xpath_eval_with_context()` - XPath from context

---

## Memory Leak Verification

### macOS

**Option 1: Using `leaks` command** (Requires Xcode Command Line Tools)

```bash
# Install Xcode Command Line Tools first if not installed
xcode-select --install

# Run program and check for leaks afterward
./basic_example &
PID=$!
sleep 1
leaks $PID
```

**Option 2: Using Xcode Instruments** (Recommended for macOS)

1. Open Xcode
2. Choose Product > Profile (⌘I)
3. Select "Leaks" instrument
4. Build and run your example
5. Watch for leak reports in timeline

**Option 3: Address Sanitizer** (Works on macOS, best for development)

```bash
# Compile with AddressSanitizer
cmake -DCMAKE_C_FLAGS="-fsanitize=address -g" ..
make

# Run examples - ASan will report leaks automatically
./basic_example
./xpath_example
# ... etc
```

Expected output with ASan (no leaks):
```
=================================================================
==12345==ERROR: LeakSanitizer: detected memory leaks
[No leaks reported]
```

### Linux

**Using Valgrind**:

```bash
# Install valgrind
sudo apt-get install valgrind  # Debian/Ubuntu
sudo yum install valgrind      # RHEL/CentOS

# Run with leak detection
valgrind --leak-check=full --show-leak-kinds=all ./basic_example
```

Expected output:
```
All heap blocks were freed -- no leaks are possible
```

**Using AddressSanitizer** (Recommended):

```bash
# Same as macOS
cmake -DCMAKE_C_FLAGS="-fsanitize=address -g" ..
make
./basic_example
```

### CI/CD Integration

For automated leak checking in GitHub Actions:

```yaml
# .github/workflows/test.yml
- name: Build with AddressSanitizer
  run: |
    cmake -DCMAKE_C_FLAGS="-fsanitize=address -g" .
    make

- name: Run examples with leak detection
  run: |
    ./examples/c/basic_example
    ./examples/c/xpath_example
    # ASan will fail the build if leaks detected
```


## API Coverage

These examples cover:

**Parse API** (100%):
- `taurus_parse()`
- `taurus_parse_with_options()`
- `taurus_parse_options_init()`
- `taurus_document_free()`
- `taurus_document_root()`
- `taurus_document_encoding()`

**Element API** (100%):
- `taurus_element_name()`
- `taurus_element_namespace()`
- `taurus_element_prefix()`
- `taurus_element_text()`
- `taurus_element_parent()`
- `taurus_element_child_count()`
- `taurus_element_child()`

**Attribute API** (100%):
- `taurus_element_attribute_count()`
- `taurus_element_attribute()`
- `taurus_element_get_attribute()`
- `taurus_element_has_attribute()`
- `taurus_attribute_name()`
- `taurus_attribute_value()`
- `taurus_attribute_namespace()`

**Namespace API** (100%):
- `taurus_element_namespace_count()`
- `taurus_element_namespace_decl()`
- `taurus_element_resolve_namespace()`
- `taurus_namespace_prefix()`
- `taurus_namespace_uri()`

**XPath API** (100%):
- `taurus_xpath_eval()`
- `taurus_xpath_eval_with_context()`
- `taurus_xpath_result_free()`
- `taurus_xpath_result_get_type()`
- `taurus_xpath_result_as_boolean()`
- `taurus_xpath_result_as_number()`
- `taurus_xpath_result_as_string()`
- `taurus_xpath_result_nodeset_size()`
- `taurus_xpath_result_nodeset_get()`

**Error API** (100%):
- `taurus_last_error()`
- `taurus_last_error_code()`
- `taurus_error_string()`
- `taurus_parse_error_line()`
- `taurus_parse_error_column()`
- `taurus_clear_error()`

## Next Steps

After Phase 4 (FFI Migration) is complete:

1. Ensure all examples compile without errors
2. Verify all examples run successfully
3. Confirm zero memory leaks with `leaks`/`valgrind`
4. Use examples as basis for integration tests
5. Add performance benchmarking examples

## Contributing

When adding new examples:

1. Follow the existing structure (includes, main, sections, cleanup)
2. Add comprehensive comments
3. Use printf with ✓/✗ for success/failure
4. Include error checking for all API calls
5. Always free resources (zero leaks)
6. Update this README with new example
7. Add to CMakeLists.txt
8. Test with memory leak detection tools

## License

Copyright (c) 2024, Ribose Inc.
All rights reserved.