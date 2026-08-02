# TODO 07: Fix stack-overflow DoS — add parser depth limit

**Priority**: P0 (security, DoS)
**Status**: Planned
**Effort**: S

## Problem

Parsing `<a>` × N + `x` + `</a>` × N crashes for N ≥ ~20 000:

```
depth=10000  exit=0
depth=20000  Segmentation fault: 11   (exit 139)
depth=50000  Segmentation fault: 11
depth=100000 Segmentation fault: 11
```

## Root cause

`parser_parse_element` calls `parser_parse_node` which calls
`parser_parse_element` (`parse/parser_new.c:1571, 1627`). Pure mutual
recursion, no depth counter. The process stack overflows before any
application-level check fires. `resolve_namespaces_recursive`
(parser_new.c:1643) has the same problem on already-parsed trees.

Any service that uses libtaurus to parse untrusted XML is vulnerable to
remote crash-by-XML.

## Fix

Add depth tracking to the parser, with a configurable cap. Default is
**256** (matches libxml2's `XML_MAX_DEPTH`).

### `src/taurus/parse/parser_new.h`

```c
#define TAURUS_MAX_ELEMENT_DEPTH 256

typedef struct {
    // ... existing fields
    int depth;                 // current recursion depth
    int max_depth;             // hard cap (configurable, default above)
} Parser;
```

### `src/taurus/parse/parser_new.c`

```c
TaurusElement parser_parse_element(Parser* p) {
    if (p->depth >= p->max_depth) {
        parser_set_error(p, "Maximum element nesting depth exceeded");
        return NULL;
    }
    p->depth++;
    TaurusElement elem = parser_parse_element_impl(p);
    p->depth--;
    return elem;
}
```

(Rename the existing body of `parser_parse_element` to
`parser_parse_element_impl`. The public entry point is the wrapper above.
Same pattern for `parser_parse_node` and `resolve_namespaces_recursive`.)

`parser_create` initializes `p->max_depth = TAURUS_MAX_ELEMENT_DEPTH`.

Expose a knob for callers that legitimately need deeper trees:

```c
// parser_new.h
void parser_set_max_depth(Parser* p, int max_depth);
```

### Apply the same pattern to `resolve_namespaces_recursive`

Either convert to an explicit stack (best, but larger change) or wrap
with the same depth guard. Phase 1: wrap.

## Tests

`test/parser/test_parser.cpp`:

```cpp
TEST(ParserDepthLimit, RejectsExcessiveNesting) {
    // Build N levels of nested <a> elements.
    std::string xml;
    for (int i = 0; i < 300; i++) xml += "<a>";
    xml += 'x';
    for (int i = 0; i < 300; i++) xml += "</a>";

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
    EXPECT_EQ(doc, nullptr);
    EXPECT_EQ(st, TAURUS_ERROR_PARSE);
}

TEST(ParserDepthLimit, AcceptsNestingAtLimit) {
    // 256 levels — exactly at the cap — must succeed.
    std::string xml;
    for (int i = 0; i < 256; i++) xml += "<a>";
    xml += 'x';
    for (int i = 0; i < 256; i++) xml += "</a>";

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr);
    taurus_document_free(doc);
}
```

Integration: re-run the deep-nesting repro from the validation report
under `leaks` and confirm exit 0 with a parse error (not 139).

## Architecture notes

A depth limit is a **first-class parser invariant**, not a security
afterthought. The wrapper pattern (`guard → inc → impl → dec`) keeps the
depth check at exactly one place per recursive entry point — MECE. Adding
a new recursive function in the future means adding one wrapper, not
remembering to sprinkle checks throughout the body.

The limit is configurable because legitimate workloads vary: a
configuration-file parser rarely sees depth > 5; a programmatic
AST-to-XML serializer might legitimately produce depth 100+. The default
is conservative.

**Open issue for phase 2:** the namespace resolver and the XPath
evaluator both have their own recursion. Each subsystem should own its
own depth counter (no global) — MECE — but a future "library-wide
recursion policy" might want a shared budget. Defer until needed.

## Verification

1. `parse/parser_new.cpp` test specs pass.
2. The original repro:
   ```bash
   python3 -c "print('<a>'*20000 + 'x' + '</a>'*20000)" > deep.xml
   ./build/cli/taurus parse deep.xml
   ```
   exits with code 1 and a parse error (not 139).
3. `leaks --atExit -- build/cli/taurus parse deep.xml` reports zero
   leaks (no partial tree left behind by the error path — this will
   surface if `parser_free` doesn't tear down correctly).
