# TODO 120 Phase F — Inline VM dispatch

## Goal
Replace `BC_FALLBACK_EVAL` delegates for the three hot AST families
(`AXIS_STEP`, `BINARY_OP`, `FUNC_CALL`) with VM opcodes that call the
existing evaluator helpers directly, skipping the AST-node-type
switch dispatch in `evaluate_expr`.

The current VM is complete but slower than direct AST evaluation for
non-literal expressions because:
1. `taurus_xpath_vm_eval` compiles the bytecode on EVERY eval call
   (no caching). The compile cost dwarfs the dispatch savings on
   literals.
2. For non-literal AST nodes the compiler emits `BC_FALLBACK_EVAL`,
   which the VM implements by calling `evaluate_expr(ctx, ast)` —
   the same function the non-VM path uses, plus the VM stack
   overhead.

So today: `taurus_xpath_eval` ≈ AST-eval + (compile + run + free).
The VM must become a net win, not a tax.

## Two-part design

### Part 1: Bytecode cache (the real win)
Extend `xpath_ast_cache` to also lazily hold a `TaurusXPathBytecode*`
per cached expression. Rename internally to reflect that the cache
holds both AST and compiled bytecode (public API names unchanged).

Flow change:
- Before: `taurus_xpath_eval` → AST cache lookup → `vm_eval(ast)`
  → compile + run + free.
- After: `taurus_xpath_eval` → cache lookup → if bytecode missing,
  compile and store → run cached bytecode.

Net effect on the second and later evals of the same expression:
zero compile cost. The VM's dispatch savings (however small) are now
pure upside.

### Part 2: Inline opcodes
Add three new opcodes. The compiler emits them for matching AST
nodes; everything else still falls through to `BC_FALLBACK_EVAL`.

- `BC_AXIS_STEP` — operand: const-pool index pointing to the STEP
  AST. VM pops input nodeset from stack, calls
  `evaluate_step(ctx, ast_step, input)`, pushes result nodeset.
  Saves: the AST switch dispatch in `evaluate_expr` for STEP nodes,
  plus the nodeset setup that wraps the context node when the STEP
  appears bare.

- `BC_BINARY_OP` — operand: XPathOperatorType (immediate). VM pops
  two operands, applies arithmetic / comparison / boolean logic
  inline. Saves: AST walking through `evaluate_operator` for the
  common 2-operand case.

- `BC_FUNC_CALL` — operand: const-pool index pointing to the
  FUNCTION_CALL AST. VM calls
  `evaluate_function_call(ctx, ast)` directly. Saves: the AST
  switch dispatch and the indirect `evaluate_expr →
  evaluate_function_call` call.

For paths (`XPATH_AST_PATH_EXPR`, `XPATH_AST_ABSOLUTE_PATH`,
`XPATH_AST_RELATIVE_PATH`), the compiler recursively emits
`BC_AXIS_STEP` for each step in sequence. The VM threads the
nodeset through the stack — each step consumes the previous
nodeset and pushes the next.

Predicates remain AST-based for now: `BC_AXIS_STEP` references
the STEP AST, which contains the predicate ASTs. The VM
calls `evaluate_step`, which calls `apply_predicates` on the
predicate ASTs. This is acceptable because predicate evaluation
is per-node — inherently expensive — and the per-eval savings
from inlining them would be small.

## MECE / OCP notes

- Adding a new opcode = add an enum value + a `case` in the VM
  switch + a `compile_*` helper. No existing cases change. This
  matches the OCP note already in bytecode.h.
- The compiler's `compile_node` switch grows by 3 cases. The
  VM's dispatch switch grows by 3 cases. No switch outside the
  VM touches.
- Bytecode cache is a pure extension to `xpath_ast_cache.c` —
  existing AST cache semantics preserved (slot replacement,
  LRU-ish, FNV-1a hash, 16 slots).

## Branch
`todo-120-phase-f-inline-dispatch`

## Phases
- Phase 1: bytecode cache + lookup API.
- Phase 2: BC_AXIS_STEP compiler + VM handler + spec.
- Phase 3: BC_BINARY_OP compiler + VM handler + spec.
- Phase 4: BC_FUNC_CALL compiler + VM handler + spec.
- Phase 5: benchmark, update README §Performance.
