# TODO 117 — XInclude parse="xml" + xpointer fallback

**Priority**: P2
**Status**: Open. Partial (parse="text" + xpointer) shipped in TODO 92.

## Why

Current XInclude handles:
- `parse="text"`: reads included file as text, replaces xi:include. ✓
- `xi:fallback` for missing files. ✓
- `xpointer`: XPath-based fragment selection. ✓

Still missing:
- `parse="xml"`: parses the included file as XML and inserts its root element (or xpointer-selected fragment) as a subtree.
- Recursive includes with cycle detection beyond the 32-level guard.

## Plan

### Phase A — parse="xml" without xpointer
- Resolve href → read file → call `leptris_parse_string` to get a `LeptrisDocument`.
- Detach the root element from the included document (set document pointer to NULL).
- Replace the xi:include element with the root element in the parent tree.
- Free the included document struct (but NOT the root element pool, which gets adopted).

This requires pool ownership transfer: the included doc's pool stays alive
until the parent doc is freed. Use a "child document" list on the parent
document to track these pools.

### Phase B — parse="xml" with xpointer
- After parsing the included document, evaluate the xpointer XPath against it.
- The result nodeset's nodes get moved into the parent tree (one per xi:include).
- Same pool-adoption mechanism as Phase A.

### Phase C — cycle detection
- Maintain a set of resolved absolute URIs on the parent document.
- Before resolving an include, check if the URI is already in the set.
- If so, treat as fallback (or error in strict mode).
- This complements the 32-level depth guard (which protects against
  unbounded recursion but not cycles).

## Acceptance

- W3C XInclude test suite (xml-test-suite) passes for parse="xml".
- Recursive includes work (chapter includes section includes paragraph).
- Cyclic includes are detected and reported.
