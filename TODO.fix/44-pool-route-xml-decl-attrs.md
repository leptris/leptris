# TODO 44: Pool-route XML declaration attributes

**Priority**: P1 (correctness — leaks on every XML declaration)
**Status**: Planned
**Effort**: S

## Problem

The XML declaration parser at `parse/parser_new.c:1729+` parses
attributes from `<?xml version="1.0" encoding="UTF-8"?>` and stores
them in parser state.  Each attribute uses `parse_name(p)` (calloc)
and `parse_attribute_value(p)` (calloc) for the name/value buffers.

These intermediates are freed in some paths (TODO 25 found and fixed
several), but the XML declaration parser's path is separate and may
still leak.

## Root cause

The XML declaration is parsed inside `parser_parse_node` (around line
1700+) as a special case.  Its attributes are extracted into local
`attr_name` / `attr_value` variables, processed, then `TAURUS_FREE`'d
in the success path.  But several error paths return early without
freeing.

## Fix

Two options:

1. **Free the intermediates in every error path.** Mechanical: walk
   the function, add `TAURUS_FREE` before every `return NULL`.
2. **Pool-route** `parse_name` / `parse_attribute_value` when called
   from inside the XML declaration parser.  The intermediates become
   pool-owned; pool destroy releases them.

Option 2 is cleaner (DRY: no caller has to remember to free).  But
requires changing `parse_name` to take a pool parameter.  Defer until
TODO 41 (unify string ownership) is in flight.

For now, option 1 — add the missing frees.

## Tests

`test/parser/test_parser.cpp`:

```cpp
TEST(ParserLeaks, XmlDeclarationDoesNotLeak) {
    const char xml[] = "<?xml version='1.0' encoding='UTF-8'?>"
                       "<?xml-stylesheet href='x.xsl'?>"
                       "<r/>";
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    taurus_document_free(doc);
    // Under leaks --atExit --: 0 bytes leaked.
}
```

## Verification

```bash
leaks --atExit -- build/cli/taurus parse ...xml_with_declaration...
# Expected: 0 leaks for 0 bytes.
```
