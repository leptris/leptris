# TODO 111 — remaining work survey (post-session update)

**Priority**: P2 (registry of follow-up work)
**Status**: living document — update as items land

## What's done in this session

### Compact-pointer migration (TODO 90) — COMPLETE Phases 1, 2a, 2b, 2c, 2d
- PR #85: Phase 1 — children_array cache removal. 120→112 B.
- PR #86: Phase 2a — field reorder. 112→104 B.
- PR #90: Phase 2b — element tree pointers as int32_t offsets. 104→88 B.
- PR #91: Phase 2c — non-element node siblings as int32_t. -4 B/node.
- PR #92: Phase 2d — element attribute pointers as int32_t. 88→80 B.
- Phase 2e: attempted but abandoned. `document` is malloc'd outside
  the pool — its address can be GBs from elements (doesn't fit
  int32_t). Compressing `namespaces` alone saves nothing because
  alignment padding eats the 4-byte gain.

**Final element size: 192 → 80 bytes (58% reduction).**

### XPath engine
- PR #87: TODO 69 — exhaustive XPath 1.0 conformance suite (54 specs).
- PR #88: TODO 110 — xpath `substring()` W3C spec compliance.
- PR #95: TODO 109 — XPath over non-element nodes (`//comment()`,
  `//processing-instruction()`, `node()` now traverse correctly).

### DTD validator (TODO 91)
- PR #98: Phase 8 — ENTITY/ENTITIES attribute validation.
- PR #99: Phase 8d — conditional sections (INCLUDE/IGNORE) with nesting.
- PR #100: Phase 8b — parameter entity substitution (`<!ENTITY % pe "...">` + `%pe;`).

### XInclude (TODO 92)
- PR #101: Phase 4 — xpointer fragment selection via XPath.

### Parser bugs
- TODO 112: `<?xml-stylesheet?>` was dropped because `<?xml` prefix match
  treated it as the XML declaration.

### ABI / visibility
- PR #89: TODO 80 — symbol visibility (`-fvisibility=hidden`).
  Shared lib drops from 445 entries (295 taurus_* + 150 leaked
  internal helpers) to 118 taurus_* only.

### Specs
- PR #93: Compact-pointer round-trip specs (locks in Phase 2 encoding).
- PR #94: Perf budget bump for offset-decoding overhead.
- PR #96: Serialize round-trip + namespaced attrs + xml-decl specs.
- PR #97: Integrated parse-walk-mutate-serialize cycle specs.
- PR #102: C14N namespaces + document order + 1.1 specs (TODO 85).

### Other
- TODO 88: freeze API contract clarification (already complete).
- TODO 104: serialize round-trip and C14N spec coverage filled in.

## Remaining work — major features

Each of these is a multi-day to multi-week effort. They warrant
their own focused sessions.

### TODO 91 Phase 8c — choice-model backtracking on ambiguous content
The current `match_seq` in `content_check.c` uses recursive descent.
For ambiguous content models like `(a, a?)*`, recursive descent can
be exponential on adversarial inputs. Real implementation needs an
NFA or compiled state machine with memoization. Estimated 1-2 weeks.

### TODO 89 — incremental SAX parsing
`taurus_sax_parser_feed` currently buffers all chunks until `is_final`
is set, then parses in one shot. True incremental parsing requires
the parser to be resumable at every `sax_advance`. Estimated multi-
week refactor of `src/taurus/sax/parser.c`.

### TODOs 79-85 — FFI bindings (Ruby, Python, Rust)
Each language is a multi-week project (Ruby-ffi, Python-cffi, Rust-
bindgen). Design docs exist in `TODO.fix/81-ruby-binding.md` etc.
Start with Ruby (the user's primary language).

## Remaining work — minor items

### TODO 92 Phase 5 — base URL resolution + recursion guard
The XInclude processor doesn't currently track or limit recursion
depth. Two documents that xi:include each other cause infinite
recursion. Add a depth limit (e.g., 10) and base-URL resolution
for relative hrefs.

### TODO 104 — additional spec coverage
Specific gaps in test_encoding.cpp (UTF-16 BOM round-trip, iconv
ISO-8859-1 path), test_vtable.cpp (serialization via vtable on
complex trees). Each is a few specs.

### TODO 85 — exclusive C14N
The current C14N supports 1.0 and 1.1, but exclusive canonicalization
(used in XML Signature) is missing. Adds the `InclusiveNamespaces`
parameter that controls which namespace declarations appear in output.

## Verification

After the session:

```bash
cmake --build build
ctest --test-dir build -j4   # 305/307 pass; 2 pre-existing flaky CLI
                              # tests that pass when run serially.
leaks --atExit -- ./build/test/test_dom   # 0 leaks
cc -I src/include -o /tmp/sz /tmp/sz.c build/src/libtaurus.a ...
# sizeof(struct taurus_element) = 80
```

## Architecture status

- Element struct: 80 bytes (was 192 — 58% reduction).
- 0 leaked internal symbols in shared library.
- All 13 XPath axes, all 27 XPath functions, all operators have specs.
- DTD validator: Phases 1-7 + ENTITY + parameter entities + conditional
  sections shipped. Choice-model backtracking is the remaining gap.
- XInclude: parse="text", parse="xml", xi:fallback, xpointer shipped.
  Base URL + recursion guard pending.
- C14N: 1.0 and 1.1 work; exclusive canonicalization pending.
