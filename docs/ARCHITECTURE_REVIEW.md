# Leptris Architecture Review

**Status**: Snapshot as of 2026-08-03
**Scope**: Post-phase-4 refactor, post-ASAN fixes

## Overview

Leptris is a pure-C99 XML 1.0 parser, XPath 1.0 engine, and SAX parser.
The codebase is structured as a layered library with strict separation
between public API, internal types, and per-subsystem implementations.

```
cli/                  Command-line interface (depends only on public API)
src/include/leptris/   Public API surface (opaque handles, stable ABI)
src/leptris/           Internal implementation
  dom/                DOM node tree
  parse/              XML parser
  xpath/              XPath lexer/parser/evaluator
  sax/                Event-driven parser
  dtd/                DTD parser + entity resolver
  serialize/          XML serializer + C14N
  encoding/           UTF-16/iconv wrappers
  memory/             Pool allocator + hash table
  common/             StringView + entity decoding
  unicode/            utf8proc wrappers (optional)
```

## Architectural invariants

These are the load-bearing rules. Breaking any of them is a bug.

### 1. Pool-ownership of DOM nodes

Every byte reachable from a `LeptrisDocument` is allocated from that
document's pool. `leptris_document_free` destroys the pool, freeing
everything. There is no reference counting; no per-node free.

**Implication**: A `LeptrisElement` pointer is valid only as long as
its owning document. Use-after-free is impossible *within* a document;
only document-level lifetime matters.

### 2. Layered architecture

The CLI never touches internals. The public API never exposes internal
types. Core never reaches up to the CLI.

```
CLI → public API → core subsystems
```

A `grep` for `leptris_internal.h` outside `src/leptris/` and `test/`
should return nothing.

### 3. Opaque handles

All public types (`LeptrisDocument`, `LeptrisElement`, `LeptrisNodeRef`,
`LeptrisXPathResult`, `LeptrisNamespace`, ...) are pointer typedefs to
incomplete struct types. Callers never see struct fields.

`_Static_assert` in `test/abi/test_header_hygiene.cpp` verifies all
opaque handles are pointer-sized — a baseline ABI stability check.

### 4. Vtable dispatch for node types

Adding a node type is purely additive: register a new entry in the
`g_node_vtables[]` array in `dom/node_vtable.c`. The serializer
(`serialize/serialize.c`) and any other type-dispatched code picks it
up automatically through `leptris_node_vtable_for(type)`.

No `switch (node->type)` statements should be added to dispatch code.

### 5. Document-scoped configuration

`LeptrisDocument` carries its own strict-mode flag and allocator hooks,
inherited from the thread-default at creation. Two documents in the
same thread can have different settings.

## Module-level findings

### `dom/` — node tree

**Status**: Solid.

- Element struct is ~96 bytes via compact architecture (compressed
  pointers + pool offsets).
- All node types begin with `LeptrisNode` header → safe casts.
- `leptris_element_add_namespace_inplace` (element.c:549) is dead code
  with a misleading TODO. The real namespace tracking path
  (`leptris_element_add_namespace` in leptris_memory.c) works correctly.
  Recommend removing the dead function in a future cleanup PR.

### `parse/` — XML parser

**Status**: Solid.

