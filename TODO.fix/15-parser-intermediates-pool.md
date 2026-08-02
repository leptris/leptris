# TODO 15: Route parser intermediate buffers through the pool

**Priority**: P0 (correctness — closes remaining per-parse leaks)
**Status**: Planned
**Effort**: M

## Problem

After TODO 05 phase 1, every node-type create function routes through
the pool — but the parser still allocates **intermediate** content
buffers via raw `TAURUS_ALLOC_N` / `taurus_sv_to_cstr` (calloc), passes
them to the create function (which pool-copies them), and then either
frees them or leaks them. The unfree'd paths are the remaining leak
sources.

`leaks` on `basic.xml` (218 B input) reports 8 leaks / 144 B, all from:

```
parser_parse_text      → malloc(16)        # content buffer
taurus_sv_to_cstr      → malloc(16)        # name / prefix / uri conversion
taurus_decode_entities → malloc(N)         # entity-resolved content
```

Call sites in `src/taurus/parse/parser_new.c`:

| Line | Code | Issue |
|------|------|-------|
| 535 | `name = TAURUS_ALLOC_N(char, len+1)` | attribute name |
| 560 | `value = TAURUS_ALLOC_N(char, len+1)` | attribute value |
| 780, 785 | `content = TAURUS_ALLOC_N(char, len+1)` | text content |
| 946 | `content = TAURUS_ALLOC_N(char, len+1)` | comment content |
| 992 | `content = TAURUS_ALLOC_N(char, len+1)` | CDATA content |
| 1070 | `data = TAURUS_ALLOC_N(char, data_len+1)` | PI data |
| 1213 | `internal_subset = TAURUS_ALLOC_N(char, subset_len+1)` | DOCTYPE subset |
| 1318 | `elem->name = taurus_sv_to_cstr(&elem->name_view)` | element name |
| 1360 | `prefix_str = taurus_sv_to_cstr(&elem->prefix_view)` | namespace prefix |
| 1453, 1486, 1498, 1499 | `uri = taurus_sv_to_cstr(...)` | namespace URIs |
| 1703 | `prefix_cstr = taurus_sv_to_cstr(...)` | resolver walk |

## Root cause

Two layered problems:

1. The parser pre-dates the pool-aware create functions. It allocates
   a buffer, populates it, then hands it to a create function that
   *copies* it into the pool. The intermediate is supposed to be
   freed by the caller, but several paths return early or hand off
   ownership ambiguously.

2. `taurus_sv_to_cstr` always `malloc`s a copy. There is a
   `taurus_sv_to_cstr_pooled` variant (declared in
   `common/string_view.h:41`) but the parser doesn't use it.

## Fix

**Strategy**: every parser-side string allocation goes through
`taurus_pool_*`. Specifically:

- Replace `TAURUS_ALLOC_N(char, len+1)` with
  `taurus_pool_alloc(p->pool, len+1)`.
- Replace `taurus_sv_to_cstr(...)` with
  `taurus_sv_to_cstr_pooled(..., p->pool)` (or for the resolver walk,
  the document's pool, which is the same pool).
- Where the parser builds a buffer that needs to outlive the function
  call (e.g., stored on a node), the buffer IS the pool allocation —
  no separate intermediate.
- Where the parser builds a temporary that's only used during parsing
  (e.g., namespace URI lookup), use a stack buffer or pool-alloc and
  let the pool reclaim at destroy.

`taurus_decode_entities` returns a `malloc`'d buffer; either change
its contract to take a pool, or free its return value immediately
after the create function copies it into the pool. (The latter is
the smaller change.)

## Tests

`test/parser/test_parser.cpp` extends with specs asserting zero leaks
under `leaks --atExit --` for several representative inputs:

```cpp
TEST(ParserLeaks, NoLeaksOnBasicDocument)         // basic.xml fixture
TEST(ParserLeaks, NoLeaksOnNamespaceDocument)     // namespaces.xml fixture
TEST(ParserLeaks, NoLeaksOnCdataDocument)         // cdata.xml fixture
TEST(ParserLeaks, NoLeaksOnEntityDocument)        // DOCTYPE + entity refs
```

CI runs each under valgrind (Linux) / leaks (macOS) — zero bytes leaked.

## Architecture notes

The ownership invariant established by TODO 05 was "every node is
pool-allocated." This TODO extends it to **every byte the parser
allocates that ends up referenced by a node is pool-allocated.** The
pool becomes the single owner of all parse-lifetime memory; document
free releases everything in one `taurus_pool_destroy` call.

After this fix, the "free the intermediate" pattern disappears
entirely — there are no intermediates.

## Verification

```bash
for f in basic.xml namespaces.xml cdata.xml full.xml; do
    leaks --atExit -- build/cli/taurus parse test/fixtures/$f | grep "leaks for"
done
# Expected: every line reports "0 leaks for 0 bytes" (or close to it,
# modulo doctype internal_subset which is TODO 16).
```
