# TODO 130 — Inline VM opcodes for common XPath functions

## Problem

`count(//book)` is 36 µs vs libxml2's ~3 µs (12× slower). The
slowness is NOT in count() itself — it's that count() takes a path
argument that's currently evaluated via the AST evaluator
(evaluate_function_call → evaluate_expr → evaluate_location_path →
evaluate_step × N), bypassing the bytecode VM entirely.

The compiler emits BC_FUNC_CALL for any function, regardless of
whether the function could be inlined. The VM handler then calls
`evaluate_function_call_inline(ctx, ast_fc)` which dispatches to
the function handler with AST args. The handler re-evaluates each
arg via `evaluate_expr`.

After TODO 125-129, the VM is much faster than evaluate_expr for
paths. Function calls that wrap paths don't benefit.

## Plan

Add specialized opcodes for the most common functions. The
compiler emits `<arg bytecode> + BC_FUNC_<NAME>` instead of
`BC_FUNC_CALL <func ast>`. The VM evaluates args via normal
dispatch (which uses the fast specialized axis opcodes), then
applies the function inline.

Initial set:
- BC_FUNC_COUNT — 1 arg nodeset → number. Pops nodeset, pushes count.
- BC_FUNC_STRING — 1 arg optional → string. Convert to string.
- BC_FUNC_BOOLEAN — 1 arg → boolean. Convert to boolean.
- BC_FUNC_NUMBER — 1 arg → number. Convert to number.
- BC_FUNC_NAME — 1 arg optional → string. Get name of first node.
- BC_FUNC_LOCAL_NAME — similar.
- BC_FUNC_NAMESPACE_URI — similar.
- BC_FUNC_SUM — 1 arg nodeset → number. Sum of node values.
- BC_FUNC_POSITION — no arg. Push context_position.
- BC_FUNC_LAST — no arg. Push context_size.
- BC_FUNC_TRUE / BC_FUNC_FALSE — no arg. Push boolean.
- BC_FUNC_NOT — 1 arg boolean → boolean. Negate.
- BC_FUNC_NORMALIZE_SPACE — 1 arg optional → string.
- BC_FUNC_STRING_LENGTH — 1 arg optional → number.
- BC_FUNC_CONCAT — N args → string. Concatenate.
- BC_FUNC_CONTAINS / STARTS_WITH — 2 args → boolean.
- BC_FUNC_SUBSTRING — 2-3 args → string.
- BC_FUNC_SUBSTRING_BEFORE / AFTER — 2 args → string.
- BC_FUNC_TRANSLATE — 3 args → string.
- BC_FUNC_FLOOR / CEILING / ROUND — 1 arg → number.
- BC_FUNC_LANG — 1 arg → boolean.
- BC_FUNC_ID — 1 arg → nodeset (special).

All 27 XPath 1.0 functions get an inline opcode. BC_FUNC_CALL
remains as the fallback for any function not in the standard set
(user-registered functions).

## Expected outcome

| Benchmark           | Today  | After  |
|---------------------|--------|--------|
| count(//book)       | 33 µs  | ~6 µs  |
| sum(//price)        | 41 µs  | ~7 µs  |
| name(//book)        | 35 µs  | ~6 µs  |
| string(//title)     | 36 µs  | ~6 µs  |

Each function call goes from "AST arg eval + handler dispatch" to
"VM arg eval + inline op".

## Branch
`todo-130-inline-function-opcodes`

## Phases
- Phase A: count, sum, position, last, true, false, not (no path
  args or trivial arg handling).
- Phase B: string, boolean, number, name, local-name, namespace-uri
  (single-arg conversion functions).
- Phase C: concat, contains, starts-with, substring, substring-before,
  substring-after, translate, normalize-space, string-length, floor,
  ceiling, round, lang, id (string/numeric functions).
