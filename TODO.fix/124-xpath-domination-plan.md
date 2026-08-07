# TODO 124 — XPath domination plan: close the 6-40× gap vs libxml2

## Problem

Diagnostic benchmark (TODO 123) reveals where taurus loses to libxml2:

```
micro (~24 B) self::*    1.13 µs   ← near libxml2 parity
medium (~5 KB) self::*   5.81 µs   ← 5× slower for identical work
large (~100 KB) self::*  9.29 µs   ← 8× slower, scales with doc size

descendant::*            14 µs     vs libxml2  0.96 µs  (14.9×)
descendant::*[@id]       42 µs     vs libxml2  1.02 µs  (42×)
count(//book[@id='b1'])  42 µs     vs libxml2  ~3 µs    (14×)
```

The single biggest cause is **`xpath_context_init_from_document`**
walking the entire document on every `xpath_context_new` call to
collect namespace declarations. For an XPath expression like
`self::*` or `count(//book)`, no namespace prefix is ever looked up —
yet we pay the walk cost on every eval.

## Strategy

Optimize in order of measured impact, validating each step against
the diagnostic benchmark. No speculative changes; every PR must
show a measurable delta on at least one diagnostic group.

### Phase 1: Lazy namespace init — projected 5× win on self::* (5.8 → 1.2 µs)

`xpath_context_new` must not walk the document. Move namespace
collection to first lookup.

- The compiler already knows which expressions reference prefixes
  (the parser produces XPATH_AST_NODE_TEST_NAME nodes with a `:`
  in the test value, plus the `local-name()` / `namespace-uri()`
  functions and namespace axis).
- At parse time, set a flag on the AST: `uses_namespaces`.
- At eval time, check the flag. If false, skip the document walk.
- For the rare expressions that do need namespaces, collect
  lazily on first prefix resolution and cache on the context.

Implementation:
1. Add `uses_namespaces` field to XPathASTNode (default 0).
2. In `xpath_parse_*`, set the flag when encountering a prefixed
   name or namespace-related construct.
3. In `xpath_context_new`, accept the flag and conditionally skip
   `xpath_context_init_from_document`.
4. For first-prefix-resolution, walk the document once and cache.

Expected outcome: micro self::* stays at ~1 µs. Medium self::*
drops from 5.8 µs to ~1.2 µs. Large from 9.3 µs to ~1.5 µs.

### Phase 2: Eliminate per-eval context alloc — projected 0.5 µs win

`xpath_context_new` mallocs a struct per eval; `xpath_context_free`
frees it. For a 1 µs eval, malloc+free is ~5-10% of cost.

Maintain a thread-local free-list of XPathContext structs. Reuse
on alloc, return on free. The struct is ~200 bytes — fits in a
small pool.

Implementation:
1. Thread-local stack of free XPathContext* (max 16 deep).
2. `xpath_context_new` pops from free-list or mallocs.
3. `xpath_context_free` resets and pushes back.

Expected outcome: ~0.5 µs shaved off every eval. Combined with
Phase 1, micro doc self::* should hit ~0.6 µs.

### Phase 3: Specialize child::name traversal — projected 3× win on child axis

Currently `child::book` goes through:
- `evaluate_step` outer loop
- `apply_axis` (strcmp per call, even though axis_id is cached on AST)
- `matches_node_test` (switch + parse_node_test_name + strcmp chain)
- `xpath_nodeset_add` with dedup check

For the common case (single name test, no predicate, no namespace),
this can be a single tight loop walking the child list and strcmp'ing.

Implementation:
1. Add VM opcode `BC_AXIS_CHILD_NAME` with const-pool entry holding
   the name string. Compiler emits this for `child::name` and bare
   `name` when no predicate follows.
