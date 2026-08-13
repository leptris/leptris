# TODO 164 — Direct-pointer tree walk in VM (medium-leverage, medium-risk)

## Status

Pending. Identified in [[161-pugixml-gap-closure-survey]] as
TODO 159 Phase C — direct-pointer tree walk in the bytecode VM.

## Why

The VM walks the tree via `taurus_elem_first_child`,
`taurus_elem_next_sibling`, etc., which call
`taurus_compact_int32_decode`. Each decode is:

```c
if (off == 0) return NULL;
if (off == TAURUS_INT32_OVERFLOW_SENTINEL) {
    /* overflow-table lookup */
}
return (char*)base + off;
```

Two branches + one add per tree edge. The VM has access to the
document pool base, so it could use direct pointer arithmetic
and skip the decoder's overflow check entirely (or assert that
overflow can't happen and skip both branches).

The compact decoder is already inline in the header, and with LTO
the compiler specialises aggressively. Still, the overflow check
is two unpredictable branches per edge — measurable on deep
traversals.

## Plan

### Phase A — VM-internal fast-path accessors

Add inline helpers in `vm.c` that bypass the overflow check:

```c
static inline TaurusElement vm_first_child_fast(TaurusElement e) {
    int32_t off = e->first_child_off;
    return off ? (TaurusElement)((char*)e + off) : NULL;
}
```

Use these in the VM's tree-walk loops (`vm_apply_axis_descendant`,
`vm_apply_axis_child`, the fused child-num-cmp handler, etc.).
Keep using the safe `taurus_elem_first_child` for any path where
the node could have been allocated far from the pool base.

### Phase B — Compile-time overflow guarantee

Document and assert at parse time that all element/attribute
nodes live within `INT32_MAX` bytes of the pool's first page.
On 64-bit systems with normal allocation patterns this is
essentially always true; the overflow table is only there as a
defensive backstop for adversarial inputs.

If we can guarantee no overflow, the safe accessors collapse to
the fast-path form and Phase A becomes the only path.

## Risk

- If any node ever DOES overflow int32, the fast path returns a
  wrong pointer. The overflow case must remain on the safe path.
- Audit each VM tree-walk site carefully — some may receive
  attribute or namespace nodes that live in different storage.

## Expected impact

Est 1.1–1.3× on traversal-heavy XPath queries. Less leverage
than it sounds because the compiler with LTO already specialises
the inline decoder well.

## Status

Pending. Lower priority than [[162-result-struct-free-list]]
and [[163-stack-allocated-xpath-context]] — those are bigger
per-call wins with less risk.
