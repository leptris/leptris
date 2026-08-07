# TODO 143 — Flat-mode traversal for read-only XPath

Phase C of TODO 140. The biggest single perf win.

## Problem

Today, every XPath query triggers promote via taurus_document_root.
For read-only queries (count, exists, name lookup), the promote
cost (1.5 µs/elem) is pure overhead — we don't need the
compact-pointer tree, just the data.

## Strategy

Make the XPath VM aware of FlatDoc. For the most common read-only
query patterns, walk the FlatDoc directly:

- `count(//name)` — Phase E fast path already does this for one
  specific name; generalize to any name + the count() function.
- `//name` (no predicate) — return a "virtual nodeset" backed by
  FlatDoc indexes. Materialize pointers lazily on access.
- `descendant::*` — same.

Mutation, predicates with position(), and complex axes still
require promote. The fast paths trigger when:
1. doc->flat_doc is non-NULL (not yet promoted)
2. The query pattern matches a recognized shape
3. No mutation has occurred

## Implementation sketch

1. Add a `FlatDocNodeset` XPath result variant that holds a list
   of flat indices instead of TaurusNode pointers.
2. Add XPath VM dispatch: if the doc has flat_doc and the expression
   is `count(//name)`, return FlatDocNodeset-based count.
3. Lazy materialization: when the caller does
   `taurus_xpath_result_get(result, i)`, promote (if needed) and
   return the corresponding TaurusElement.

## Expected impact

| Query                | Current | After  |
|----------------------|---------|--------|
| `count(//book)` 5 KB| 100 µs  | 5 µs   |
| `//book` 5 KB        | ~150 µs | ~50 µs (materialize on access) |
| `book[@id='x']` 5 KB| ~150 µs | ~80 µs |

Matches or beats pugixml on read-only XPath for unpromoted docs.

## Risk

Medium. The XPath VM has many code paths; the dispatcher must
correctly identify which queries are safe for the fast path.
Misidentification causes wrong results.

## Test plan

- Existing XPath conformance suite (438 specs) catches regressions.
- New spec: count via fast path matches count via XPath+promote.
- New spec: nodeset_get from fast-path result returns same elements
  as the promote-then-query path.
