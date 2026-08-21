# TODO 92: XInclude processor

**Priority**: P3 (feature gap)
**Status**: Phases 1-3 shipped (parse="text" + parse="xml" + xi:fallback)
**Effort**: L

## What's in place

- `src/leptris/xinclude/xinclude.c` is compiled into the library.
- Five classifier/attribute helpers are **fully implemented**:
  - `leptris_xinclude_is_include_element(elem)` — checks namespace
    + local name "include".
  - `leptris_xinclude_is_fallback_element(elem)` — same for
    "fallback".
  - `leptris_xinclude_get_href(elem)` — reads `href` attribute.
  - `leptris_xinclude_get_parse(elem)` — reads `parse` attribute,
    defaults to `"xml"` per XInclude spec.
  - `leptris_xinclude_get_xpointer(elem)` — reads `xpointer`.
- `leptris_xinclude_process(doc, base_url)` is **a stub** returning
  `LEPTRIS_ERROR_NOT_IMPLEMENTED`.
- 6 specs in `test/xinclude/test_xinclude.cpp`.

The public API in `leptris.h` is no longer declared-but-undefined.

## What's still missing

The actual `leptris_xinclude_process` engine. The full work:

1. **Walk the document** looking for `xi:include` elements
   (using `leptris_xinclude_is_include_element`).
2. **Resolve `href`** against `base_url` (or treat as absolute).
   Support `file://`, `http://`, relative paths.
3. **Dispatch on `parse`**:
   - `parse="xml"` (default): parse the included resource as XML,
     adopt its root element(s) in place of `xi:include`.
   - `parse="text"`: read the resource as raw text, replace
     `xi:include` with a single text node.
4. **Apply `xpointer`** if present — select fragment from included
   document. Requires XPath engine integration.
5. **Handle `xi:fallback`** — if inclusion fails (file not found,
   parse error), process the fallback element's children. If no
   fallback, error out.
6. **Recursion guard** — prevent infinite `xi:include` loops.
7. **Base URL propagation** — included content's relative URLs
   resolve against the including resource's URL.

The hard parts are (4) — xpointer evaluation — and (1) — DOM
mutation requires cross-document node copying, which interacts
with the pool-ownership model (each document has its own pool).

## Plan

Phase 1 (2-3 days): `parse="text"` only. Read file, replace
`xi:include` with text node. No fallback, no xpointer, no
base URL.

Phase 2 (3-5 days): `parse="xml"` for local files. Cross-document
node copy via deep traversal + pool-routed re-allocation.

Phase 3 (1-2 days): `xi:fallback` handling.

Phase 4 (3-5 days): `xpointer` evaluation. Reuses the existing
XPath engine.

Phase 5 (1-2 days): Base URL resolution + recursion guard.

## Acceptance

- W3C XInclude test suite (basic cases) passes.
- No leaks (cross-document node copies respect pool ownership).
- Documents correctly merge included content.
- Recursion detected and reported.
