# TODO 33: Fix `dtd_parse_quoted_string` leak

**Priority**: P0 (correctness — last leak source on full.xml)
**Status**: Planned
**Effort**: S

## Problem

After all earlier fixes, `full.xml` still shows 2 leaks / 32 bytes:

```
STACK OF 1 INSTANCE OF 'ROOT LEAK: <malloc in dtd_parse_quoted_string>':
6   taurus  ...  taurus_parse_string_with_encoding + 392
5   taurus  ...  taurus_parse + 220
2   taurus  ...  taurus_dtd_parse_internal_subset + 1344
    1 (16 bytes) ROOT LEAK: <malloc in dtd_parse_quoted_string 0x...>
```

`dtd_parse_quoted_string` calloc-allocates a quoted-string value (e.g.,
the URI in `<!ENTITY foo SYSTEM "uri">`) but the consumer doesn't free
the intermediate.

## Root cause

`dtd/parser.c::dtd_parse_quoted_string` returns a heap-allocated
string.  Callers use it for entity `value`, `system_id`, `public_id`,
etc.  Some callers free it; others don't.

## Fix

Two options:

1. **Free the intermediate after the consumer copies it** (matches the
   pattern used elsewhere).
2. **Change `dtd_parse_quoted_string` to take a pool parameter** and
   allocate from the pool.

Option 2 is cleaner (DRY: no caller has to remember to free).  But
it's a larger change.  Option 1 is the targeted fix.

## Tests

`test/parser/test_parser.cpp`:

```cpp
TEST(ParserLeaks, DoctypeWithSystemEntityIsPoolOwned) {
    const char xml[] =
        "<!DOCTYPE r ["
        "<!ENTITY foo SYSTEM \"http://example.com/foo\">"
        "]><r/>";
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    taurus_document_free(doc);
    // Zero leaks under `leaks --atExit --`.
}
```

## Verification

```bash
leaks --atExit -- build/cli/taurus parse test/fixtures/full.xml
# Expected: 0 leaks for 0 bytes.
```
