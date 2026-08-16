# TODO 192: Subtree-interval element index — beat pugixml on repeated queries

**Priority**: P1 (the one place we can be strictly FASTER, not parity)
**Status**: Done (2026-08-16) — subtree_end + match_positions in the
index; relative-`//` compiler fusion ([d-o-s node()][child b] →
BC_AXIS_DESCENDANT_NAME); VM interval path with nested-subtree skip;
remove_child/remove_all_children index invalidation (latent stale-index
bug the new specs exposed). Gate: 546/546 + ASAN + zero leaks; `.//item`
from 200 section contexts: 2.0 → 0.30 µs/query (6.6× vs main);
pugixml with reused xpath_query = 0.42 µs — taurus 1.4× FASTER.
**Effort**: M

## Why this is the lever pugixml cannot match

From the v1.16 audit (TODO 185 round 10): pugixml's XPath has NO
document index — every `//name`, from any context, walks the tree
every time (their step_fill axis loops; no caching, no precomputation
beyond translate tables / @attr='const' folding).

We already have `taurus_element_index` (TODO 132/133: preorder flat
array + name buckets + attr-value buckets), but it only serves
ROOT-context queries (vm.c:237, vm.c:603 — both `vm_apply_absolute`
paths). Relative descendants (`.//x`, `a//b`, `$ctx//x`) still walk.
This TODO extends the index to subtree-restricted queries — after
which the second+ query from ANY context is O(K matches), not O(N
subtree). That is a beyond-parity win on the repeated-query
workloads `bench_xpath_pugixml` models.

## Key fact that makes it cheap

`all_elements` is filled by `index_walk` in PREORDER, and element
pointers from the parse arena are monotonic in document order — so
preorder position == array index, and pointer order == index order.
A subtree is therefore a CONTIGUOUS interval of the flat array:

- element at preorder position `i` covers `[i, subtree_end[i]]`
- `subtree_end[i]` = `all_count - 1` after walking all children of
  element i (single line in the existing recursive walk — no extra
  pass, no parent pointers, no tree re-walk).

Subtree-restricted bucket query for context element C:

1. Binary-search C in `all_elements` by pointer (O(log N)) → `lo`.
2. `hi = subtree_end[lo]` → pointer bounds `[all_elements[lo],
   all_elements[hi]]` (inclusive; use <= hi bound on pointers).
3. Filter the name bucket's matches: keep `e` with
   `lo_ptr <= e <= hi_ptr`. Pure pointer compares — the same cost
   class as the existing absolute bucket scan.

## Changes

1. `dom/element_index.h`: add `size_t* subtree_end;` to
   `struct taurus_element_index` + accessor:
   `void taurus_element_index_lookup_subtree(const TaurusElementIndex* idx, TaurusElement ctx, const char* name, TaurusElement** out, size_t* out_count)`
   (returns bucket-filtered array view; caller must not free).
2. `dom/element_index.c`:
   - allocate `subtree_end` alongside `all_elements` in build;
   - set `idx->subtree_end[me] = idx->all_count - 1;` at the end of
     each `index_walk` frame (after the child loop);
   - free it in `taurus_element_index_free`;
   - implement the lookup (binary search + bucket filter; fall back
     to empty/NULL when ctx is not in the array — foreign doc).
3. `xpath/vm.c`: at the RELATIVE descendant step (the non-absolute
   path that currently walks descendants from each context node),
   consult the index (same lazy-build + `axis_query_count >= 2`
   deferral TODO 190 uses) when the node test is a plain name;
   filter per context element; fall back to the walk otherwise.
   Include self (`descendant-or-self`) by allowing `e == ctx`.
4. Specs (`test/xpath/`): `.//name` count under non-root context;
   `a//b` chained; context with no matches; deep-only matches;
   `descendant::name` explicit axis; cross-document context
   (foreign element handle) falls back safely; index invalidation
   after mutation (existing invalidation path must clear subtree
   results — it already rebuilds, so just a spec).
5. Benchmark gate: extend `benchmarks/xpath/bench_pugixml.cpp` with
   a repeated relative-query section (e.g. 100 × `count(.//item)`
   from each section element). Gate: no regression on the existing
   sections; the new section should beat pugixml's per-query walk
   after the first query (index build amortization).

## Risks / notes

- Mutation invalidation already exists (index_invalidate on tree
  mutation); the interval array rides the same lifecycle.
- `descendant::` (strict) = filter `e != ctx` on the same interval.
- Predicated steps (`//x[@a='v']`) can combine with the existing
  attr-value buckets — later follow-up, not this TODO.
- Do NOT touch `direct_parse.c` (codegen wall, TODO 185 round 10);
  this change is entirely in dom/ + xpath/ + benchmarks.

## Also beyond pugixml (ranked backlog)

1. This TODO — subtree intervals (biggest).
2. VM pre-evaluation of constant string-function args
   (concat/contains/substring) — pugixml folds only translate
   tables; README Planned item.
3. `madvise(MADV_HUGEPAGE)` on large arenas (Linux) for the
   multi-MB attr blocks; macOS cannot measure — packager win only.
4. SIMD scans beyond count3 — blocked by the codegen wall for
   in-loop edits; revisit only with an architectural change.
