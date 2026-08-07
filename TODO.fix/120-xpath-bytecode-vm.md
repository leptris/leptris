# TODO 120 — XPath bytecode VM (compile-once, eval-many)

**Priority**: P3
**Status**: Phase A+B shipped (PR #127 + this PR). Phases C-E remain.

## Shipped

### Phase A (PR #127)
- `bytecode.h`: opcode enum (`XPATH_BC_*`), constant-pool types,
  `TaurusXPathBytecode` struct.
- `compiler.c`: AST → bytecode recursive-descent emitter. Handles
  literals (number, string), path expressions, steps, node tests.
- `vm.c`: stack-based interpreter dispatch loop. Handles literals
  + ROOT_CONTEXT. All other opcodes fall back to AST evaluator.

### Phase B (this PR)
- Compiler extended to handle ALL AST node types:
  `PREDICATE`, `FUNCTION_CALL`, `ABSOLUTE_PATH`, `RELATIVE_PATH`,
  `VARIABLE_REFERENCE`, `ARGUMENT`.
- Unsupported/wrapper types emit `BC_FALLBACK_EVAL` with AST node
  pointer in constant pool.

## Remaining

### Phase C — VM inline dispatch
The VM currently delegates everything except literals to the AST
evaluator.  To deliver real perf wins, the VM must dispatch on
more opcodes:

- `BC_BINARY_OP`: pop two VMValues, apply operator, push result.
  Needs VMValue ↔ XPathResult conversion helpers.
- `BC_FUNC_CALL`: look up function in registry, pop N args, call.
  Needs function registry access from VM.
- `BC_AXIS_STEP`: the hardest.  Each of the 13 axes walks the DOM
  differently.  Options:
  (a) Call existing axis_* functions from the VM (thin wrapper).
  (b) Inline axis logic into the VM (maximum perf, ~600 lines).

Option (a) is simpler and still saves the AST dispatch overhead.
Option (b) is the pugixml approach but requires duplicating
evaluator_axes.c logic.

### Phase D — wire into taurus_xpath_eval
Replace the default evaluation path:
```
taurus_xpath_eval → parse → compile_ast → vm_eval
                               ↓ (on fallback)
                               evaluate_expr(ast)
```
Keep AST evaluator as fallback for queries the VM can't handle.

### Phase E — accept
- W3C XPath 1.0 conformance: still 438/438.
- Benchmarks: `//book` on 10 KB doc under 5 µs after first compile.
- New spec: compile a query, serialize bytecode, deserialize, run.

## Architecture notes

The VM is designed as an open/closed system: adding a new opcode =
append to the enum + add a case in the VM dispatch switch.  No
existing code changes.  The compiler maps AST types to opcodes in
a single switch; new AST types get a default `BC_FALLBACK_EVAL`
case.

The constant pool is a tagged union (`XPATH_CONST_NUMBER`,
`XPATH_CONST_STRING`, `XPATH_CONST_AST_NODE`).  The AST-node type
allows the VM to punt any subexpression to `evaluate_expr` without
losing the AST reference.

## Estimated effort for Phases C-E

- Phase C (VM inline): ~800 lines across vm.c + conversion helpers.
- Phase D (wire-in): ~50 lines in taurus_xpath_eval.
- Phase E (testing): ~200 lines of new specs + benchmark validation.

Total: ~1050 lines. 2-3 focused sessions.
