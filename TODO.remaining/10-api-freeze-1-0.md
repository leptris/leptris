# 10 — API freeze checklist for 1.x

DONE (2026-08-22, post-v1.1.0). The audit results:

- types.h single canonical header (TODO 99): VERIFIED CLEAN. Every
  public type has exactly one unguarded definition, in
  src/include/leptris/types.h. The duplicates in dtd.h /
  dom/element.h sit behind the shared LEPTRIS_INTERNAL_TYPES_DEFINED
  guard (order-independent), and dom/node.h's node-type enum is a
  typedef alias to the public LeptrisNodeKind (one definition, no
  value drift — see PR #464).
- Public status constants only (TODO 98): VERIFIED CLEAN. Exhaustive
  grep of every `*status = LEPTRIS_*` write in src/leptris: all
  values come from the public LeptrisStatus enum; the internal
  leptris_error_code members are written only to the thread-local
  channel in error.c. ParseStatusContract specs pin the behavior.
- Versioned symbol visibility: leptris.symvers binds the public
  surface to the LEPTRIS_1 version node (global leptris_*, local
  everything else), applied via --version-script on ELF builds
  (GNU ld/gold/lld); macOS and Windows skip it. Future ABI-breaking
  releases version against a LEPTRIS_2 node instead of a hard soname
  bump.
- Memory-ownership contracts: coverage audit (scripted scan of every
  LEPTRIS_API declaration returning a pointer/handle) found 19
  functions without a "Memory:" note — all filled: the *_node_create
  trio, node_parent, element_as_node, the doctype getters,
  status_string, version, the c14n_* family, xpath_eval_with_vars
  context, and the xinclude getters. Every pointer the public API
  hands out now states who frees it.

With this, the 1.x public surface is settled as specified.
