# TODO 104 — remaining spec coverage gaps

**Priority**: P2 (defense in depth — every public API gets a spec)
**Status**: open

## What's well-covered (>= 10 specs)

- `test/dom/test_dom.cpp` — 29 specs (after TODO 51, 55 additions)
- `test/xpath/test_xpath.cpp` — 23 specs
- `test/dtd/test_dtd_validate.cpp` — 23 specs
- `test/sax/test_sax.cpp` — 24 specs (after TODO 60 additions)
- `test/parser/test_parser.cpp` — 14 specs
- `test/memory/test_pool.cpp` — 13 specs
- `test/cli/test_cli.cpp` — 12 specs

## What's thin (< 10 specs)

### `test/parser/test_encoding.cpp` — 10 specs

Encoding edge cases that aren't covered:

- UTF-8 BOM detection (with and without XML declaration)
- UTF-16LE / UTF-16BE round-trip
- UTF-16 with BOM vs without
- ISO-8859-1 (iconv path)
- EBCDIC heuristic detection
- Mislabeled encoding (claims UTF-16 but is UTF-8)
- Empty input + encoding wrapper
- Mixed encoding in same document (illegal but recoverable)

### `test/dom/test_vtable.cpp` — 8 specs

The vtable dispatch is internal. Specs cover element, text,
comment, CDATA, PI, doctype. Missing: serialization via vtable
on a complex tree; the dispatch-on-type for arbitrary node
handle (e.g., `leptris_node_get_type` returning each type).

### `test/serializer/test_c14n.cpp` — 5 specs

C14N edge cases missing:

- Namespaces in canonical output (default + prefixed)
- XML 1.0 vs 1.1 C14N difference
- Whitespace handling in canonical mode
- Document order for attributes (alphabetical per spec)
- Empty document canonical form

### `test/serializer/test_serialize.cpp` — 4 specs

Serialization edge cases missing:

- Indented output (multi-level pretty-print)
- Encoding="UTF-16" in declaration
- XML declaration omission
- CDATA preservation
- Comment preservation
- Round-trip fidelity (parse → serialize → parse, compare trees)

### `test/abi/test_header_hygiene.cpp` — 6 specs

Public ABI tests. Missing:

- Forward-compatibility: include only `leptris/types.h` and confirm
  only type defs leak (no function decls)
- C++ compatibility (`extern "C"`)
- No internal headers leaked into the public API surface

### `test/perf/test_perf.cpp` — 3 specs

Perf regression tests. Missing:

- Steady-state throughput (sustained 1k parses)
- Cold-start latency (single parse)
- Memory growth under sustained load

## What's missing entirely

- `test/xinclude/test_xinclude.cpp` — exists, 12 specs (good after TODO 54)
- `test/dom/test_compact.cpp` — exists, 3 specs (compact parser is
  not wired in; specs cover what's there)
- `test/smoke/test_smoke.cpp` — 3 specs (smoke tests; fine)

## Plan

For each thin file, add 5-10 specs that exercise the documented
behavior of every public function in the corresponding subsystem.
Pattern follows PR #51 / PR #55 / PR #60 — one spec per public
API function, with happy path + at least one error/NULL path.

## Acceptance

- Every public API function has at least one spec
- Every documented error code path has at least one spec
- macOS `leaks` clean on every test binary
