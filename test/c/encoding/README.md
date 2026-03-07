# Encoding Tests

This directory contains character encoding tests adopted from pugixml.

## Test Files

### Adopted from pugixml

- `test_unicode.c` - Unicode handling tests
  - UTF-8 validation
  - UTF-16 support (if implemented)
  - Character range validation
  - BOM handling

- `test_charset.c` - Character set tests
  - Encoding detection
  - Character conversion
  - Special character handling

## Running Tests

```bash
cd build
ctest -R encoding
```

## Coverage Goals

- UTF-8 correctness
- BOM detection and handling
- Invalid encoding detection
- Multi-byte character support