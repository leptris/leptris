# TODO 138 — Clean FFI API for Nokogiri-compatible Ruby binding

## Problem

The Ruby FFI team reported that the current C public API is "too
minimal" for a Nokogiri-style binding. Several functions that the
binding needs are either:
- Implemented internally but not declared in the public header
- Not exported with LEPTRIS_API
- Missing entirely

The user specified the exact required surface:

```
Parse (leptris_parse), document_root/document_encoding, element-only
traversal via indexed children (element_child(elem, i),
element_child_count), attributes by name and by index, namespaces,
XPath eval (doc + doc-with-context), XPath result type/boolean/
number/string/nodeset-size/nodeset-get, error helpers. No siblings,
no node types, no mutation, no SAX, no serialization, no
free_string.
```

## What exists today

| Function | Status |
|---|---|
| `leptris_parse_string` | ✓ in public header, LEPTRIS_API |
| `leptris_document_root` | ✓ in public header, LEPTRIS_API |
| `leptris_document_encoding` | ✓ implemented, LEPTRIS_API in core.c, but **NOT in public header** |
| `leptris_element_child` | ✓ in public header, LEPTRIS_API |
| `leptris_element_child_count` | ✓ in public header (returns size_t), LEPTRIS_API |
| `leptris_element_attribute` | ✓ in public header, LEPTRIS_API (by name) |
| `leptris_element_attribute_count` | ✓ exists in internal dom/element.h (returns uint8_t), **NOT exported** |
| Attribute index access | ✗ MISSING |
| Namespace count | ✗ MISSING |
| `leptris_element_namespace` | ✓ returns first namespace |
| `leptris_namespace_uri` | ✓ in public header, LEPTRIS_API |
| `leptris_namespace_prefix` | ✓ in public header, LEPTRIS_API |
| `leptris_xpath_eval` | ✓ in public header, LEPTRIS_API |
| `leptris_xpath_result_type` | ✓ in public header, LEPTRIS_API |
| `leptris_xpath_result_count` | ✓ in public header, LEPTRIS_API |
| `leptris_xpath_result_get` | ✓ in public header, LEPTRIS_API |
| `leptris_xpath_result_boolean` | ✓ in public header, LEPTRIS_API |
| `leptris_xpath_result_number` | ✓ in public header, LEPTRIS_API |
| `leptris_xpath_result_string` | ✓ in public header, LEPTRIS_API |
| `leptris_xpath_result_free` | ✓ in public header, LEPTRIS_API |
| `leptris_status_string` | ✗ MISSING |

## What to add

### 1. `leptris_document_encoding` declaration
Already implemented. Just add the declaration in the public header.

### 2. Export `leptris_element_attribute_count` with proper signature

Currently in `dom/element.h` (internal) as `uint8_t` (max 255 attrs per
element, but conceptually should be `size_t` for FFI). Need to:
- Move declaration to public header `leptris.h`
- Change return type to `size_t` (uint8_t limits to 255 attrs)
- Apply LEPTRIS_API

### 3. New: `leptris_element_attribute_name_at(elem, index)`

```c
LEPTRIS_API const char* leptris_element_attribute_name_at(
    LeptrisElement elem, size_t index);
```

Returns the name of the i-th attribute. Walks the attribute linked
list. O(n) per call, same as libxml2's `xmlGetAttrName`.

Memory: String is owned by the element. Do not free.

### 4. New: `leptris_element_attribute_value_at(elem, index)`

```c
LEPTRIS_API const char* leptris_element_attribute_value_at(
    LeptrisElement elem, size_t index);
```

Returns the value of the i-th attribute. Walks the attribute list
in parallel with name_at (two walks would be wasteful — better to
walk once and return both via a struct or pair of out-params).

For FFI ergonomics, two separate functions are simpler than
out-params or structs. The cost of two walks is negligible for the
typical element (avg ~3 attributes).

### 5. New: `leptris_element_namespace_count(elem)`

```c
LEPTRIS_API size_t leptris_element_namespace_count(LeptrisElement elem);
```

Returns the number of namespaces declared on this element. O(n) over
the attribute list looking for xmlns:*.

### 6. New: `leptris_status_string(status)`

```c
LEPTRIS_API const char* leptris_status_string(LeptrisStatus status);
```

Returns a human-readable string for a status code. Used for error
messages. The Ruby FFI side wraps this for `Leptris::Error.new(code)`.

## Design principles

- **O(n) is fine.** None of these need to be fast. Nokogiri's
  `xmlGetAttrName`, `xmlGetNsProp`, `xmlGetNsList` are all O(n).
  The Ruby side caches. The element_index from TODO 132 is for
  XPath, not for DOM traversal.

- **No new state.** All these are read-only operations on the
  existing tree. No tracking required.

- **String ownership.** All returned strings are document-owned
  (live until `leptris_document_free`). No `free_string` required.
  This matches the user's request: "no free_string".

- **Const correctness.** All string returns are `const char*`.
  Element returns are `LeptrisElement` (a typedef for a pointer, not
  a struct, so const doesn't apply).

- **No new files.** Add declarations to the existing `leptris.h`
  umbrella header. Add implementations to existing files
  (`src/leptris/core.c` for document, `src/leptris/dom/element.c`
  for element, `src/leptris/error.c` for status string).

## Layout in leptris.h

```c
/* ============================================================================
 * FFI Helpers (Nokogiri-compatible binding surface)
 *
 * The functions in this section are designed for FFI consumption by the
 * Ruby (and other language) bindings. They follow Nokogiri conventions:
 *   - O(n) operations are acceptable; the binding layer caches.
 *   - Returned strings are document-owned. No free_string required.
 *   - No state tracking or callbacks.
 *
 * If you are writing C code that calls into libleptris, prefer the
 * higher-level functions earlier in this header.
 * ============================================================================ */

LEPTRIS_API const char* leptris_document_encoding(LeptrisDocument doc);

LEPTRIS_API size_t leptris_element_attribute_count(LeptrisElement elem);
LEPTRIS_API const char* leptris_element_attribute_name_at(
    LeptrisElement elem, size_t index);
LEPTRIS_API const char* leptris_element_attribute_value_at(
    LeptrisElement elem, size_t index);

LEPTRIS_API size_t leptris_element_namespace_count(LeptrisElement elem);

LEPTRIS_API const char* leptris_status_string(LeptrisStatus status);
```

## Specs

Add a new test in `test/abi/test_header_hygiene.cpp` that:
- For each new function, parse the header and confirm the declaration
  exists with LEPTRIS_API
- For each, link against the library and call it (round-trip test)
- Round-trip: `parse(<root attr1="v1"/>); doc.attributes[0].name == "attr1"`

## Branch
`todo-138-clean-ffi-api`
