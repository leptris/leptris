# TODO 142 — Document pointer propagation cost in promote

Phase B of TODO 140 (split out for clarity).

## Problem

`flat_promote_build_tree` sets `elem->document = doc` on every
element during the walk. For a 1000-element doc, that's 1000
unnecessary field writes — they all set the same value.

## Fix

Skip the per-element assignment in the loop. After the walk, do a
single BFS/DFS over the built tree setting `document` on every node.
This is one cache-friendly linear pass instead of interleaved with
the per-element construction.

For very large docs this saves ~50 ns per element = ~50 µs total
on a 1000-element doc.

## Risk

Low. The document pointer is used by:
- leptris_element_get_document (XPath context resolution)
- COW version propagation
- Element index build

All of these run AFTER promote completes, so a single post-pass
assignment is safe.

## Test plan

- Existing `ProducesSameShapeAsLegacyParser` spec verifies the tree.
- New spec: walk every element after promote, verify all have
  document pointer set.
