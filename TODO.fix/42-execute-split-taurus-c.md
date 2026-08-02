# TODO 42: Execute split of `taurus.c`

**Priority**: P3 (architecture — maintainability)
**Status**: Planned — execution
**Effort**: L

## Problem

`src/taurus/taurus.c` is 2900+ lines mixing:

1. Version accessors (~30 lines)
2. Allocation hook overrides (~50 lines)
3. Document lifecycle: `taurus_parse_*`, `taurus_document_free` (~600 lines)
4. Encoding wrappers: UTF-16 / iconv glue (~300 lines)
5. Node query API: ~20 functions (~400 lines)
6. Element query API: ~40 functions (~600 lines)
7. Attribute modification API (~400 lines)
8. Tree mutation: `append_child`, `insert_*` (~300 lines)
9. Document finalization: `finalize_strings`, `freeze_tree` (~200 lines)
10. Compact-pointer overflow table cleanup (~100 lines)

TODO 24 designed a split; this TODO executes it.

## Fix (phased)

Each phase is one commit; tests must pass between phases.  No batched
moves.

### Phase 1: extract `encoding/wrapper.c`

Self-contained: `taurus_parse_string_with_encoding`,
`taurus_parse_string_inplace`, UTF-16 BOM detection, iconv dispatch.
~300 lines.

### Phase 2: extract `compact/overflow.c`

`taurus_compact_set_current_document`,
`taurus_compact_cleanup_document`, the `__thread` table state.
~100 lines.

### Phase 3: extract `dom/document.c`

`taurus_parse`, `taurus_document_free`, `taurus_document_root`,
`taurus_document_serialize`, `taurus_document_finalize_strings`.
~500 lines.

### Phase 4: extract `dom/navigation.c`

`taurus_node_get_type`, `taurus_node_first_child`, sibling/parent
walks, `taurus_node_as_element`, `taurus_element_as_node`.
~400 lines.

### Phase 5: extract `dom/finalization.c`

`finalize_element_strings`, `taurus_document_freeze_tree`,
COW helpers.  ~300 lines.

After all phases, `src/taurus/taurus.c` is <200 lines (version +
allocation hooks + a few stragglers).

## Migration checklist per phase

For each move:

1. Create target file with stub.
2. Move functions one at a time.
3. After each function, `cmake --build build && ctest --test-dir build` —
   zero regressions.
4. Update `src/CMakeLists.txt` `TAURUS_SOURCES`.
5. Update `#include` paths in callers if needed.

## Tests

No behavioral change.  Existing 59 specs cover correctness.

Add a "smoke" spec that exercises every public API function — if any
file-split accidentally drops a function from the build, the smoke
spec catches it.

## Architecture notes

The split mirrors the public header structure (`src/include/taurus/`),
making "where is this declared → where is it implemented" a one-to-one
mapping.

Each resulting file is <500 lines, navigable in one screen of grep.
Future changes touch one focused file, not a 2900-line monolith.

## Verification

After phase N:

```bash
wc -l src/taurus/taurus.c            # shrinks with each phase
find src/taurus -name "*.c" -exec wc -l {} + | sort -n | tail -10
cmake --build build
ctest --test-dir build               # 100% pass
```
