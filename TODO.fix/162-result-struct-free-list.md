# TODO 162 — Result struct free-list (medium-leverage, low-risk)

## Status

Pending. Identified in [[161-pugixml-gap-closure-survey]] as the
next medium-leverage XPath per-call optimisation.

## Why

`taurus_xpath_result_new` allocates a `struct taurus_xpath_result`
per `taurus_xpath_eval` call. The result is freed at the end of
the call by `taurus_xpath_result_free`. On `bench_xpath_taurus`
this is one malloc/free pair per query — small, but visible in
profiles.

For comparison, the nodeset free-list (TODO 159 Phase B) eliminated
the `XPathNodeSet` struct malloc/free churn with the same pattern
and shaved ~5% off per-call overhead.

## Plan

### Phase A — Thread-local result free-list

Add a thread-local singly-linked free-list of result structs (cap
32). Pattern matches the nodeset free-list:

- `xpath_result_new`: pop from free-list if non-empty, else malloc.
  Reset fields to defaults.
- `xpath_result_free`: free any owned nodeset/string payload first
  (existing logic), then push the result struct onto the free-list
  if under cap; else free.

Stash the next-pointer in the result struct's padding (the union
is currently 8 bytes; for non-nodeset results, the bytes are
unused). Or add an 8-byte `free_list_next` field — the struct is
not in a hot path so the size bump is acceptable.

### Phase B — Inline nodeset result construction

For nodeset-typed results, `xpath_result_new` + manually setting
`value.nodeset_value` is two writes. Inline this into a single
helper `xpath_result_new_nodeset(nodeset)` and use it from the
~15 sites in `vm.c` and `evaluator.c` that build a nodeset result.

## Risk

- Cap must prevent unbounded growth on pathological call patterns
  (already learned from Phase B nodeset free-list).
- The next-pointer can't share space with a live value; either
  add a field or use padding.
- Threads exiting with cached results leak those structs. Acceptable
  for a process-global cache; same call as Phase B.

## Expected impact

~5% on `bench_xpath_taurus` total wall time. Less than Phase B
because there's only one result per call (vs ~3 nodesets).

## Status

Pending.
