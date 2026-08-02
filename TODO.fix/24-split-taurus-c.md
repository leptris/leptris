# TODO 24: Split `taurus.c` (3000+ lines) into focused modules

**Priority**: P3 (architecture)
**Status**: Design only — execution deferred to a focused refactor session
**Effort**: L

## Problem

`src/taurus/taurus.c` is a 3000+ line monolith. It mixes:

1. **Version accessors** (`taurus_version` etc.) — ~30 lines.
2. **Allocation hook overrides** (`taurus_allocation_function`) — ~50 lines.
3. **Document lifecycle** (`taurus_parse_string`, `taurus_parse_file`,
   `taurus_document_free`) — ~500 lines.
4. **Encoding wrappers** (`taurus_parse_string_with_encoding`,
   UTF-16/iconv conversion) — ~300 lines.
5. **Node query API** (`taurus_node_get_type`, `taurus_text_node_get_content`,
   ~20 functions) — ~400 lines.
6. **Element query API** (`taurus_element_*`, ~40 functions) — ~600 lines.
7. **Attribute modification API** — ~400 lines.
8. **Tree mutation API** (`append_child`, `insert_before`, etc.) — ~300 lines.
9. **Document finalization** (`taurus_document_finalize_strings`,
   `taurus_document_freeze_tree`) — ~200 lines.
10. **Mixed-content helpers** (`taurus_element_text`, recursive text concat).
11. **Compact-pointer overflow table** — global state cleanup.

Mixing these in one file is a maintainability tax: every change has
to find the right section in a 3000-line file, every greps returns
dozens of unrelated hits, and the file's structure doesn't communicate
the API's shape.

## Root cause

The file grew organically as the public API surface expanded. No
one file split was ever "the right time" to do, so none happened.

## Proposed split

Mirror the public header structure (each public header maps to one
implementation file):

```
src/taurus/
├── taurus.c                 ← entry-point version + allocation hooks (~100 lines)
├── dom/
│   ├── document.c           ← taurus_parse_*, taurus_document_*
│   ├── element_query.c      ← taurus_element_name, attribute accessors
│   ├── element_modify.c     ← (already exists, just verify scope)
│   ├── navigation.c         ← taurus_node_*, child/sibling/parent walks
│   └── finalization.c       ← taurus_document_finalize_strings, freeze_tree
├── encoding/
│   └── wrapper.c            ← taurus_parse_string_with_encoding, UTF-16/iconv
│                              glue
└── compact/
    └── overflow.c           ← global overflow-table state and cleanup
```

Each file is **≤ 500 lines** (rough heuristic: fits in a single
screen of grep output, easy to navigate).

## Migration plan

Mechanical, but risky (lots of `static` functions need promoting to
internal-linkage visibility, lots of `extern` declarations to add).

1. Create empty target files with stub headers.
2. Move one logical chunk at a time, with tests passing between
   moves. Don't batch — every commit is one chunk.
3. After each move: `cmake --build build && ctest --test-dir build` —
   zero regressions.
4. Update `src/CMakeLists.txt` `TAURUS_SOURCES` to reflect new file
   list.

Phases:
- Phase 1: split out `encoding/wrapper.c` (self-contained, ~300 lines).
- Phase 2: split out `compact/overflow.c` (also self-contained).
- Phase 3: split out `dom/document.c` (parse entry points).
- Phase 4: split out `dom/navigation.c` (node query API).
- Phase 5: split out `dom/finalization.c`.

## Tests

No behavioral change. Existing specs cover correctness.

Add a "smoke" spec that exercises every public API function — if any
file-split accidentally drops a function from the build, the smoke
spec catches it.

## Architecture notes

This is purely structural. No public API change, no behavior change.
The benefit is **navigability** — opening a 300-line file to find the
function you want is faster than scrolling through 3000 lines.

The split mirrors the public header structure (`src/include/taurus/`)
which makes the "where is this declared → where is it implemented"
lookup a one-to-one mapping.

## Status

Design only. Execution is a focused refactor session — not appropriate
to bundle with correctness fixes (too much churn in one PR). Land
TODOs 15-22 first; this TODO is the "tidy up the workshop" pass that
follows.

## Verification

After phase N completes:

```bash
wc -l src/taurus/taurus.c            # should shrink with each phase
find src/taurus -name "*.c" -exec wc -l {} +   # all files ≤ 500 lines
cmake --build build
ctest --test-dir build               # 100% pass
```
