# TODO 70: Final architecture review

**Priority**: P3 (architecture roadmap)
**Status**: Design only
**Effort**: N/A (documentation)

## Purpose

A consolidated view of architectural debt remaining after the
validation passes.  This is the roadmap for future work.

## The three big refactors

### 1. Unify string ownership (TODO 41)

**Current**: three string lifetimes
- Pool-owned (calloc'd via `leptris_pool_strdup`).
- Document-buffer (StringView pointing into `doc->xml_buffer`).
- Heap-allocated (`leptris_strdup` / `leptris_sv_to_cstr` — calloc'd
  with manual free).

**Target**: one — pool-owned.  Every string reachable from a document
lives in the document's pool.

**Why it matters**: the current mix is the root cause of every leak
the validation pass chased.  Eliminating the calloc path makes leaks
structurally impossible.

**Approach**: mechanical migration.  Every `strdup` / `calloc` for
strings becomes `leptris_pool_strdup`.  `StringView` becomes a
parsing-only optimization (zero-copy from `xml_buffer`); every
StringView is converted to a pool-owned C string before being stored
on a node.

### 2. Split `leptris.c` (TODOs 24/42/45/54)

**Current**: `leptris.c` is 2900+ lines mixing 10+ concerns.

**Target**: 5-6 focused modules under `src/leptris/dom/` and
`src/leptris/encoding/`.

**Why it matters**: navigability, lower cognitive load for changes,
each file <500 lines.

**Approach**: 5-phase migration documented in TODO 24.  Each phase
is one commit; tests pass between phases.

### 3. Full thread safety (TODOs 27/38/48/64)

**Current**: process-global → `__thread`.  Two documents in the same
thread can't differ in strict mode (post-TODO 38: can) or allocator
hooks (still can't).

**Target**: per-document state for everything that's currently
thread-default.

**Why it matters**: enables multi-tenant use cases (plugins,
FFI, embedded scripting).

**Approach**: same pattern as TODO 38 — add fields to
`LeptrisDocument`, propagate through the parser and pool.

## What we have now

After TODOs 01-69:

- **Build**: clean, all features.
- **Tests**: 79+ specs across 11 modules.
- **Leaks**: 0/0 on every fixture.
- **Warnings**: 0.
- **Performance**: faster than libxml2 on every benchmark.
- **Memory safety**: ASAN + libFuzzer + CI workflows.
- **Dispatch**: vtable registry, additive extension.
- **State scope**: document-scoped strict mode + depth limit.

The remaining work is structural cleanup, not correctness.  Each
item above is a focused refactor session, not a quick fix.

## Recommendation

Sequence the refactors:

1. **Unify string ownership first** — eliminates an entire class of
   potential bugs; the migration touches the same code paths as the
   split, so do them in this order.
2. **Split `leptris.c`** — easier once string ownership is unified
   (fewer cross-module allocations to reason about).
3. **Per-doc allocators last** — depends on the pool refactor being
   in place.

Each refactor is independently valuable; together they take the
codebase from "passing tests" to "structurally correct."
