# TODO 41: Unify string ownership model

**Priority**: P3 (investigated; current design is intentional)
**Status**: Closed — no change required
**Effort**: 0

## Original concern

Strings inside a `LeptrisDocument` had three apparent lifetimes:

1. **Pool-owned** — node names, attribute values, text content.
2. **Document-owned but heap-allocated** — `doc->xml_buffer` (the
   parse input copy).
3. **Caller-owned** — `doc->xml_buffer` when the caller used
   `leptris_parse_string_inplace`.

## Investigation outcome

Lifetime #3 is the **intentional API contract** for
`leptris_parse_string_inplace`. The caller opts into zero-copy by
passing their own buffer; they retain ownership. The
`xml_buffer_needs_free` flag tracks this.

Lifetime #2 is necessary because:
- The pool is created BEFORE the xml_buffer copy (see `leptris_parse`).
- Even if we pool-allocated xml_buffer, the pool's LeptrisBigAlloc
  side-list would track it — net effect: same lifetime, more
  indirection.

Conditional free in `leptris_document_free` is correct and well-documented:

```c
if (doc->xml_buffer && doc->xml_buffer_needs_free) {
    LEPTRIS_FREE(doc->xml_buffer);
}
```

## Why this isn't a bug

The original "three lifetimes" framing was misleading. There are
really two:

1. **Pool-owned** — everything reachable from the document.
2. **External** — `xml_buffer` when caller opted into inplace parsing.

Removing lifetime #2 would either:
- Break the inplace API (callers can't pass stack buffers anymore).
- Force pool allocation of potentially-MB-sized input buffers (poor
  fit for the pool's page-based design).

Neither is an improvement.

## Decision

**Close this TODO with no code change.** The conditional free is the
correct design.

The architecture review (`docs/ARCHITECTURE_REVIEW.md`) captures this
decision under "Architecture decisions to revisit (low priority)".
