# TODO 158 — Inline tree-walk helpers across TU boundaries

## Status

**Phase A DONE** in v0.17.2. Branchless tree wiring via compile-time
offset tables (`dp_ns_off[5]` and `dp_par_off[5]`) in direct_parse.c.
Replaces two 5-way type-dispatched switches with two array lookups.

## Why

`taurus_node_first_child_internal`, `taurus_node_get_next_sibling`,
`taurus_elem_first_child`, etc. live in node.c / element.c. Callers
in direct_parse.c, evaluator.c, vm.c incur a real function call
for every tree walk. Each call:

- Push arguments (1-2 registers)
- Call instruction + return (2-3 cycles each, plus branch predict)
- Reload in the callee

Per-element traversal cost: ~5 ns. On a 1000-element doc that's 5 µs.

## Plan

### Phase A — Move hot accessors to header as `static inline`

The hot helpers:
- `taurus_elem_first_child`
- `taurus_elem_next_sibling`
- `taurus_elem_parent`
- `taurus_node_first_child_internal`
- `taurus_node_get_next_sibling`
- `taurus_element_name` (already inlined via `name` field)

These go in element.h / node.h as `static inline` so the compiler
inlines them at -O2 and LTO can specialize across TUs.

### Phase B — Branchless type dispatch

Today `taurus_node_get_next_sibling` switches on `node->type`:
element → read element->next_sibling_off, text → read
text->next_sibling_off, etc. The switch is branchy.

Replace with a lookup table of field offsets per type:

```c
static const size_t NEXT_SIB_OFF[] = {
    offsetof(struct taurus_element, next_sibling_off),
    offsetof(struct taurus_text_node, next_sibling_off),
    offsetof(struct taurus_comment_node, next_sibling_off),
    /* ... */
};
return (TaurusNode*)((char*)node + *(int32_t*)((char*)node + NEXT_SIB_OFF[node->type]));
```

Or: ensure `next_sibling_off` lives at the SAME offset in every
node-type struct. Then there's no dispatch — just one load.

### Phase C — LTO-friendly monomorphic dispatch

Even with `static inline`, the compiler may refuse to inline when
the function is large or has many call sites. Make sure the LTO
pass sees the function bodies by enabling `-flto` consistently
(already enabled in Release via `TAURUS_ENABLE_LTO`).

## Risk

- **Header bloat**: moving implementations to headers increases
  compile time slightly. Not material.
- **Visibility**: `static inline` functions in headers can't use
  internal-only state. Audit each one.

## Expected impact

Small (~5-10% on traversal-heavy benchmarks). Mainly a clean-up
that helps the SIMD work (TODO 157) by avoiding call overhead.

## Status

Pending. Fast to land; pair with TODO 157.
