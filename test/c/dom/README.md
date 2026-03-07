# DOM Tests

This directory contains DOM traversal and text handling tests adopted from pugixml.

## Test Files

### Adopted from pugixml

- `test_dom_traverse.c` - DOM navigation tests
  - Child/sibling navigation
  - Parent/ancestor queries
  - Attribute iteration
  - Node type filtering
  - Recursive traversal patterns

- `test_dom_text.c` - Text content tests
  - Text extraction
  - CDATA handling
  - Whitespace normalization
  - Entity expansion in text

## Running Tests

```bash
cd build
ctest -R dom
```

## Coverage Goals

- Complete DOM traversal API validation
- Text handling edge cases
- Performance benchmarks for large DOMs

## Note

DOM modification tests (insert/remove/update) are not included as Taurus focuses on read-only parsing and XPath queries.