# TODO 126 — Phase 3: Specialize child::name traversal

## Goal

`child::book` and bare `book` (which defaults to child axis) are
the most common XPath steps. After TODO 125 (lazy namespace init)
they take ~1.2 µs — close to libxml2 (~0.94 µs) but still with
~30 % gap.

The remaining cost is the chain:
`evaluate_step` → `apply_axis` (strcmp per call) →
`matches_node_test` (switch + parse_node_test_name + strcmp chain)
→ `xpath_nodeset_add` with dedup check.

For the common case — single name test, no predicate, no
namespace prefix — this can be a tight inline loop in the VM:
walk first_child → next_sibling, strcmp each child's name against
the constant name, append matches.

## Plan

1. Add VM opcodes:
   - `BC_AXIS_CHILD_NAME` — u16 operand: const-pool string (name).
     Pops input nodeset, pushes children matching name.
   - `BC_AXIS_CHILD_WILD` — no operand. Pops input, pushes all
     children.
   - `BC_AXIS_ATTRIBUTE_NAME` — u16 operand: const-pool string.
     Pops input, pushes attribute matching name.
   - `BC_AXIS_ATTRIBUTE_WILD` — no operand.
   - `BC_AXIS_SELF_NAME` — u16 operand. Pops input, pushes those
     matching name (filter).
   - `BC_AXIS_PARENT_NAME` — u16 operand. Pops input, pushes
     unique parent if name matches.

2. Compiler emits these when the AST shape matches:
   - STEP, axis_id == CHILD/ATTRIBUTE/SELF/PARENT
   - children[0] is NODE_TEST_NAME or NODE_TEST_ALL with no prefix
   - No predicate children (child_count == 1)

3. VM handlers are tight loops. No dedup (child and attribute axes
   can't produce duplicates from a single root). For PARENT the
   result is at most one node per input.

## Why also SELF and PARENT

These axes are O(1) per input but currently go through the full
evaluate_step scaffolding. Specializing removes ~0.5 µs each.

## Expected outcome

| Benchmark          | Before (Phase 1) | After (Phase 3) |
|--------------------|------------------|-----------------|
| child::book        | 1.14 µs          | ~0.7 µs         |
| child::*           | 1.16 µs          | ~0.7 µs         |
| attribute::id      | 1.07 µs          | ~0.7 µs         |
| attribute::*       | 1.25 µs          | ~0.8 µs         |
| self::*            | 0.97 µs          | ~0.7 µs         |
| parent::*          | ~1.0 µs          | ~0.7 µs         |

Modest absolute wins, but they bring us past libxml2 on these
specific axes. Phase 4 builds the same pattern for descendant.

## Branch
`todo-126-axis-specialization`
