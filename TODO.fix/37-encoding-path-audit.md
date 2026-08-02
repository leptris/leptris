# TODO 37: Encoding path leak audit

**Priority**: P1 (correctness — affects non-UTF-8 inputs)
**Status**: Planned
**Effort**: M

## Problem

The encoding paths in `src/taurus/encoding/{utf16.c,encoding.c}`
allocate buffers via `malloc`/`taurus_encoding_convert`.  After TODO
05's encoding-wrapper fix in `taurus_parse_string_with_encoding`,
the intermediate UTF-8 buffer IS freed.  But:

1. The iconv conversion path in `taurus_encoding_convert` (encoding.c)
   allocates a 4× input buffer; if iconv fails partway, the partial
   buffer leaks.
2. `taurus_encoding_parse_declaration` allocates a string for the
   detected encoding name; caller responsibility for free is unclear.
3. UTF-16 BOM detection path may strdup the encoding name into the
   document without checking for allocation failure.

None of these are exercised by the test suite — every fixture is UTF-8.

## Fix

### Phase 1: leak probe

Add specs that exercise each encoding path:

```cpp
TEST(EncodingUtf16, ParsesBOMAndConverts) {
    /* Build UTF-16LE BOM + simple document. */
    const unsigned char utf16[] = {0xFF, 0xFE, '<', 0, 'r', 0, '/', 0, '>', 0};
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string((const char*)utf16, sizeof(utf16), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    EXPECT_STREQ(taurus_element_name(root), "r");
    taurus_document_free(doc);
}

TEST(EncodingIso8859, ConvertsToUtf8) {
    /* Latin-1 input with high-byte chars. */
    /* ... */
}

TEST(EncodingIconvFailure, DoesNotLeakOnInvalidInput) {
    /* Force iconv failure with an invalid encoding name. */
    /* ... */
}
```

Run under `leaks --atExit --`.  Fix whatever leaks.

### Phase 2: defensive coding

- `taurus_encoding_convert`: free the partial output buffer on iconv
  failure.
- `taurus_encoding_parse_declaration`: document ownership of the
  returned string.
- All callers: check for NULL return.

## Tests

The phase 1 specs are the deliverable.

## Architecture notes

Encoding conversion is a **system boundary** — input is untrusted,
output is UTF-8 we trust.  Every byte the encoding layer allocates
must be either:

1. Transferred to the document pool (so document free releases it), or
2. Freed by the encoding layer itself before returning.

Currently the model is muddled.  Phase 2 makes it explicit.

## Verification

```bash
ctest --test-dir build --output-on-failure -R Encoding
# Pass.  Under leaks, zero bytes leaked for each encoding.
```
