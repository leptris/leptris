# Parser Tests

This directory contains comprehensive parser tests adopted from pugixml and custom tests for Taurus-specific parsing features.

## Test Files

### Adopted from pugixml

- `test_parse_pi.c` - Processing instruction tests
  - Basic PI parsing
  - Whitespace handling
  - Error conditions
  - Edge cases

- `test_parse_cdata.c` - CDATA section tests
  - Basic CDATA parsing
  - Special characters
  - Nested scenarios
  - Error handling

- `test_parse_comments.c` - XML comment tests
  - Single/multi-line comments
  - Special characters
  - Edge cases

- `test_parse_entities.c` - Entity reference tests
  - Built-in entities (&lt;, &gt;, &amp;, &quot;, &apos;)
  - Numeric entities (&#123;, &#xAB;)
  - Custom entity declarations
  - Error handling

- `test_parse_doctype.c` - DOCTYPE declaration tests
  - Internal DTD subsets
  - External DTD references
  - Entity declarations
  - Notation declarations

- `test_parse_errors.c` - Error handling tests
  - Malformed documents
  - Recovery strategies
  - Error reporting

## Running Tests

```bash
cd build
ctest -R parse
```

## Coverage Goals

- 500+ test cases from pugixml
- All XML 1.0 parsing features
- Comprehensive error handling