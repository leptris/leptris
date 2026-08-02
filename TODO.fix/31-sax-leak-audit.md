# TODO 31: SAX parser leak audit

**Priority**: P2 (correctness / coverage)
**Status**: Planned
**Effort**: S

## Problem

The SAX parser (`src/taurus/sax/parser.c`) is a separate entry point
from the DOM parser, but it shares the same pool-based allocation
model.  It's never been audited for leaks — the leak investigation
in TODOs 05/15/16 focused exclusively on `taurus_parse_string` /
`taurus_parse_string_with_encoding`.

## Root cause (hypothesis)

The SAX parser likely allocates per-event buffers (element names,
attribute names/values, text chunks).  Some of these may be calloc'd
for backwards compatibility, others pool-allocated.  Without specs
exercising the path, regressions would go unnoticed.

## Fix

1. **Audit** every allocation in `src/taurus/sax/parser.c`.  Confirm
   each is pool-owned or explicitly freed.

2. **Specs** in `test/sax/test_sax.cpp` (new directory):

```cpp
TEST(SaxParser, ParsesBasicDocument) {
    const char xml[] = "<root><a/>text<b/></root>";
    // ... feed to taurus_sax_parse, collect events ...
}

TEST(SaxParser, NoLeaksOnComplexDocument) {
    // Under leaks/valgrind, zero bytes leaked after parse.
}

TEST(SaxParser, HandlesAllNodeTypes) {
    // Comments, CDATA, PIs, DOCTYPE — every SAX event fires.
}

TEST(SaxParser, HandlesEncodingDeclaration) {
    // UTF-16 BOM, UTF-8 BOM, explicit encoding="..." attribute.
}
```

Add `test/sax/CMakeLists.txt` and register via `taurus_add_test`.

## Tests

The specs above are the deliverable.

## Architecture notes

The SAX parser should share the same ownership model as the DOM
parser — same pool, same string interning, same depth guard.  If
the audit reveals divergence, surface it as a separate TODO and
consolidate.

## Verification

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R Sax
# All SAX specs pass.

leaks --atExit -- build/cli/taurus parse ...   # CLI doesn't use SAX,
                                              # so a direct SAX test
                                              # binary is the right probe.
```
