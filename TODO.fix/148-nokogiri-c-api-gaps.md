# TODO 148 — Nokogiri-compat C API gaps

## Why

The Ruby binding (taurus-ruby, TODO 118) cannot fully match
Nokogiri semantics without these C-side primitives. Each one
blocks a commonly-used Nokogiri method on `Node`, `Element`, or
`Document`. Without them, the Ruby side either raises
`NotImplementedError`, parses XML strings again as a workaround
(slow, lossy), or emulates the behavior in pure Ruby (slow per
call).

## Survey

| API                                                | Blocks (Ruby)                                                       | Impact  |
|----------------------------------------------------|---------------------------------------------------------------------|---------|
| `taurus_element_copy` (detached)                   | `Node#dup`, `#clone`, `Element#dup`                                 | High    |
| `taurus_document_copy`                             | `Document#dup`, `#clone`                                            | High    |
| `taurus_document_internal_subset` / DOCTYPE access | `Document#internal_subset`, `#doctype`, `#validate`                 | High    |
| `taurus_node_get_xpath`                            | `Node#path`, `#css_path`, `#matches?`                               | High    |
| `taurus_parse_fragment`                            | `Document#fragment`, `Node#fragment`, `Node#parse`, in-context add  | Medium  |
| `taurus_xpath_register_function`                   | `Searchable#xpath(expr, ..., handler)`                              | Medium  |
| `taurus_node_line` on flat_promote path            | `Node#line` for documents with DTDs/entities                        | Medium  |

## Plan

Each gap ships as its own PR. Tests under `test/abi/`. The Ruby
FFI binding (separate repo) consumes these as they land.

### Phase 1 — Detached deep copy (PR: `feat/dom-deep-copy`)

- `taurus_element_copy(TaurusElement src, TaurusDocument dest_doc)`
  → detached `TaurusElement`. Subtree is recursively copied into
  `dest_doc->pool`. Returns NULL on bad args or alloc failure.
- `taurus_document_copy(TaurusDocument src)` → new
  `TaurusDocument` with the entire tree + declaration + PIs.
- Refactor: `taurus_element_append_copy` becomes
  `taurus_element_copy` + `taurus_element_append_child`.

### Phase 2 — DOCTYPE access (PR: `feat/doctype-public-api`)

- `taurus_document_internal_subset(TaurusDocument doc)` →
  opaque `TaurusDoctype` handle (or NULL).
- `taurus_doctype_get_name`, `_get_root_name`,
  `_get_public_id`, `_get_system_id`, `_get_internal_subset`.
- The legacy parser already builds a `TaurusDoctypeNode`; expose
  it via the public API.

### Phase 3 — `taurus_node_get_xpath` (PR: `feat/node-get-xpath`)

- `taurus_node_get_xpath(TaurusNodeRef node, char** out, size_t* out_len)`
  → status. Caller frees `*out` via `taurus_free_string`.
- Format: `/qname[sibling_index]` for elements; `text()` for text;
  attribute paths use `@name`. Index is 1-based among same-named
  siblings (matches Nokogiri).

### Phase 4 — DocumentFragment (PR: `feat/parse-fragment`)

- `taurus_parse_fragment(const char* xml, size_t len, TaurusDocument dest_doc, TaurusStatus* st)`
  → detached `TaurusElement` synthetic root (named
  `#document-fragment`) holding parsed children.
- Supports multiple top-level elements (wrap source in
  `<__frag__>...</__frag__>` internally, parse, return the wrapper).
- Caller moves children via existing
  `taurus_element_append_child` (which now unlinks, see #217).

### Phase 5 — Custom XPath function handlers (PR: `feat/xpath-custom-fn`)

- `taurus_xpath_register_function(TaurusDocument doc, const char* name, TaurusXPathFn fn, void* user_data)`
- `typedef int (*TaurusXPathFn)(TaurusXPathContext* ctx, int argc, TaurusXPathValue* argv, TaurusXPathValue* out, void* user_data)`
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
