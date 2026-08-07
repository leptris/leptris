# TODO 88 — freeze API: contract clarification + stub removal

**Priority**: P2 (correctness gap in public API)
**Status**: design — `taurus_node_thaw` is a stub

## What's in place

Public freeze API (shipped earlier this session):

* `taurus_document_freeze(doc)` — freezes the entire tree
* `taurus_document_is_frozen(doc)` — checks
* `taurus_node_freeze(node)` — recursive
* `taurus_node_is_frozen(node)` — checks

The parser calls `taurus_document_freeze_tree` internally so
every freshly-parsed document is frozen.

## What's broken

`taurus_node_thaw` in `src/taurus/dom/node.c` is a TODO stub:

```c
TaurusNode* taurus_node_thaw(TaurusNode* node) {
    if (!node) return NULL;
    if (!node->frozen) return node;
    /* TODO: Implement COW deep copy when frozen.
     * For now, just unfreeze (not safe for COW) */
    node->frozen = 0;
    return node;
}
```

It just clears the `frozen` flag in place. That's wrong if
multiple consumers hold the same frozen document — one mutation
is visible to all of them.

The stub is **internal-only** (declared in `dom/node.h`, not in
any public header). No external caller hits it. So the
broken-ness is latent.

## Architectural choice

Two possible contracts:

### Option A — freeze is permanent (libxml2 / pugixml model)

Document the freeze API as one-way. If you need to mutate a
frozen doc, you explicitly deep-copy first via a new
`taurus_document_clone` API. `taurus_node_thaw` is removed.

**Pros**: simple, no surprises, no COW machinery.
**Cons**: every "I want to mutate a frozen doc" call site needs
an explicit clone step.

### Option B — true COW

Mutation entry points check `frozen` first. If frozen, the
mutation path deep-copies the affected subtree transparently
and returns a new (unfrozen) pointer to the caller.

**Pros**: ergonomic — callers don't need to think about freeze.
**Cons**: every mutation entry point needs COW logic. The
existing API surface (`taurus_element_set_attribute(elem, ...)`
returns `TaurusStatus`, not a new `elem`) doesn't naturally
support "here's your new pointer" — callers would have to use
the return value differently.

## Recommendation

**Option A.** Freeze is permanent. Reasons:

1. The public API surface (`set_attribute`, `set_text`,
   `append_child`, …) returns `TaurusStatus`, not a new element
   handle. Adapting them to return new pointers is a major API
   change that breaks every caller.
2. pugixml doesn't have COW; libxml2 doesn't either. Users coming
   from either library will expect the freeze-permanent model.
3. COW deep copy is expensive — a hidden pool allocation per
   mutation is surprising.
4. The freeze API is currently used by the parser to mark
   completed documents. No user-facing flow currently relies on
   thaw being useful.

## Plan

1. Update public header docs to state "freeze is permanent."
2. Remove `taurus_node_thaw` from `dom/node.h` (internal header,
   no callers).
3. Delete the `taurus_node_thaw` implementation.
4. Add specs that exercise freeze + attempted mutation, asserting
   the doc stays frozen and the mutation is rejected.
5. If a real COW use case emerges later, design it as an explicit
   `taurus_document_clone` API rather than transparent COW.

## Acceptance

- Public header docs state the freeze contract clearly.
- No TODO stubs in `node.c`.
- Specs cover the "frozen doc rejects mutation" path.
