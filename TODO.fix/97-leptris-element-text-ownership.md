# TODO 97 — leptris_element_text ownership contract violation

## Priority
P0 — public API leaks per-call. Caught by `TextContentAccessors` ASAN run
on Linux CI for PR #51 (TextContentAccessors_IntTextReturnsValue, leak of
4 bytes via `leptris_element_text_int` → `leptris_element_text`).

## Symptom
Every call to `leptris_element_text()` malloc'd a buffer and returned it as
`const char*`. The header documents the string as **owned by the element**
("Do not free or modify"), but the only safe response to that contract is to
never free the buffer — guaranteeing a leak per call. Callers (xpath, the
`_text_int` / `_text_double` / `_text_uint` / `_text_float` / `_text_bool`
family, the DOM benchmark) all treat the result as borrowed, so each call
leaked the buffer.

```
Direct leak of 4 byte(s) in 1 object(s) allocated from:
  leptris_element_text_int  src/leptris/dom/element_query.c:664
  → leptris_element_text    src/leptris/dom/element_query.c:46
  → leptris_element_get_text_content  src/leptris/dom/element.c:854 (leptris_malloc)
```

## Fix
Make the implementation match the documented ownership: the returned
pointer must live as long as the document. Two paths:

1. **Single text/CDATA child** — return the child node's own storage
   directly (`LeptrisTextNode::content` / `LeptrisCDATANode::content`).
   Zero allocation, no leak possible.
2. **Mixed content** — concatenate via the existing
   `leptris_element_get_text_content`, then intern the result in the
   document pool via `leptris_pool_strdup`. The string is released when
   `leptris_document_free` destroys the pool.

`element_owning_document()` walks the parent chain so the lookup is robust
against detached subtrees whose `document` pointer is unset but whose
ancestor is attached. If the element is fully detached, return `""` (the
document-scoped model has nowhere to put the join; falling back to
`leptris_malloc` would re-introduce the leak).

## Why this is the right model

* The header already promised document-owned storage. The implementation
  was simply wrong.
* The two `_text_*` conversion functions and XPath's `xpath_public.c`
  callers never had to free anything — they all treat the result as
  borrowed. Fixing the implementation here repairs **all** call sites
  simultaneously, including pre-existing latent leaks in xpath.
* Pool allocation matches the rest of the library's ownership model
  (TODO 41 "Unify string ownership model"). Strings created from a
  document live as long as the document.

## Specs
Added to `test/dom/test_dom.cpp::TextContentAccessors`:

* `IntTextReturnsValue` / `DoubleTextReturnsValue` — typed conversion
  paths; now free of manual `free()` calls in the test (the API owns
  the storage).
* `TextIsDocumentOwnedForSingleTextChild` — single text child → same
  pointer on repeated calls, no allocation.
* `TextIsDocumentOwnedForMixedContent` — mixed content → concatenated
  into document pool, valid until `leptris_document_free`.
* `EmptyElementYieldsEmptyString` — empty / NULL element → `""`,
  not NULL (header docstring updated to match).

## Why not just change the contract to "caller frees"

That would force every caller to track a malloc'd buffer that's identical
in shape to document-owned strings, force the SAX/xpath layers to track
lifetime across frames, and force the DOM benchmark to either leak or
free strings whose lifetime semantics it shouldn't need to know. It also
mismatches every other public text accessor in the library (e.g.
`leptris_attribute`, `leptris_node_name` — all return borrowed pointers).

## Verification
* `ctest --test-dir build` — 166/166 pass.
* `leaks --atExit -- ./build/test/test_dom` — 0 leaks.
* `leaks --atExit -- ./build/test/test_xpath` — 0 leaks (xpath no longer
  leaks when a nodeset result is converted to number/string).
* `leaks --atExit -- ./build/test/test_sax` — 0 leaks.
* Header docstring updated in `src/include/leptris.h` and
  `src/include/leptris/dom/element.h` to state the document lifetime
  and the two-path policy.