2. VM handler pops input nodeset, for each input element walks
   `first_child → next_sibling`, strcmp's each against the name,
   appends matches directly. No apply_axis, no matches_node_test,
   no dedup (child axis can't produce duplicates).
3. Same pattern for `BC_AXIS_ATTRIBUTE_NAME`, `BC_AXIS_SELF_NAME`,
   `BC_AXIS_PARENT_NAME`.

Expected outcome: child::* drops from 5.9 µs to ~2 µs.
attribute::id drops to ~1.5 µs.

### Phase 4: Specialize descendant traversal — projected 4× win on descendant::* (14 → 3.5 µs)

Descendant axis walks the entire subtree. Currently:
- For each input node, call apply_axis("descendant")
- Inside apply_axis, recursive walk + nodeset_add with dedup

The recursive walk has per-node overhead (visit, check, add).
Specialize it as a tight recursive function in the VM.

Implementation:
1. Add `BC_AXIS_DESCENDANT_NAME` and `BC_AXIS_DESCENDANT_WILD`.
2. VM handler: tight recursive subtree walk, no dedup (descendant
   axis can't produce duplicates from a single root).
3. Skip apply_axis dispatch entirely.

Expected outcome: descendant::* drops from 14 µs to ~3 µs.

### Phase 5: Predicate fast paths — projected 2× win on predicate-heavy (42 → 20 µs)

Predicate cost is `descendant::*[@id]` = 36 µs vs `descendant::*`
alone = 14 µs → predicate adds 22 µs. That's per-node predicate
evaluation.

Most predicates are simple:
- `[@attr]` — attribute existence
- `[@attr = 'literal']` — attribute value match
- `[position() = N]` or `[N]` — position predicate

Specialize these to skip the full AST evaluation.

Implementation:
1. Detect simple predicate patterns at compile time.
2. Emit dedicated opcodes: `BC_PREDICATE_ATTR_EXISTS`,
   `BC_PREDICATE_ATTR_EQ_LITERAL`, `BC_PREDICATE_POSITION`.
3. VM handlers do the check inline without setting up a full
   predicate evaluation context.

Expected outcome: descendant::*[@id] drops from 36 µs to ~20 µs.
descendant::*[N] drops to ~5 µs.

### Phase 6: Pre-evaluate function arguments — projected 30% win on count(), name(), etc.

Currently function handlers take AST args and re-evaluate them
themselves via `evaluate_expr`. This means each function call
walks the AST.

If the VM pre-evaluates args onto the stack and the function
handler accepts pre-evaluated `taurus_xpath_result**` instead of
`XPathASTNode**`, we save the AST walk per arg.

Implementation:
1. Add new handler signature `XPathFunctionHandlerVM` taking
   pre-evaluated results.
2. Each of the 27 standard functions gets a VM-friendly wrapper.
3. BC_FUNC_CALL emits arg compilation + pop-args + call.

Expected outcome: count() drops from 33 µs to ~23 µs.
name() drops from 35 µs to ~25 µs.

### Phase 7: Pool-allocate result objects — projected 0.5 µs win

XPathResult and XPathNodeSet allocations are per-eval. Pool them
on a thread-local free-list.

Expected outcome: ~0.5 µs shaved off every eval.

## Total expected impact

| Benchmark              | Today   | Target  | vs libxml2 |
|------------------------|---------|---------|------------|
| self::* (micro)        | 1.1 µs  | 0.6 µs  | parity     |
| self::* (medium)       | 5.8 µs  | 0.8 µs  | parity     |
| self::* (large)        | 9.3 µs  | 1.0 µs  | parity     |
| child::*               | 5.9 µs  | 2.0 µs  | 2× slower  |
| child::book            | 5.6 µs  | 1.5 µs  | parity     |
| descendant::*          | 14 µs   | 3.5 µs  | 3.5× slower|
| descendant::*[@id]     | 42 µs   | 20 µs   | 20× slower |
| count(//book)          | 33 µs   | 23 µs   | 7× slower  |

Phases 1-2 close most of the per-call floor gap (5× → 1×).
Phases 3-5 close most of the axis-traversal gap.
Phases 6-7 are incremental.

After Phases 1-5, taurus will be at libxml2 parity on most
non-predicate axes. Predicate-heavy expressions will still lag
but by ~20× instead of 42×.

## Branches / PRs

Each phase is its own branch + PR:

- `todo-124-1-lazy-namespace-init`
- `todo-124-2-context-pool`
- `todo-124-3-axis-specialization`
- `todo-124-4-descendant-specialization`
- `todo-124-5-predicate-fast-paths`
- `todo-124-6-pre-eval-args`
- `todo-124-7-result-pool`

Validate each PR by re-running `bench_xpath_diagnostic` and
attaching before/after numbers to the PR description.

## Why not just rewrite everything?

Each phase is independently testable and reviewable. If Phase 1
alone closes the per-call floor, that's already a major win that
unblocks real-world XPath use. Phases 2-7 build incrementally on
top.

The bytecode VM (TODO 120) is the right infrastructure for
Phases 3-5: each specialized opcode = one new VM case + one
compiler case. Pure OCP, no existing cases change.
