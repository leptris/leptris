# Changelog

All notable changes to Taurus will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-08-06

First tagged release.

### Fixed

- All memory leaks across the test suite (was 43 leaks on basic.xml, now 0).
- Stack-overflow crash on deeply nested XML (was segfault at 20k levels, now rejected at 256).
- Memory pool oversized-allocation leak (was leaking allocations larger than page size).
- Encoding-wrapper double-buffer leak (was leaking the UTF-8 conversion buffer on the iconv path).
- DTD subsystem leak (was leaking 128 KB per DOCTYPE-bearing document).
- Pool linked-list corruption that orphaned the pre-allocated second page.
- Serializer buffer-overflow on realloc failure and size_t wrap.
- ASAN crash in `parser_create_writable` — `dtd` and `has_namespace_prefixes` fields were uninitialized; ASAN's malloc-fill made `p->dtd` look non-NULL and crashed in `ttdtd_lookup_entity`.
- SAX namespace-tracking leak — `ns_prefixes` was only freed when `end_prefix_mapping` was registered; restructuring to re-iterate `attrs` at cleanup eliminates both the leak and the per-prefix allocations.

### Added

- `taurus_document_set_strict` / `taurus_document_get_strict` — per-document strict mode.
- `taurus_set_max_depth` / `taurus_get_max_depth` — configurable parser depth limit.
- `taurus_element_as_node` — element-to-node cast helper.
- `TAURUS_ENABLE_ASAN` CMake option — AddressSanitizer build.
- `TAURUS_ENABLE_FUZZING` CMake option — libFuzzer harness.
- `TAURUS_BUILD_DOCS` CMake option — Doxygen API reference.
- Node vtable registry — adding a node type is now purely additive (no switch to edit).
- Hash table dynamic growth past 75% load factor.
- Pool oversized-allocation tracking via side list.
- 105 specs across 14 modules (smoke, parser, encoding, dom, vtable, compact, memory, xpath, serializer, c14n, perf, sax, cli, abi).
- CI: ASAN + leak check on every PR; fuzzing nightly.
- vcpkg overlay port under `ports/taurus/`.
- ABI-stability guards: `_Static_assert` on opaque handle sizes; `TAURUS_FOR_BINDGEN` macro for FFI generators.

### Changed

- Every node allocation routes through the document pool — single ownership model.
- Attribute values bypass string interning (3.4x perf improvement on attrs.xml; now 1.3x faster than libxml2).
- `taurus_parse_string_with_encoding` frees the intermediate UTF-8 buffer after parse (was overwriting `doc->xml_buffer` and leaking the copy).
- DTD container (`TaurusDTD`) is now pool-allocated; entity declarations pool-allocated.
- All DOM node create functions consolidated to a single pool-routed entry point per type (no more `_create` / `_create_fast` split).
- Magic-number node-type checks replaced with `TAURUS_NODE_TYPE_*` enum constants.
- Single source of truth for internal typedefs (`common/types_internal.h`).
- `SerializeBuffer` struct tagged for forward-declaration compatibility.

### Removed

- Dead `taurus_node_create` (non-pool variant) — pool owns all node lifetime.
- Dead `taurus_element_add_namespace` static.
- Legacy `_create_fast` wrappers per node type.
- 50+ compile warnings (now zero).
- Stray 0-byte `src/taurus/dom/compact_allocator.c`.
- `gtest` from `vcpkg.json` (tests use CMake FetchContent).

## [0.1.0] - Pre-release baseline

Initial development snapshot, never formally tagged.

### Added

**XML Parsing**
- Full XML 1.0 parsing support
- Well-formed XML validation
- Character encoding support (UTF-8)
- Document structure preservation

**DOM (Document Object Model)**
- Complete DOM implementation
- Element navigation and manipulation
- Attribute access and modification
- Text, Comment, CDATA, and Processing Instruction nodes
- Mixed content support
- Node iteration API (`TaurusNodeRef`)

**XPath 1.0**
- Complete XPath 1.0 engine
- 13 XPath axes (ancestor, descendant, following, etc.)
- 15 XPath operators
- 27 XPath functions (string, numeric, node-set, boolean)
- Namespace-aware XPath queries

**XML Serialization**
- Document and element serialization
- Pretty-printing with configurable indentation
- Namespace declaration serialization
- Correct entity reference handling per XML 1.0 spec
- Character-perfect output preservation

**SAX (Simple API for XML)**
- Event-driven XML parsing
- 8 callback types for comprehensive XML processing
- Zero DOM construction overhead

**DTD Validation**
- DTD parsing and validation
- ELEMENT and ATTLIST declarations
- Required attribute checking
- Content model validation

**CLI Tool**
- `taurus parse` - Parse and display XML structure
- `taurus xpath` - Execute XPath queries
- `taurus format` - Format and pretty-print XML
- `taurus validate` - Validate against DTD
- `taurus version` - Display version information

**Features**
- Memory pool allocator for O(1) allocations
- Zero-copy parsing with StringView
- Compact element structure for performance
- Fast attribute lookup with hash table

### Performance
- XPath evaluation: 5.91x faster than libxml2
- DOM operations: competitive with pugixml
- Memory-efficient: pool allocation reduces overhead

### Testing
- 100% test pass rate (55/55 tests)
- W3C XPath conformance: 438/438 tests passing
- Comprehensive test suite covering all features

### Documentation
- Complete README.adoc with usage examples
- API reference for all public functions
- SAX, DTD, and XPath guides
- Mixed content handling documentation

### Platforms
- Linux (x86_64, ARM64)
- macOS (x86_64, ARM64/Apple Silicon)
- Windows (MSVC compatible)

### Dependencies
- No required external dependencies for basic functionality
- Optional: iconv for encoding conversion
- Optional: utf8proc for Unicode normalization
