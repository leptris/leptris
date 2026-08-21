# TODO 148 — Nokogiri-compat C API gaps

## Why

The Ruby binding (leptris-ruby, TODO 118) cannot fully match
Nokogiri semantics without these C-side primitives. Each one
blocks a commonly-used Nokogiri method on `Node`, `Element`, or
`Document`. Without them, the Ruby side either raises
`NotImplementedError`, parses XML strings again as a workaround
(slow, lossy), or emulates the behavior in pure Ruby (slow per
call).

## Survey

| API                                                | Blocks (Ruby)                                                       | Impact  |
|----------------------------------------------------|---------------------------------------------------------------------|---------|
| `leptris_element_copy` (detached)                   | `Node#dup`, `#clone`, `Element#dup`                                 | High    |
| `leptris_document_copy`                             | `Document#dup`, `#clone`                                            | High    |
| `leptris_document_internal_subset` / DOCTYPE access | `Document#internal_subset`, `#doctype`, `#validate`                 | High    |
| `leptris_node_get_xpath`                            | `Node#path`, `#css_path`, `#matches?`                               | High    |
| `leptris_parse_fragment`                            | `Document#fragment`, `Node#fragment`, `Node#parse`, in-context add  | Medium  |
| `leptris_xpath_register_function`                   | `Searchable#xpath(expr, ..., handler)`                              | Medium  |
| `leptris_node_line` on flat_promote path            | `Node#line` for documents with DTDs/entities                        | Medium  |

## Plan

Each gap ships as its own PR. Tests under `test/abi/`. The Ruby
FFI binding (separate repo) consumes these as they land.

### Phase 1 — Detached deep copy (PR: `feat/dom-deep-copy`)

- `leptris_element_copy(LeptrisElement src, LeptrisDocument dest_doc)`
  → detached `LeptrisElement`. Subtree is recursively copied into
  `dest_doc->pool`. Returns NULL on bad args or alloc failure.
- `leptris_document_copy(LeptrisDocument src)` → new
  `LeptrisDocument` with the entire tree + declaration + PIs.
- Refactor: `leptris_element_append_copy` becomes
  `leptris_element_copy` + `leptris_element_append_child`.

### Phase 2 — DOCTYPE access (PR: `feat/doctype-public-api`)

- `leptris_document_internal_subset(LeptrisDocument doc)` →
  opaque `LeptrisDoctype` handle (or NULL).
- `leptris_doctype_get_name`, `_get_root_name`,
  `_get_public_id`, `_get_system_id`, `_get_internal_subset`.
- The legacy parser already builds a `LeptrisDoctypeNode`; expose
  it via the public API.

### Phase 3 — `leptris_node_get_xpath` (PR: `feat/node-get-xpath`)

- `leptris_node_get_xpath(LeptrisNodeRef node, char** out, size_t* out_len)`
  → status. Caller frees `*out` via `leptris_free_string`.
- Format: `/qname[sibling_index]` for elements; `text()` for text;
  attribute paths use `@name`. Index is 1-based among same-named
  siblings (matches Nokogiri).

### Phase 4 — DocumentFragment (PR: `feat/parse-fragment`)

- `leptris_parse_fragment(const char* xml, size_t len, LeptrisDocument dest_doc, LeptrisStatus* st)`
  → detached `LeptrisElement` synthetic root (named
  `#document-fragment`) holding parsed children.
- Supports multiple top-level elements (wrap source in
  `<__frag__>...</__frag__>` internally, parse, return the wrapper).
- Caller moves children via existing
  `leptris_element_append_child` (which now unlinks, see #217).

### Phase 5 — Custom XPath function handlers (PR: `feat/xpath-custom-fn`)

- `leptris_xpath_register_function(LeptrisDocument doc, const char* name, LeptrisXPathFn fn, void* user_data)`
- `typedef int (*LeptrisXPathFn)(LeptrisXPathContext* ctx, int argc, LeptrisXPathValue* argv, LeptrisXPathValue* out, void* user_data)`
- Registered functions live in `doc->xpath_functions` (hash table).
- The XPath evaluator checks the table before raising "unknown
  function" — pure extension, no break to existing semantics.

### Phase 6 — flat_promote line tracking (PR: `perf/flat-node-line`)

- Grow `FlatNode` 28 → 32 bytes; add `uint32_t line` field.
- `flat_parser` tracks line as `direct_parse` already does.
- `flat_promote` copies `fn->line` into `node->base.line`.
- Closes the v0.5.14 known limitation.

## Test plan

Every phase ships with specs under `test/abi/test_<feature>.cpp`:
- happy-path parse + access
- NULL arg returns NULL/0
- deep-copy independence: mutating copy doesn't affect source
- DOCTYPE round-trips for SYSTEM/PUBLIC/internal subset
- xpath unique path: same node yields same path; different nodes differ

539 → 545+ specs after all phases land.
