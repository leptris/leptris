# TODO 42: Execute leptris.c split — phases 1-4

**Priority**: ~~P2~~ Done
**Status**: All 4 phases merged
**Effort**: Done

## Outcome

leptris.c went from **2646 lines** (start of session) to **900 lines**
(after phase 4). All four phases landed:

### Phase 1: encoding wrapper extraction (TODO 73)

- Created `src/leptris/encoding/wrapper.c` (122 lines)
- Owns `leptris_parse_string_with_encoding` and the UTF-16/iconv
  detection logic.
- Fixed a double-buffer leak discovered during extraction.

### Phase 2: Node + XPath public API extraction (PR #25)

- Created `src/leptris/dom/node_public.c` (135 lines) — LeptrisNodeRef
  accessors, type casts, content getters for text/comment/cdata/PI.
- Created `src/leptris/xpath/xpath_public.c` (282 lines) —
  `leptris_xpath_eval`, `leptris_xpath_result_*`,
  `leptris_xpath_variable_set_*`, `leptris_xpath_eval_with_vars`.
- leptris.c: 2646 → 2169 lines.

### Phase 3: Element query API extraction (PR #27 + #28)

- Created `src/leptris/dom/element_query.c` (883 lines) — all
  `leptris_element_*`, `leptris_element_attribute_*`,
  `leptris_element_text_*`, `leptris_element_namespace_*` functions
  plus the type-safe setter wrappers and `leptris_element_hash_value`.
- leptris.c: 2169 → 1300 lines.

### Phase 4: C14N extraction (PR #30)

- Created `src/leptris/serialize/c14n.c` (441 lines) — owns
  `leptris_c14n_canonicalize` plus `c14n_escape_text`,
  `compare_attributes`, `compare_namespaces`, `c14n_serialize_element`.
- leptris.c: 1300 → 900 lines.

## What's left in leptris.c (900 lines)

- Utility macros (APPEND_STRING buffer-growth helper)
- Forward declarations
- `leptris_parse` + `leptris_parse_inplace` (the parse core, ~300 lines)
- Version information + parse options
- Document lifecycle (`parse_string`, `parse_string_inplace`,
  `parse_with_options`, `parse_file`, `document_free`, `document_root`,
  `serialize_document`)
- File I/O (`leptris_load_file`)
- Memory management helpers
- Document string finalization

These all logically belong together — they're the document lifecycle.
Further extraction would create artificial module boundaries.

## Acceptance

All four phases met:
- leptris.c < 1500 lines → **achieved 900**
- All 105 tests pass unchanged → **yes**
- ASAN run reports zero leaks → **yes**
- Public API unchanged → **yes**
