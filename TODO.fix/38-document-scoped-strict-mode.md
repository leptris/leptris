# TODO 38: Document-scoped strict mode + allocator hooks (TODO 27 phase 2)

**Priority**: P1 (architecture — full thread-safety story)
**Status**: Planned
**Effort**: M

## Problem

TODO 27 phase 1 made `g_taurus_strict_mode` and the allocator hooks
`__thread`, eliminating process-wide races.  But that's still not
enough: two documents in the **same thread** with different strict /
allocator requirements can't coexist.

Example: a library that wants to parse user-provided XML strictly
(rejecting bare entities) while also parsing trusted config XML
leniently.  With `__thread`, the library has to swap the global
before/after each parse — race-prone if the parse itself spawns
threads.

## Root cause

State that should be document-scoped is thread-scoped.

## Fix

### Step 1: add fields to TaurusDocument

```c
struct taurus_document {
    // ... existing fields
    int strict_mode;                       /* per-document strict flag */
    taurus_allocation_function alloc_hook; /* per-document allocator */
    taurus_deallocation_function dealloc_hook;
};
```

### Step 2: add public API

```c
TAURUS_API TaurusStatus taurus_document_set_strict(TaurusDocument doc, int strict);
TAURUS_API int          taurus_document_get_strict(TaurusDocument doc);

TAURUS_API TaurusStatus taurus_document_set_allocators(
    TaurusDocument doc,
    taurus_allocation_function alloc,
    taurus_deallocation_function dealloc);
```

### Step 3: parser uses doc, not global

Replace `taurus_get_strict_mode()` calls in `parse/parser_new.c` with
`p->strict_mode` (parser field, threaded through from doc).

Replace `taurus_alloc_hook` / `taurus_free_hook` calls in
`memory/pool.c` with `pool->alloc_hook` / `pool->dealloc_hook`.

### Step 4: keep process-wide defaults

`taurus_set_strict_mode(int)` and the existing
`taurus_allocation_function_set` set **defaults** that new documents
inherit when they don't specify their own.  This preserves backwards
compatibility.

## Tests

`test/parser/test_parser.cpp`:

```cpp
TEST(ParserStrictMode, DocumentScoped) {
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string("<r/>", 4, &st);
    ASSERT_NE(doc, nullptr);

    taurus_document_set_strict(doc, 1);
    EXPECT_EQ(taurus_document_get_strict(doc), 1);

    taurus_document_free(doc);
}

TEST(ParserStrictMode, TwoDocumentsIndependent) {
    TaurusStatus st;
    TaurusDocument a = taurus_parse_string("<r/>", 4, &st);
    TaurusDocument b = taurus_parse_string("<r/>", 4, &st);
    taurus_document_set_strict(a, 1);
    EXPECT_EQ(taurus_document_get_strict(b), 0);  /* unaffected */
    taurus_document_free(a);
    taurus_document_free(b);
}
```

Plus a multi-threaded spec that parses N documents concurrently with
different strict modes and verifies no cross-contamination.

## Architecture notes

Process-wide mutable state is a library anti-pattern.  `__thread`
moves it to thread-scope, which is better but still not enough — the
right scope is **the document**, the unit of work the user actually
manipulates.

The C analog of Ruby's "instance variable" — state that belongs to an
instance, not a class or process.

## Verification

```bash
ctest --test-dir build --output-on-failure -R StrictMode
# All StrictMode specs pass.

# Multi-threaded isolation:
build/test/parser/test_parser_multithreaded
# Exit 0, no cross-contamination.
```
