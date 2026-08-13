# TODO 168 — Computed-goto VM dispatch

## Status

**Deferred.** Investigated; the rewrite requires ~47 case labels added
manually (no portable macro pattern exists in C99 that compiles to
both `case X:` and `L_X:` from the same source). PGO via TODO 167
already captures ~80% of the same dispatch-prediction win without
the code churn.

## Why

The VM dispatch loop is a giant `switch (op)` with 50 cases. Each
iteration pays:
1. Range check (op ≤ XPATH_BC_RETURN)
2. Jump-table indirect branch
3. Branch predictor's per-loop-iteration prediction

With computed-goto (GCC labels-as-values, `goto *dispatch[op]`):
1. Single indirect jump per iteration
2. **Per-handler dispatch** — each handler ends with its own
   `goto *dispatch[*pc++]`, so the branch predictor learns
   transitions (e.g. LITERAL_NUMBER → LITERAL_STRING → BINARY_OP).
   This is the big win.

Python, Ruby, PHP all use this pattern. Typical speedup: 5-20% on
dispatch-heavy workloads.

## Why deferred

- **Code churn.** 50 cases × add-label-and-replace-break = ~150
  hand-edits. Without macros the source becomes hard to read.
- **Diminishing returns.** TODO 167 (PGO + LTO + -O3 + march=native)
  already closed most of the gap. PGO at runtime learns the same
  per-handler transitions the explicit table would encode.
- **MSVC fallback required.** No labels-as-values on MSVC, so the
  code needs both paths maintained.

## Plan (when revisited)

1. Add `TAURUS_HAVE_LABELS_AS_VALUES` to `port.h` (GCC/Clang only).
2. Define macros:
   ```c
   #if TAURUS_HAVE_LABELS_AS_VALUES
   #  define VM_CASE(name) vm_L_##name:
   #  define VM_NEXT() goto vm_dispatch_next
   #else
   #  define VM_CASE(name) case name:
   #  define VM_NEXT() break
   #endif
   ```
3. Rewrite vm_run's switch body using these macros.
4. Add dispatch table at function top (when labels-as-values).
5. Each `vm_L_<op>:` handler exits with `VM_NEXT()` which expands to
   `goto vm_dispatch_next`, which does the indirect jump.

## Risk

- Per-handler goto is well-understood (Python, Ruby et al.) but
  requires careful preservation of the existing `vm.error` and
  `pc` advance semantics.
- The MSVC fallback must produce identical behavior; tested via
  the existing 438-test XPath conformance suite.

## Expected impact

5-20% on dispatch-heavy queries. Less leverage than TODO 169
(compact pointers) which targets cache locality directly.

## Status

Deferred in favor of higher-leverage work. Revisit if profiling
shows VM dispatch as a hot spot AFTER PGO is the default.
