# TODO 191: CLI output on public API only + public attribute iteration

**Priority**: P2 (architecture conformance)
**Status**: Design
**Effort**: M

## Problem

`TAURUS_BUILD_SHARED=ON` previously failed to link the CLI (and
the test tree): `cli/output.c` walks the tree through INTERNAL
accessors that are not exported from the shared library. Interim
fix shipped (2026-08-16): CLI and specs link `taurus_static` when
both library types are built (cli/CMakeLists.txt,
test/CMakeLists.txt). This TODO is the real fix.

CLAUDE.md / cli/CLI_ARCHITECTURE.md contract: "The CLI never
touches XML structures directly — it always goes through the
public API." output.c currently violates it via nine internal
symbols (verified against `nm -gU` of the shared library):

- `taurus_element_get_name` (public: `taurus_element_name`)
- `taurus_element_get_text_content` (public: `taurus_element_text`,
  NOTE different ownership: internal returns malloc'd copy)
- `taurus_node_get_next_sibling` (public: `taurus_node_next_sibling`)
- `taurus_document_ensure_promoted` (parse already promotes)
- `taurus_compact_int32_decode` / `taurus_compact_ptr16_decode`
  (edge decoding — public navigation functions cover this)
- `taurus_element_get_first_attribute` + `struct taurus_attribute`
  walks (`attr_cname` / `attr_cvalue` / `taurus_attr_next`)
- `taurus_element_get_namespace_uri` / `taurus_element_get_prefix`
  (public: `taurus_element_namespace*` family)

## Public API gap found

The public API has NO attribute iteration — only by-name lookup
(`taurus_element_attribute`) and a count. Serialization from any
binding therefore cannot enumerate attributes (the Ruby and
Python bindings have the same hole). Proposal (additive, no ABI
break):

```c
typedef struct taurus_attribute* TaurusAttributeRef; /* opaque */

TaurusAttributeRef taurus_element_first_attribute(TaurusElement elem);
TaurusAttributeRef taurus_attribute_next(TaurusAttributeRef attr);
const char* taurus_attribute_name(TaurusAttributeRef attr);
const char* taurus_attribute_value(TaurusAttributeRef attr);
```

(`TaurusAttributeRef` may need `TAURUS_NODE_TYPE_ATTRIBUTE = 6`
interop for XPath attribute results.)

## Plan

1. Add the four attribute-iteration functions to the public API +
   specs (test/dom or test/abi).
2. Refactor `cli/output.c` (xml/json/text formatters) onto public
   calls only; delete the `taurus_internal.h` include.
3. Flip cli/CMakeLists.txt + test/CMakeLists.txt back to the
   plain `taurus` alias; keep a CI shared-build job so this cannot
   regress (suggested: `TAURUS_BUILD_SHARED=ON` in one matrix leg).
4. Minor: README Quick start references `fixtures/basic.xml`,
   which does not exist in the repo — add the fixture or fix the
   path.

## Notes

- Namespace printing also reads `struct taurus_namespace` fields
  directly; public `taurus_element_namespace_decl_prefix/uri` +
  `taurus_element_namespace_count` are exported and cover it.
- Discovered while wiring the Python binding's shared-library
  workflow (TODO 82, PR #372).
