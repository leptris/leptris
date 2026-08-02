# TODO 50: C14N (Canonical XML) specs

**Priority**: P2 (coverage)
**Status**: Planned
**Effort**: M

## Problem

`taurus_c14n_canonicalize` is exposed in the public API
(`src/include/taurus/dom/serialize.h`) but has zero specs.  C14N
matters for digital signatures and cryptographic hashing — silent
canonicalization bugs would be security-relevant.

The C14N rules (per the header docstring):

1. UTF-8 encoding
2. Normalized line endings (\n)
3. Lexicographic attribute ordering
4. Namespace declaration ordering
5. Empty element normalization (`<tag></tag>` not `<tag/>`)
6. Entity/character reference expansion
7. Attribute value quoting with double quotes

Each rule needs at least one spec.

## Fix

`test/serializer/test_c14n.cpp`:

```cpp
TEST(C14N, NormalizesLineEndings) {
    /* \r\n in attribute values → \n. */
}

TEST(C14N, SortsAttributes) {
    /* <r b='2' a='1'/> → <r a='1' b='2'></r> */
}

TEST(C14N, SortsNamespaceDeclarations) {
    /* xmlns ordering. */
}

TEST(C14N, ExpandsEmptyElement) {
    /* <r/> → <r></r> */
}

TEST(C14N, UsesDoubleQuotes) {
    /* <r a='1'/> → <r a="1"></r> */
}

TEST(C14N, ExpandsEntityReferences) {
    /* &lt; → &lt; (preserved in C14N) vs numeric refs expanded */
}

TEST(C14N, RoundTripsStableUnderReCanonicalization) {
    /* Canonicalizing twice should give the same output. */
}

TEST(C14N, NoLeaksOnComplexDocument) { /* ... */ }
```

## Tests

The specs above are the deliverable.

## Architecture notes

C14N is a **deterministic transformation** — same input, same output,
every time.  That makes it highly testable: any spec that asserts on
exact output catches any drift.

If the current implementation has bugs (likely — it's untested), the
specs will surface them.  Fix bugs as they're found; don't paper over.

## Verification

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R C14N
# All specs pass.
```
