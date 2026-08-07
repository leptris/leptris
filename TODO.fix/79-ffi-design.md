# TODO 79: FFI design — principles and ABI contract

**Priority**: P1 (architecture — enables bindings to every language)
**Status**: Design only
**Effort**: L (multi-phase)

## What "FFI" means here

A Foreign Function Interface is the boundary between libtaurus (C99)
and code in other languages (Ruby, Python, Rust, etc.).  Every
language with a C ABI can technically call our functions, but a good
FFI is more than "C functions exist":

1. **Stable ABI** — callers compiled against version N work with N+1.
2. **Opaque handles** — callers never see struct fields; only typed
   pointers.
3. **Clear ownership** — every function documents who frees what.
4. **Explicit error reporting** — no exceptions across the boundary.
5. **No C-isms** — no macros, no varargs, no `size_t` ambiguity.

## Current state

The public API in `src/include/taurus/` already does most of this:

- All handles are opaque typedefs (`TaurusDocument`, `TaurusElement`,
  `TaurusNodeRef`, `TaurusXPathResult`).
- Status is via `TaurusStatus*` output parameter.
- Memory ownership is documented per-function ("Memory:" comments).
- Strings returned are either document-owned (do not free) or
  caller-owned (free with `taurus_free_string`).

What's missing:

1. **Symbol versioning** — no `.so.N` versioning on Linux.
2. **No ABI stability commitment** — function signatures could change.
3. **No FFI shim** — the raw API has C-specific patterns (output
   params, multiple return values) that are awkward in bindings.
4. **No tests for ABI stability** — adding a struct field would
   silently break bindings.

## Fix (phased)

### Phase 1: ABI audit + documentation

Walk every public header.  For each function:

- Verify signature is stable-worthy (no `int` for boolean, no `char*`
  where `const char*` should be).
- Document ownership contract.
- Tag with `@since X.Y.Z`.

Add an `ABI.md` document describing what's stable.

### Phase 2: symbol versioning

Use CMake's `set_target_properties(... VERSION X.Y.Z SOVERSION X)`
on `taurus_shared` (already there).  On Linux, link with
`-Wl,--version-script,taurus.map` to control which symbols are
exported.

### Phase 3: FFI-friendly shim (optional)

A second header `taurus/ffi.h` that wraps common patterns:

- `taurus_parse_string_with_status(xml, len, status*)` — already
  exists; document.
- `taurus_xpath_eval_returning_string(doc, expr, out*)` — wraps the
  nodeset→string coercion that's currently 2 calls.
- Iterators: `taurus_element_first_child` + `taurus_node_next_sibling`
  pattern (already exists; document).

Most languages' FFI can use the existing API directly; the shim is
for languages that struggle with output params (Rust without `unsafe`,
some scripting languages).

### Phase 4: bindings

See TODOs 81-84 for specific bindings.

## Tests

- ABI stability test: a script that captures the current symbol table
  and asserts against a baseline.  Catches accidental ABI breaks.
- FFI smoke test in each bound language.

## Architecture notes

A good FFI is the difference between "this library is C-only" and
"this library is the canonical XML parser for any language."  The
pool-ownership model (TODOs 05/15/25/33) is already FFI-friendly:
callers don't have to manage individual allocations — `document_free`
releases everything.

The remaining work is mostly documentation and a few ergonomic
wrappers, not architectural change.
