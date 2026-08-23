# 08 — The threading contract, stated and enforced

The concurrency audit (2026-08-23): documents are independent (pool,
element index, rank cache, custom-fn registrations are per-document);
the compact overflow table and current-document pointer are already
thread-local. Remaining process-global hazards:

- error.c channel → fixed by 01 (thread-local).
- XPath AST/bytecode cache (xpath_ast_cache.c g_cache) — concurrent
  insert corrupts; becomes LEPTRIS_THREAD_LOCAL (per-thread cache
  keeps the reuse win, drops cross-thread sharing).
- g_standard_registry lazy init — double-init race on first
  concurrent eval; moves to a load-time constructor (both MSVC and
  ELF/Mach-O support them).

Deliverable: README "Threading" section stating the contract — one
document per thread needs no locking; documents may migrate between
threads only between calls; nothing else is shared. Plus a
concurrency stress spec (test/concurrency): 4 threads ×
parse/xpath/serialize/free loops, ASAN-clean.

DONE 2026-08-23: all three fixes + README section + stress spec
green under ASAN (MSVC skips the thread spawn, contract still
documented).
