# TODO 25: Kill remaining parser leaks (`taurus_sv_to_cstr`, `parse_name`)

**Priority**: P0 (correctness)
**Status**: Planned
**Effort**: S

## Problem

After TODO 05 phase 2 and TODO 15, the leak profile on `basic.xml`
(218 B input) still shows:

```
Process ...: 8 leaks for 128 total leaked bytes.

STACK OF 7 INSTANCES OF 'ROOT LEAK: <malloc in taurus_sv_to_cstr>':
  ...

STACK OF 1 INSTANCE OF 'ROOT LEAK: <malloc in parse_name>':
  ...
```

`taurus_sv_to_cstr` is called from `parser_new.c` at several sites
that pass its result to a setter that copies into the pool — but the
intermediate buffer is not freed on every path.

## Root cause

`taurus_sv_to_cstr` always `malloc`s a new buffer.  Callers that
hand the result to a pool-copying setter (e.g.,
`taurus_element_set_namespace_uri_view`) must `free()` the
intermediate.  Some sites miss the free; others free in one branch
but not another.

## Fix

Audit every `taurus_sv_to_cstr` call in `parse/parser_new.c` (lines
1311, 1353, 1446, 1479, 1491, 1492).  For each:

1. Identify the consumer — does it copy the string, or store the
   pointer?
2. If it copies (most setters), `free()` the intermediate after the
   call.
3. If it stores the pointer, replace `taurus_sv_to_cstr` with
   `taurus_sv_to_cstr_pooled(sv, p->pool)` so the pool owns the
   allocation.

The `parse_name` leak is the same pattern: `parse_name` returns a
calloc'd buffer; the consumer copies it; the original is sometimes
not freed.

## Tests

`test/parser/test_parser.cpp` extends with specs that exercise each
path:

```cpp
TEST(ParserLeaks, NoLeaksOnNamespaceDocument)
TEST(ParserLeaks, NoLeaksOnPrefixedAttribute)
TEST(ParserLeaks, NoLeaksOnDefaultNamespace)
```

CI runs each under `leaks --atExit --` and `valgrind --error-exitcode=1`;
zero bytes leaked.

## Verification

```bash
for f in basic namespaces cdata full; do
  leaks --atExit -- build/cli/taurus parse test/fixtures/$f.xml | grep "leaks for"
done
# Expected: every line reports 0 leaks for 0 bytes.
```
