# 08 — The threading contract, stated and enforced

The concurrency audit (2026-08-23): documents are independent (pool,
element index, rank cache, custom-fn registrations are per-document);
the compact overflow table and current-document pointer are already
thread-local. Remaining process-global hazards:

- error.c channel → fixed by 01 (thread-local).
- XPath AST/bytecode cache (xpath_ast_cache.c g_cache) — stays
  PROCESS-GLOBAL under a mutex (a TLS cache would leak ASTs on
  thread exit; no portable C99 TLS destructors). Three races fixed:
  (a) twin insert freed the caller's AST while it was still in use —
  insert now returns the canonical (pinned) AST; (b) LRU eviction
  freed AST/bytecode underneath a running evaluate — entries are now
  pin-counted and evicted-but-pinned entries go to a graveyard freed
  on last release; (c) compile reads the shared AST, verified
  read-only.
- g_standard_registry lazy init — double-init race on first
  concurrent eval; moves to a load-time constructor (both MSVC and
  ELF/Mach-O support them).
- SIMD dispatch pointers (simd_text.c) and the parser's cached
  getenv debug flags — lazy first-call writes raced; dispatch now
  constructor-initialized, the getenv flags are thread-local.

Deliverable: README "Threading" section stating the contract — one
document per thread needs no locking; documents may migrate between
threads only between calls; nothing else is shared. Plus a
concurrency stress spec (test/concurrency): 4 threads ×
parse/xpath/serialize/free loops, ASAN-clean.

DONE 2026-08-23: README "Threading model" section + stress spec
(test/concurrency: 4 threads x parse/eval/serialize/free, TLS error
isolation, per-doc error slots). Found and fixed one real corruption
(twin-insert use-after-free, reproduced: first eval per thread
returned "Unsupported AST node type: <garbage>") and two TSAN races
(SIMD dispatch init, getenv flag caching). ThreadSanitizer-clean on
the full suite; the lone TSAN failure (CliFormat.CompactByDefault)
is the macOS TSAN-runtime-after-fork artifact, not a library race.
