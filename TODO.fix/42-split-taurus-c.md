# TODO 42: Execute taurus.c split — phases 1-4

**Priority**: ~~P2~~ Done
**Status**: All 4 phases merged
**Effort**: Done

## Outcome

taurus.c went from **2646 lines** (start of session) to **900 lines**
(after phase 4). All four phases landed:

### Phase 1: encoding wrapper extraction (TODO 73)

- Created `src/taurus/encoding/wrapper.c` (122 lines)
- Owns `taurus_parse_string_with_encoding` and the UTF-16/iconv
  detection logic.
- Fixed a double-buffer leak discovered during extraction.

### Phase 2: Node + XPath public API extraction (PR #25)

- Created `src/taurus/dom/node_public.c` (135 lines) — TaurusNodeRef
  accessors, type casts, content getters for text/comment/cdata/PI.
- Created `src/taurus/xpath/xpath_public.c` (282 lines) —
  `taurus_xpath_eval`, `taurus_xpath_result_*`,
  `taurus_xpath_variable_set_*`, `taurus_xpath_eval_with_vars`.
- taurus.c: 2646 → 2169 lines.

### Phase 3: Element query API extraction (PR #27 + #28)

- Created `src/taurus/dom/element_query.c` (883 lines) — all
  `taurus_element_*`, `taurus_element_attribute_*`,
  `taurus_element_text_*`, `taurus_element_namespace_*` functions
  plus the type-safe setter wrappers and `taurus_element_hash_value`.
- taurus.c: 2169 → 1300 lines.

### Phase 4: C14N extraction (PR #30)

- Created `src/taurus/serialize/c14n.c` (441 lines) — owns
  `taurus_c14n_canonicalize` plus `c14n_escape_text`,
  `compare_attributes`, `compare_namespaces`, `c14n_serialize_element`.
- taurus.c: 1300 → 900 lines.

## What's left in taurus.c (900 lines)

- Utility macros (APPEND_STRING buffer-growth helper)
- Forward declarations
- `taurus_parse` + `taurus_parse_inplace` (the parse core, ~300 lines)
- Version information + parse options
- Document lifecycle (`parse_string`, `parse_string_inplace`,
  `parse_with_options`, `parse_file`, `document_free`, `document_root`,
  `serialize_document`)
- File I/O (`taurus_load_file`)
- Memory management helpers
- Document string finalization

These all logically belong together — they're the document lifecycle.
Further extraction would create artificial module boundaries.

## Acceptance

All four phases met:
- taurus.c < 1500 lines → **achieved 900**
- All 105 tests pass unchanged → **yes**
- ASAN run reports zero leaks → **yes**
- Public API unchanged → **yes**
