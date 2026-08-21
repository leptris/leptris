# TODO 191: CLI output on public API only + public attribute iteration

**Priority**: P2 (architecture conformance)
**Status**: Design
**Effort**: M

## Problem

`LEPTRIS_BUILD_SHARED=ON` previously failed to link the CLI (and
the test tree): `cli/output.c` walks the tree through INTERNAL
accessors that are not exported from the shared library. Interim
fix shipped (2026-08-16): CLI and specs link `leptris_static` when
both library types are built (cli/CMakeLists.txt,
test/CMakeLists.txt). This TODO is the real fix.

CLAUDE.md / cli/CLI_ARCHITECTURE.md contract: "The CLI never
touches XML structures directly — it always goes through the
public API." output.c currently violates it via nine internal
symbols (verified against `nm -gU` of the shared library):

- `leptris_element_get_name` (public: `leptris_element_name`)
- `leptris_element_get_text_content` (public: `leptris_element_text`,
  NOTE different ownership: internal returns malloc'd copy)
- `leptris_node_get_next_sibling` (public: `leptris_node_next_sibling`)
- `leptris_document_ensure_promoted` (parse already promotes)
- `leptris_compact_int32_decode` / `leptris_compact_ptr16_decode`
  (edge decoding — public navigation functions cover this)
- `leptris_element_get_first_attribute` + `struct leptris_attribute`
  walks (`attr_cname` / `attr_cvalue` / `leptris_attr_next`)
- `leptris_element_get_namespace_uri` / `leptris_element_get_prefix`
  (public: `leptris_element_namespace*` family)

## Public API gap found

The public API has NO attribute iteration — only by-name lookup
(`leptris_element_attribute`) and a count. Serialization from any
binding therefore cannot enumerate attributes (the Ruby and
Python bindings have the same hole). Proposal (additive, no ABI
break):

```c
typedef struct leptris_attribute* LeptrisAttributeRef; /* opaque */

LeptrisAttributeRef leptris_element_first_attribute(LeptrisElement elem);
LeptrisAttributeRef leptris_attribute_next(LeptrisAttributeRef attr);
const char* leptris_attribute_name(LeptrisAttributeRef attr);
const char* leptris_attribute_value(LeptrisAttributeRef attr);
```

(`LeptrisAttributeRef` may need `LEPTRIS_NODE_TYPE_ATTRIBUTE = 6`
interop for XPath attribute results.)

## Plan

1. Add the four attribute-iteration functions to the public API +
   specs (test/dom or test/abi).
2. Refactor `cli/output.c` (xml/json/text formatters) onto public
   calls only; delete the `leptris_internal.h` include.
3. Flip cli/CMakeLists.txt + test/CMakeLists.txt back to the
   plain `leptris` alias; keep a CI shared-build job so this cannot
   regress (suggested: `LEPTRIS_BUILD_SHARED=ON` in one matrix leg).
4. Minor: README Quick start references `fixtures/basic.xml`,
   which does not exist in the repo — add the fixture or fix the
   path.

## Notes

- Namespace printing also reads `struct leptris_namespace` fields
  directly; public `leptris_element_namespace_decl_prefix/uri` +
  `leptris_element_namespace_count` are exported and cover it.
- Discovered while wiring the Python binding's shared-library
  workflow (TODO 82, PR #372).
