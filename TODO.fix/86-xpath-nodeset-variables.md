# TODO 86: Implement XPath nodeset variable support

**Priority**: P2 (feature — closes a known gap)
**Status**: Stub returns unsupported
**Effort**: M

## Problem

`src/taurus/xpath/evaluator.c:670`:

```c
/* TODO: Implement nodeset variable support */
```

XPath variables can hold four types: nodeset, string, number, boolean.
Today we support string/number/boolean; a nodeset variable falls
through silently.

## Plan

1. Extend `TaurusXPathVariable` to carry a `TaurusNode**` + count for
   nodeset values (in addition to the existing string/number/boolean
   union).
2. In the evaluator, when a `$var` reference resolves to a nodeset,
   push it onto the evaluation stack as a nodeset result.
3. Specs:
   - Define a variable holding `{book1, book2}` and assert `count($books)`
     returns 2.
   - Assert `$books[1]/@id` returns the first book's id.
   - Assert `$books/title` returns the titles of all books in the variable.

## Acceptance

- W3C XPath conformance suite passes with variables covering all four types.
- No memory leaks (variables are pool-owned; nodesets reference
  document nodes — no copy).
