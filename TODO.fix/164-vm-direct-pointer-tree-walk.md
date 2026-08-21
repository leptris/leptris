# TODO 164 — Direct-pointer tree walk in VM (medium-leverage, medium-risk)

## Status

**Investigated, deferred.** Two factors pushed this out:

1. The existing `leptris_compact_int32_decode` already has the
   fast path inline in its body. With LTO (the default for
   Release) the compiler inlines the function across TUs, so the
   call overhead disappears. The inline-fast-path version would
   only help non-LTO builds — uncommon in production.

2. Touching 5+ DOM headers (element.h, text.h, comment.h, cdata.h,
   pi.h) for a no-op-under-LTO change risks introducing subtle
   sentinel-value mismatches. The current `INT32_MIN` sentinel
   lives in compact.c as `LEPTRIS_INT32_OVERFLOW_SENTINEL`; an
   inline version in compact.h must match exactly.

If a future profile shows the compact decode as a hot spot
(likely only on non-LTO builds or with a much smaller working
set), revisit by:
- Moving `LEPTRIS_INT32_OVERFLOW_SENTINEL` from compact.c to compact.h
- Adding `static inline leptris_compact_int32_decode_inline` with
  the same fast-path/slow-path split
- Updating the 13 call sites in element.h/text.h/comment.h/cdata.h/pi.h

For now, the existing inline accessors plus LTO cover the common
case.

## Why

The VM walks the tree via `leptris_elem_first_child`,
`leptris_elem_next_sibling`, etc., which call
`leptris_compact_int32_decode`. Each decode is:

```c
if (off == 0) return NULL;
if (off == LEPTRIS_INT32_OVERFLOW_SENTINEL) {
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
static inline LeptrisElement vm_first_child_fast(LeptrisElement e) {
    int32_t off = e->first_child_off;
    return off ? (LeptrisElement)((char*)e + off) : NULL;
}
```

Use these in the VM's tree-walk loops (`vm_apply_axis_descendant`,
`vm_apply_axis_child`, the fused child-num-cmp handler, etc.).
Keep using the safe `leptris_elem_first_child` for any path where
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
