# 01 — XPath fn: catalog: sequence functions (#691-A)

Adds the sequence-ops slice of #691 over the existing item/nodeset
model. Each function TDD-first against Saxon-HE (probe batch), then
registered in the core registry (functions.c) — no new types.

- exists(S), empty(S), head(S), tail(S)
- reverse(S), unordered(S) (identity)
- index-of(S, v), distinct-values(S)
- remove(S, i), insert-before(S, i, S2), subsequence(S, s[, l])
- zero-or-one, one-or-more, exactly-one (cardinality checks ->
  dynamic error on violation)
- avg(S), min(S), max(S) (numeric + string forms)
- count/sum/position/last already exist — SSOT guard only
- deep-equal(A, B) (document order, string values; whitespace
  trim option second pass)

Gate: Xslt30.FnSequences spec (Saxon-probed) + full suite green.


## Status 2026-09-04 — sequence node tail shipped

innermost/outermost (ancestor/descendant-in-set filters),
has-children (explicit + zero-arg context form; text children
count), path (root-anchored /a/b[2] positional form), nilled
(xsi:nil read). fn:snapshot still open — needs detached-copy result
trees (copy_subtree_detached is the building block).
