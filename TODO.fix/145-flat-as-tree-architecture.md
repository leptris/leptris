# TODO 145 — Flat-as-tree: make the FlatDoc the primary representation

## Why

The current two-representation model (FlatDoc + compact-pointer tree,
bridged by promote) caps our parse + first-access performance at
roughly 2× what the legacy parser does. pugixml achieves 5–30×
better than us on parse because its tree IS the flat buffer — no
promotion step exists.

Closing this gap requires making the FlatDoc the primary
representation that the public API and all internal consumers
operate on directly. Promote becomes an optional one-way door for
mutation-heavy workloads, not a mandatory toll on every parse.

## Current architecture (v0.5.3)

```
XML input
    ↓
flat_parse  ──────────────▶  FlatDoc
                                  ↓ (lazy, on first tree access)
                              flat_promote_into
                                  ↓
                              Compact-pointer tree (96 B elem)
                                  ↓
                              Public API (LeptrisElement)
```

- FlatNode: 28 B + 12 B attr (zero-copy into input buffer)
- LeptrisElement: 96 B (pool-allocated, compact-pointer edges)
- Promote cost: ~1.5 µs per element

## Target architecture

```
XML input
    ↓
flat_parse  ──────────────▶  FlatDoc  ◀─── primary representation
                                  │
                                  ├── Public API operates directly
                                  │   on FlatDoc for read-only paths
                                  │
                                  └── (optional) mutation triggers
                                      lazy conversion to compact
                                      tree for write-heavy paths
```

- FlatNode grows to ~40 B (adds document back-pointer + a few flags)
  but still 2.4× smaller than LeptrisElement.
- Promote becomes opt-in for mutation, not required for reads.

## Phases

### Phase 1 — FlatDoc-aware public API accessors

Add internal flat-mode accessors that operate directly on FlatDoc:

```c
const char* flat_elem_name(struct leptris_document* doc, FlatNode* n);
FlatNode* flat_elem_parent(struct leptris_document* doc, FlatNode* n);
FlatNode* flat_elem_first_child(struct leptris_document* doc, FlatNode* n);
FlatNode* flat_elem_next_sibling(struct leptris_document* doc, FlatNode* n);
const char* flat_elem_attr_value(FlatNode* n, const char* name);
size_t flat_elem_attr_count(FlatNode* n);
```

Public API entry points (leptris_element_name, etc.) gain a dispatch:
if the doc has flat_doc and hasn't been mutated, route to flat
accessors. No promote needed.

This phase is invisible to existing callers — same return values,
just faster.

### Phase 2 — Flat-mode serialize / C14N / XPath

Update the serialize, C14N, and XPath code paths to operate on
FlatDoc when available. The walks are simpler than the compact-
pointer walks because array indexing is branch-free.

XPath VM gets a new dispatch table entry: when the doc is flat and
the expression matches a fast-path pattern (count, simple axis),
evaluate against FlatDoc directly.

### Phase 3 — FlatDoc grows to support mutation

Add the few fields needed to make FlatDoc support append_child /
set_name / set_attribute. The mutation API marks the doc as
"mutated" which forces promote on next access (one-way door).

For workloads that never mutate (most parse-then-query code), the
compact-pointer tree is never built. For workloads that do mutate,
the cost is paid once at first mutation, not at first read.

### Phase 4 — Deprecate the dual-representation promote path

After Phase 3 stabilizes, remove the always-promote behavior. New
default: FlatDoc is the tree; promote runs only when explicitly
requested via a new leptris_document_compact() API for users who
need the compact-pointer layout (rare).

## Expected performance after completion

| Operation                | Current | Target | pugixml |
|--------------------------|---------|--------|---------|
| Parse only (5 KB)        | 53 µs   | 50 µs  | ~5 µs   |
| Parse + first read       | 78 µs   | 55 µs  | ~5 µs   |
| Parse + XPath count      | 100 µs  | 60 µs  | TBD     |
| Parse + mutation         | 78 µs   | 78 µs  | N/A     |

We won't match pugixml's raw parse (their parser is hand-tuned
C++ with PCH), but read-then-query workloads close most of the
gap. Mutation stays at current cost — acceptable since most parses
don't mutate.

## Risk assessment

- **ABI**: LeptrisDocument remains opaque. LeptrisElement semantics
  shift (may point into FlatDoc OR compact tree), but the type is
  opaque so callers can't tell.
- **Correctness**: 460+ existing specs must still pass. The dual-
  representation dispatch is the main correctness risk.
- **Performance regression**: Phase 1's per-accessor dispatch adds
  a branch. Measurable but tiny (~1 ns per call).
- **Memory**: FlatDoc with the new fields is ~40 B/node, vs 96 B
  for compact tree. Net win even with both representations
  coexisting during promote.

## Implementation order

This is a multi-week effort. Ship in 4 PRs (one per phase). Each
phase is independently shippable and lands value:

1. **Phase 1** (this PR): flat accessors + dispatch
2. Phase 2: serialize/c14n/xpath flat-mode walkers
3. Phase 3: FlatDoc mutation support
4. Phase 4: deprecate always-promote

Phase 1 alone unlocks ~30% of the perf win because the dispatch
lets read paths skip promote. Phases 2-3 compound the win.
