# TODO 67: Encoding edge case specs

**Priority**: P2 (coverage — encoding paths are subtle)
**Status**: Planned
**Effort**: S

## Problem

TODO 37 added basic encoding specs but didn't cover:

- UTF-8 BOM (EF BB BF) — currently stripped silently
- Malformed UTF-8 sequences at various positions (start, mid, end)
- XML declaration with mismatched encoding="..." vs actual bytes
- Mixed encodings in attributes (rare but legal in some legacy XML)
- Empty input
- Just whitespace

## Fix

Extend `test/parser/test_encoding.cpp`:

```cpp
TEST(EncodingUtf8, ParsesWithBom) { /* ... */ }
TEST(EncodingUtf8, RejectsOverlongEncoding) { /* ... */ }
TEST(EncodingUtf8, RejectsSurrogateRange) { /* ... */ }
TEST(EncodingEdgeCases, EmptyInputReturnsNull) { /* ... */ }
TEST(EncodingEdgeCases, JustWhitespace) { /* ... */ }
TEST(EncodingEdgeCases, MismatchedEncodingDeclaration) { /* ... */ }
```

## Tests

The specs above are the deliverable.

## Verification

```bash
ctest --test-dir build --output-on-failure -R Encoding
```
