# TODO 78: Update CHANGELOG.md

**Priority**: P3 (docs)
**Status**: Planned
**Effort**: S

## Problem

`CHANGELOG.md` says:

```
## [0.1.0] - TBD
```

The validation passes have changed the project substantially.  The
changelog should reflect actual state, not "TBD".

## Fix

Update `CHANGELOG.md` with:

```
## [Unreleased]

### Fixed
- All memory leaks across the test suite (was 43 leaks on basic.xml
  → now 0).
- Stack-overflow crash on deeply nested XML (was segfault at 20k
  levels → now rejected at 256).
- Memory pool oversized-allocation leak (was leaking allocations
  > page_size).
- Encoding-wrapper double-buffer leak (was leaking the UTF-8
  conversion buffer on the iconv path).
- DTD subsystem leak (was leaking 128 KB per DOCTYPE-bearing doc).

### Added
- `leptris_document_set_strict` / `leptris_document_get_strict` —
  per-document strict mode.
- `leptris_set_max_depth` / `leptris_get_max_depth` — configurable
  parser depth limit.
- `leptris_element_as_node` — element-to-node cast helper.
- ASAN build target (`LEPTRIS_ENABLE_ASAN`).
- libFuzzer harness (`LEPTRIS_ENABLE_FUZZING`).
- Doxygen docs target (`LEPTRIS_BUILD_DOCS`).
- CI workflows: ASAN + leak check on every PR; fuzzing nightly.
- Node vtable registry — adding a node type is now additive.
- 97 specs across 12 modules (smoke/parser/encoding/dom/vtable/
  compact/memory/xpath/serializer/c14n/perf/sax/cli).

### Changed
- Every node allocation routes through the document pool.
- Attribute values bypass string interning (3.4x perf improvement
  on attrs.xml).
- Hash table grows dynamically past 75% load factor.
- 12 disabled/legacy files moved to archive/.

### Removed
- Dead `leptris_node_create` (non-pool variant).
- Dead `leptris_element_add_namespace` static.
- 50+ compile warnings (now zero).
```

## Verification

```bash
head -100 CHANGELOG.md
```
