# TODO 65: SAX parser deep audit

**Priority**: P1 (correctness — untested edge cases)
**Status**: Planned
**Effort**: M

## Problem

TODO 31 added basic SAX specs but didn't cover:

- Incremental parsing (`leptris_sax_parser_feed` with multiple chunks)
- Error recovery — what does SAX do on malformed input?
- Memory model — does SAX leak on error paths?
- Attribute iteration order — should match document order

## Fix

Add specs:

```cpp
TEST(SaxIncremental, ParsesAcrossChunks) { /* ... */ }
TEST(SaxErrorHandling, ReturnsErrorOnMalformed) { /* ... */ }
TEST(SaxLeaks, NoLeaksOnParseError) { /* ... */ }
TEST(SaxAttributes, ReturnedInDocumentOrder) { /* ... */ }
TEST(SaxNamespaces, FiresPrefixMappingEvents) { /* ... */ }
```

Audit `src/leptris/sax/parser.c` for:
- calloc/strdup sites that aren't freed on error paths
- buffer management on chunk boundaries
- state machine correctness

## Tests

The specs above are the deliverable.

## Verification

```bash
ctest --test-dir build --output-on-failure -R Sax
leaks --atExit -- build/test/test_sax
# Zero leaks across all SAX tests.
```
