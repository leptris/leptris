# TODO 127 — Phase 4: Specialize descendant / descendant-or-self

## Goal

After TODO 125 (lazy namespace init) and TODO 126 (specialized
child/attribute/self/parent axes), the biggest remaining gap vs
libxml2 is on the descendant axis:

    descendant::*            9.5 µs   vs libxml2  0.96 µs   (10× slower)
    descendant::title        7.5 µs   vs libxml2  0.99 µs   (7.5× slower)
    descendant::*[@id]      29 µs     vs libxml2  1.02 µs   (28× slower)

(The predicate case is Phase 5.)

## What's slow today

Descendant goes through `evaluate_step → apply_axis` which:
1. Looks up the axis function by name (strcmp per call).
2. Recursively walks the subtree.
3. For each visited node, calls `matches_node_test`.
4. Adds matches to the result via `xpath_nodeset_add`.
5. After the axis returns, evaluate_step does O(n²) dedup on the
   first 32 entries of the result.

For a 50-element subtree, that's 50 × (matches_node_test switch
dispatch + nodeset_add capacity check + dedup scan).

## Plan

Add specialized opcodes:

- `BC_AXIS_DESCENDANT_NAME` — u16 const-pool string.
- `BC_AXIS_DESCENDANT_WILD` — no operand.
- `BC_AXIS_DESCENDANT_OR_SELF_NAME` — u16 const-pool string.
- `BC_AXIS_DESCENDANT_OR_SELF_WILD` — no operand.

VM handler:
- Tight recursive subtree walk inline in the VM.
- Skip matches_node_test (direct strcmp on element name).
- Skip dedup when input has 1 element (the common case for
  `descendant::*` from a single context node). For multi-element
  input, fall back to BC_AXIS_STEP to preserve correctness.
- No apply_axis dispatch.

## Compiler gating

Same as Phase 3: emit specialized opcode only when:
- STEP axis_id == DESCENDANT or DESCENDANT_OR_SELF
- Node test is NODE_TEST_NAME (no `:`) or NODE_TEST_ALL
- No predicates

## Expected outcome

| Benchmark              | Phase 3 | Phase 4 |
|------------------------|---------|---------|
| descendant::*          | 9.5 µs  | ~3 µs   |
| descendant::title      | 7.5 µs  | ~2 µs   |
| descendant-or-self::*  | ~10 µs  | ~3.5 µs |

Won't reach libxml2 parity (libxml2 has specialized pre-allocated
nodeset structures) but should close most of the gap. Phase 5
(predicate fast paths) will help the [@id] case.

## Branch
`todo-127-descendant-specialization`
