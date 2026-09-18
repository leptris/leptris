# Lever 4: Lane 18 compact-node micro-optimizations (non-layout)

Status: TODO. Lane 18 gate: element append ≤ pugixml, attr-parse
≤ pugixml. PR #1017 closed attr-parse to 1.96x; the mutation rows
need the node-layout redesign (parked — NOT this lever). This
lever is the NON-layout remainder.

Low-hanging:
- append_child: sibling-tail cache on the parent (O(1) append
  instead of chain walk) if not already present.
- attr lookup: interned-name pointer compare before strcmp in
  leptris_element_attr (names are pooled; identical strings are
  common in real docs).
- pool alloc batching for text nodes (arena bump is already O(1);
  batch the header init).

RED bench first: benchmarks/dom_benchmark append + attr rows.
