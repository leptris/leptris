# TODO 16: Fix DTD subsystem memory ownership

**Priority**: P0 (correctness — single biggest leak source)
**Status**: Planned
**Effort**: M

## Problem

`leaks` on `full.xml` (a document with a DOCTYPE internal subset)
reports **131 KB leaked across 12 allocations**. The dominant sources:

```
STACK OF 1 INSTANCE OF 'ROOT LEAK: <calloc in taurus_dtd_create>':
  7 (128K) ROOT LEAK
    4 (64.1K) <malloc in taurus_pool_create>        ← DTD's OWN pool
    2 (64.1K) <malloc in taurus_pool_create>        ← pre-allocated page

STACK OF 1 INSTANCE OF 'ROOT LEAK: <calloc in ttdtd_entity_create>':
  3 (96 bytes)
    1 (16 bytes) <malloc in dtd_parse_quoted_string>
    1 (16 bytes) <malloc in ttdtd_entity_create>
```

## Root cause

The DTD subsystem (`src/taurus/dtd/{model.c,parser.c,resolver.c}`)
**creates its own private memory pool** at `taurus_dtd_create` time
(`pool.c`'s default 64 KB page size) and never destroys it. It also
calloc's `TaurusDTD` (80 B), `DTDEntityDecl` (64 B each), and various
intermediate strings — none of which are freed by
`taurus_document_free`.

`taurus_document_free` (in `taurus.c:864`) does call
`ttdtd_free((TaurusDTD*)doc->dtd)`, but that function (in `dtd/model.c`)
frees only the hash tables, not the pool or the calloc'd structs.

## Fix

**Strategy**: DTD allocations are document-scoped. Route them through
the document's pool. Specifically:

### Step 1: Document pool becomes DTD's pool

`taurus_dtd_parse_internal_subset` (in `dtd/parser.c`) currently does:

```c
TaurusDTD* dtd = taurus_dtd_create();
```

Change to:

```c
TaurusDTD* dtd = taurus_dtd_create(pool);  // pass the document's pool
```

`taurus_dtd_create` then no longer creates its own pool — it uses the
caller's. All `ttdtd_*_create` functions (entity, element, notation,
attribute declarations) similarly take the pool.

### Step 2: Make `ttdtd_free` a no-op (or delete it)

Once every DTD allocation comes from the document's pool, freeing the
document destroys the pool which destroys the DTD memory. The
`ttdtd_free` function becomes redundant; either delete it or have it
assert that nothing needs freeing.

### Step 3: Doctype setter functions through the pool

While here, complete TODO 05 phase 3: `taurus_doctype_set_public_id`,
`set_system_id`, `set_internal_subset` (in `dom/doctype.c`) currently
`taurus_strdup` (calloc). Change them to take a pool, or to use
whatever pool the doctype node belongs to (via the document
back-pointer).

## Tests

`test/parser/test_parser.cpp`:

```cpp
TEST(ParserLeaks, DoctypeWithInternalSubsetIsPoolOwned) {
    const char xml[] =
        "<!DOCTYPE r ["
        "<!ENTITY foo 'bar'>"
        "<!ENTITY baz 'qux'>"
        "<!ELEMENT r (#PCDATA)>"
        "<!ATTLIST r attr CDATA #IMPLIED>"
        "]>"
        "<r attr='val'>&foo;</r>";

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    // The internal subset must be queryable.
    // (This implicitly exercises the DTD hash tables.)

    taurus_document_free(doc);
    // Under leaks/valgrind: zero leaks.
}
```

## Architecture notes

**Ownership invariant** (extends TODO 05/15):

> Every byte the parser allocates that ends up referenced by a
> document — including DTD declarations — comes from the document's
> pool. The DTD subsystem has no private pool.

This eliminates the DTD's "two pool" confusion (document pool + DTD
pool both alive during parse). It also fixes the hidden 128 KB
overhead that every DOCTYPE-bearing document paid even for tiny
internal subsets.

## Verification

```bash
leaks --atExit -- build/cli/taurus parse test/fixtures/full.xml | grep "leaks for"
# Expected: 0 leaks for 0 bytes (down from 12 leaks / 131 KB).
```
