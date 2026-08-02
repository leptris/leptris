# TODO 27: Eliminate process-global mutable state

**Priority**: P1 (architecture / thread safety)
**Status**: Planned
**Effort**: M

## Problem

Several pieces of parser state are stored in process-global (or
thread-local) variables:

| Symbol | Location | Why it's a problem |
|--------|----------|--------------------|
| `g_taurus_strict_mode` | `taurus.c:895` | Strict mode is per-call concept; current API forces one mode for whole process |
| `g_alloc_function` / `g_dealloc_function` | `taurus.c:2458-2459` | Custom allocator is whole-process; can't mix documents with different allocators |
| `g_overflow_table` | `dom/compact.c:150` | Thread-local, fine, but its lifecycle is implicit |
| `g_current_document` | `dom/compact.c:209` | **Bug-prone**: parse-A → parse-B → free-A leaks B's overflow entries into A's free path |

## Root cause

The codebase grew organically.  When a piece of state needed to be
reachable from many call sites, the easy answer was a global.  That
works for single-threaded, one-document-at-a-time use; it breaks
down under:

- Multi-threaded apps parsing different documents concurrently
  (allocator hooks race).
- Apps that mix custom allocators per document.
- Re-entrant parsing (a parser callback that itself parses).

## Fix (phased)

### Phase 1 (this session): document-scoped strict mode

Move `g_taurus_strict_mode` to the document.  Add a public API:

```c
TaurusStatus taurus_document_set_strict(TaurusDocument doc, int strict);
int          taurus_document_get_strict(TaurusDocument doc);
```

Keep `taurus_set_strict_mode(int)` as a process-wide default (used
when a document is created without an explicit setting).  The
parser checks `doc->strict_mode` instead of the global.

### Phase 2: per-document allocator hooks

`TaurusDocument` carries `alloc_hook` / `dealloc_hook` set at parse
time.  `taurus_allocation_function_set` becomes "set the default
hook for new documents."  Pool allocations use the document's hook.

### Phase 3: explicit document context for overflow table

Replace `g_current_document` with an explicit `TaurusDocument*`
parameter on every compact-pointer operation.  This is invasive —
defer until other refactors settle.

## Tests

`test/parser/test_parser.cpp`:

```cpp
TEST(ParserStrictMode, DocumentScopedStrictRejectsBareEntity) {
    // Lenient by default, strict when explicitly enabled.
}

TEST(ParserStrictMode, ProcessWideDefaultStillWorks) {
    // taurus_set_strict_mode() sets the default for new documents.
}
```

Plus a spec that parses two documents concurrently in different
threads, each with a different allocator, and verifies no
cross-contamination.  (Likely reveals a real bug today.)

## Architecture notes

Process-global mutable state is a **library anti-pattern**: it
prevents the library from being used in multi-tenant scenarios
(plugins, FFI, multi-threaded servers).  The C analog of "instance
variable" — the right place for this state is the document, not the
process.

The `__thread` storage class on `g_overflow_table` is fine — it's
thread-local.  But the per-document vs per-thread ambiguity
(g_current_document is `static`, not `__thread` — wait, let me
re-check) is itself a smell.

## Verification

```bash
# Document-scoped strict mode works regardless of process global.
build/test/parser/test_parser --gtest_filter='ParserStrictMode.*'

# Multi-threaded allocator isolation.
build/test/parser/test_parser_multithreaded   # new test
```
