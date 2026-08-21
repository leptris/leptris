# TODO 163 — Stack-allocated XPathContext (medium-leverage, medium-risk)

## Status

**DONE.** Added `xpath_context_init` / `xpath_context_cleanup` to
`evaluator.h` / `evaluator.c`. Updated `leptris_xpath_eval` and
`leptris_xpath_eval_with_vars` in `xpath_public.c` to stack-allocate
the context via `XPathContext ctx_storage;` instead of malloc'ing.
Legacy `xpath_context_new` / `xpath_context_free` preserved as
thin wrappers (malloc + init / cleanup + free) for any external
callers.

Benchmark delta is in the noise floor (4.59 µs → 4.44–4.54 µs
total CPU on `bench_xpath_leptris`). Structural win: one fewer
malloc/free syscall per `leptris_xpath_eval` call.

## Why

`xpath_context_new` allocates a ~256-byte `XPathContext` struct
on the heap per `leptris_xpath_eval` call. The struct lives for
the duration of one evaluation; no one stashes the pointer past
`xpath_context_free`.

On `bench_xpath_leptris` this is ~100 ns per call — modest, but
consistent across every eval. Stack-allocating the context in
`leptris_xpath_eval` would eliminate the malloc/free pair.

## Plan

### Phase A — Stack-allocate in `leptris_xpath_eval`

Replace:
```c
XPathContext* xpath_ctx = xpath_context_new(doc, context_elem);
...
xpath_context_free(xpath_ctx);
```
with:
```c
XPathContext ctx_storage;
XPathContext* xpath_ctx = xpath_context_init(&ctx_storage, doc, context_elem);
...
xpath_context_cleanup(xpath_ctx);  /* no free */
```

`xpath_context_new` becomes a thin wrapper that mallocs + calls
`_init` (preserves API for any external callers). `_init` does
the field setup; `_cleanup` releases owned resources without
freeing the struct.

### Phase B — Audit for pointer-stashing

Search for callers that store `XPathContext*` past the eval call:

```
grep -rn 'XPathContext\*' src/leptris/xpath/ | grep -v 'static\|xpath_context_'
```

The only legitimate holders during a single eval are:
- evaluator helper functions (called from within eval — fine)
- bytecode VM functions (called from within eval — fine)
- custom function handlers (called from within eval — fine)

No long-lived holders expected. If any are found, they need to
allocate their own context on the heap.

## Risk

- If any caller does stash the context pointer for later use,
  they'll have a dangling pointer after `leptris_xpath_eval`
  returns. Need a thorough audit.
- The struct is 256 bytes (mostly the `error_msg[256]` buffer).
  Stack-allocating may stress restricted-stack environments
  (embedded threads with small stacks). Mitigation: keep the
  heap path as a fallback via a build option, or split
  `error_msg` into a separate allocation.

## Expected impact

~100 ns per `leptris_xpath_eval` call. On `bench_xpath_leptris`
total wall (~6 µs) that's ~1.5%. On its own not material; in
combination with [[162-result-struct-free-list]] and the existing
nodeset free-list, the per-call malloc count drops from ~5 to ~0.

## Status

Pending.