- `parser_new.c` is the active parser. `compact_parser.c` is a
  parallel implementation that is NOT wired into the build's call
  graph (no entry points called from `leptris_parse`). Either wire it
  in (it's an alternative code path for compact-mode documents) or
  document that it's experimental.
- Depth limit (`LEPTRIS_MAX_ELEMENT_DEPTH=256`) is configurable via
  `leptris_set_max_depth`.
- All node-creation sites route through the pool.

### `xpath/` — XPath engine

**Status**: Solid.

- 438/438 W3C XPath 1.0 conformance tests pass.
- 27 functions, 13 axes, all operators.
- Variable support covers boolean/number/string/node-set (the
  node-set case landed in PR #29 / TODO 86).
- The Linux-only XPath variable crashes (TODO 94) and DTD parser
  crashes (TODO 95) were root-caused and fixed in PR #37: the
  project compiled with strict C99 (`C_EXTENSIONS OFF`) which made
  `strdup()` implicitly declared as `int`, truncating 64-bit
  pointers on Linux x86_64. Fixed by adding
  `_POSIX_C_SOURCE=200809L` project-wide.

### `sax/` — event-driven parser

**Status**: Solid after PR #23 fix.

- `sax_parse_element` is the workhorse. Handles self-closing and
  normal-close tags.
- Namespace prefix events (`start_prefix_mapping` /
  `end_prefix_mapping`) iterate `attrs` directly — no per-prefix
  allocations (regression fix from PR #23).
- Incremental parsing (`leptris_sax_parser_feed`) is currently
  one-shot under the hood (TODO 89). True streaming remains a
  feature gap.

### `dtd/` — DTD support

**Status**: Phase 1 + Phase 2 implemented (PRs #34, #38, #39).

- `parser.c`, `model.c`, `resolver.c` are wired in and work.
- `validator.c` (PR #34) is compiled. The validation engine has
  Phase 1 (EMPTY content model check, PR #38) and Phase 2 (ATTLIST
  parsing + #REQUIRED enforcement, PR #39).
- Entity resolution works via the document's pool (post-PR-#23 fix
  that routed DTD through the document pool rather than a private
  one).
- `leptris_dtd_parse` now correctly releases its standalone pool
  via `owns_pool` flag on `LeptrisDTD` (PR #37).
- ATTLIST parsing was unblocked by the `_POSIX_C_SOURCE` define
  (PR #37) — without it, strdup linkage was broken on Linux.

What's still pending (TODO 91 Phase 3+):
- General #REQUIRED iteration (needs hash-table iterator API).
- Element-content grammar matcher `(a, b+, c?)`.
- Attribute type validation (`ID`, `IDREF`, `NMTOKEN`, enumerated).
- `ENTITY`-typed attribute resolution.

### `serialize/` — XML output

**Status**: Solid.

- `serialize.c` dispatches via vtable (no switch).
- `c14n.c` extracted in PR #30 — owns C14N 1.0 canonicalization.
- Buffer growth is overflow-safe with `alloc_failed` flag.

### `encoding/` — encoding conversion

**Status**: Solid.

- UTF-16 BOM detection always compiled.
- iconv path optional via `LEPTRIS_ENABLE_ICONV`.
- `wrapper.c` extracted in TODO 73 — owns the encoding-detection
  parse path. Fixed a double-buffer leak during extraction.

### `memory/` — pool allocator

**Status**: Solid.

- Pool uses chained pages; existing allocations don't move on growth.
- Oversized allocations tracked via `LeptrisBigAlloc` side-list,
  freed in `leptris_pool_destroy`.
- Hash table grows at 75% load factor.

## Test coverage

**105 specs across 14 modules**, all passing under both regular and
ASAN builds:

| Module | Spec count |
|--------|-----------|
| smoke | 3 |
| parser | 11 |
| encoding | 4 |
| dom | varies |
| vtable | varies |
| compact | varies |
| memory (pool) | varies |
| xpath | 11 |
| serializer | varies |
| c14n | varies |
| perf | varies |
| sax | 13 |
| abi (header hygiene) | 6 |
| cli | 12 |

Each spec module is one Google Test binary. Discovery is
`DISCOVERY_MODE PRE_TEST` so ctest re-discovers on every run.

**Coverage gaps**:

- XPath variables — specs backed out pending Linux ASAN investigation
  (TODO 94).
- C14N 1.1 and Exclusive XML Canonicalization — TODO 85.
- DTD validation — TODO 91 (validator not implemented).

## CI

Twelve workflows, all currently green on `main`:

- `test.yml` — Linux + macOS build + tests
- `build.yml` — Linux + macOS CLI smoke
- `asan.yml` — Linux ASAN, macOS leaks
- `checks.yml` — trailing whitespace, clang-format (informational),
  cppcheck (informational)
- `benchmark.yml` — Linux + macOS perf
- `fuzz-nightly.yml` — libFuzzer nightly
- `release.yml` — manual version-bump-and-tag
- CodeQL

## Public API stability

- Opaque handles verified pointer-sized via `_Static_assert`.
- `LEPTRIS_FOR_BINDGEN` macro strips platform-specific attributes for
  binding generators.
- SOVERSION tracked on shared library.
- `leptris.h` umbrella header includes everything; subsystem headers
  (`leptris/dom/document.h`, `leptris/xpath/xpath.h`, etc.) allow
  finer-grained inclusion.

See `docs/FFI.md` for the FFI contract.

## Known debt (in priority order)

1. **TODO 94** — Linux ASAN heap-state bug exposed by XPath variable
   specs. Real bug, needs Linux reproduction.
2. **TODO 91** — DTD validator not implemented. Big feature gap.
3. **TODO 69** — W3C XPath conformance suite not integrated.
4. **TODO 92** — XInclude classifier helpers shipped (PR #33) but
   the full `leptris_xinclude_process` is a stub returning
   `LEPTRIS_ERROR_NOT_IMPLEMENTED`.
5. **TODO 89** — SAX incremental parsing is one-shot under the hood.
6. **TODO 79-85** — FFI design done; Ruby/Python/Rust bindings not
   implemented.

## What went well (recent work)

- ASAN crashes fixed by initializing `Parser.dtd` and
  `has_namespace_prefixes` in `parser_create_writable`.
- SAX leak fixed by eliminating per-prefix allocations entirely.
- leptris.c split from 2646 lines to 900 lines across 4 phases.
- XPath nodeset variable support implemented (TODO 86).
- Man page version drift corrected.
- Docs refresh (building.md, CHANGELOG).
- C14N extracted to focused module.

## Recommendations for next priorities

1. Reproduce and fix TODO 94 (Linux ASAN). All other XPath work
   depends on having a reliable test environment.
2. Decide whether to wire in `compact_parser.c` or remove it. Dead
   code is misleading.
3. Start DTD validator (TODO 91) — biggest user-facing feature gap.
4. Add C14N 1.1 and Exclusive XML Canonicalization (TODO 85) once
   the foundation is solid.

## Architecture decisions to revisit (low priority)

- The `xml_buffer_needs_free` flag (TODO 41) was investigated and is
  the correct design for the `leptris_parse_string_inplace` API
  contract. Don't change.
- The duplicate `APPEND_STRING` macro in `leptris.c` and `c14n.c` is
  intentional — making it a function would add overhead. Marked
  clearly in comments.
