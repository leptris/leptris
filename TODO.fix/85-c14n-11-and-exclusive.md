# TODO 85: C14N 1.1 + exclusive canonicalization

**Priority**: P2 (correctness — current C14N is partial)
**Status**: Planned
**Effort**: M

## Problem

`leptris_c14n_canonicalize` exposes `LEPTRIS_C14N_1_0` and
`LEPTRIS_C14N_1_1` enum values.  The specs in
`test/serializer/test_c14n.cpp` verify basic C14N 1.0 behavior
(empty element expansion, attribute sorting) but don't cover:

- **C14N 1.1** differences (XML 1.1 line ending normalization, etc.)
- **Exclusive canonicalization** (xml-c14n11 + exclusive) — used in
  XML Signature
- **Comments** mode (with/without)
- **Inclusive namespace prefixes**

## Fix

Extend `test/serializer/test_c14n.cpp`:

```cpp
TEST(C14N11, NormalizesXml11LineEndings) { /* ... */ }
TEST(C14NExclusive, KeepsNamespaceDeclarations) { /* ... */ }
TEST(C14NWithComments, PreservesComments) { /* ... */ }
TEST(C14NWithoutComments, StripsComments) { /* ... */ }
TEST(C14NExclusive, DoesNotIncludeInheritedNamespaces) { /* ... */ }
```

If the current implementation doesn't support 1.1 or exclusive, mark
them as `TODO` in the source and have the tests skip with a clear
message.

## Tests

The specs above.

## Architecture notes

Canonical XML is a small spec but the edge cases are subtle.  The
current implementation handles the common cases (verified by TODO 57
specs) but full conformance requires running the W3C C14N test suite.

If the implementation is incomplete, the right path is:

1. Document what works (current specs).
2. Document what doesn't (this TODO).
3. Either complete the implementation or wrap a known-good library
   (libxml2's C14N) behind the same API.

## Verification

```bash
ctest --test-dir build --output-on-failure -R C14N
```
