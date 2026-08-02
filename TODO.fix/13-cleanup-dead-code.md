# TODO 13: Triage disabled and legacy code

**Priority**: P3 (hygiene)
**Status**: Inventory complete; awaiting user decision
**Effort**: S

## Inventory

### Category A: Disabled at the CMake level

`src/CMakeLists.txt` comments these out of TAURUS_SOURCES:

| File | Purpose | Recommendation |
|------|---------|----------------|
| `src/taurus/dtd/validator.c` | DTD content-model validation ("Phase 5/6") | Finish or archive |
| `src/taurus/xinclude/xinclude.c` + `.h` | XInclude support (update for compact arch pending) | Finish or archive |

### Category B: Legacy implementations (not in TAURUS_SOURCES)

`src/taurus/taurus_parse.c`, `parse_content.c`, `parse_document.c`,
`parse_simple.c`, `parse_element.c`, `parse_helpers.{c,h}`,
`parse_internal.h` — superseded by `src/taurus/parse/parser_new.c`.
Active parser is wired in via `src/CMakeLists.txt:46-48`.

### Category C: Backup files

`src/taurus/xpath/evaluator_axes.c.bak2` — leak from a manual edit.

### Category D: Multi-implementation files

`src/taurus/dom/element_compact.c` + `element_fast.c` exist alongside
`element.c`. CMakeLists picks specific files.

## Recommendation

Per the global rule "NEVER DELETE source files," do not `rm` any of
the above. Two acceptable paths:

**Option A (recommended): move to `archive/`** — preserves git
history, keeps the active tree clean. Layout sketch in the original
TODO body.

**Option B: leave in place, add a STATUS block to CMakeLists.txt**
warning readers about which files are dead. Lower-effort, doesn't
fully clean the tree.

## Status

Awaiting user decision. Implementation is one `git mv` batch once a
path is chosen.
