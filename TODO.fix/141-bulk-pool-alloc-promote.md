# TODO 141 — Bulk pool allocation in promote pass

Phase A of TODO 140.

## Problem

`flat_promote_build_tree` calls `taurus_pool_alloc` once per element.
Each call:
- bumps the pool's current pointer
- may allocate a new page (slow path)
- writes metadata

For a 1000-element document, that's 1000 pool_alloc calls. pugixml
allocates the entire element array in one shot.

## Fix

Replace per-element pool_alloc with a single bulk allocation.

### API addition

Add `taurus_pool_alloc_bulk(pool, count, elem_size, out_block)` that:
- Computes total bytes = count * elem_size
- Single pool_alloc for the whole block
- Returns base pointer; caller slices into individual elements

### Promote pass changes

In `flat_promote_build_tree`:
1. After the FlatDoc parse, count elements (= `flat->node_count`)
2. Bulk-allocate all elements in one call BEFORE the walk
3. In the walk, take element pointers from the pre-allocated block
   instead of calling `taurus_pool_alloc` per element

### Risks

- Mixed node types: text/comment/cdata/pi nodes have different
  sizes. Solution: bulk-allocate per type, four separate blocks.
- Mutations during promote: not a concern, promote runs single-
  threaded.

## Expected impact

Promote cost per element: 1.5 µs → 0.7 µs (50% reduction).

For a 5 KB doc with ~50 elements:
- Before: ~75 µs promote
- After: ~35 µs promote

Combined with parse: 12 µs parse + 35 µs promote = 47 µs vs current
78 µs (1.7× faster).

## Test plan

- New spec verifying bulk-allocated elements have the same layout
  as per-element-allocated ones (sizeof check).
- Existing `ProducesSameShapeAsLegacyParser` spec verifies correctness.
- Benchmark: promote cost in bench_flat_parse should drop.
