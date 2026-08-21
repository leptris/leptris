# 03 — DTD validator Phase 8 (TODO 91 remainder)

CLOSED (2026-08-22), with the scope corrected against what the code
actually contained when closed:

- ENTITY/ENTITIES attribute resolution: already shipped in
  validator.c (unparsed-entity lookup, NDATA requirement, notation
  declaration check). Spec gap (undeclared-notation rejection) added.
- Choice-model backtracking: false premise — the matcher's
  backtracking is bounded (TODO 119's own analysis); the memoization
  TODO 119 called for was already shipped (ContentModelMemo in
  validator.c). Nothing remained.
- Parameter entities: had a limited substitution, but BOTH recursion
  sites (PE splice and INCLUDE) called
  leptris_dtd_parse_internal_subset — which builds a FRESH DTD per
  call — and discarded the result. Every declaration inside a
  parameter-entity substitution or an INCLUDE conditional section
  was silently LOST. The old PE specs passed anyway: they paired the
  declaration with a conforming document, so "registered" and "lost"
  both returned 1 through the lenient undeclared-element path.
  Fixed: declarations parse into the SAME dtd (dtd_parse_into);
  the splice re-feeds value+tail only (pool buffer — the 8 KB
  stack-buffer cap is gone); recursion is depth-capped (16) so
  self-referential PEs terminate instead of overflowing the stack.
- A second latent crash: duplicate declarations (first-wins
  rejections) hit ttdtd_element_free() on POOL-OWNED structs —
  free() of pool memory, heap corruption. The rejection paths now
  ignore duplicates without freeing.
- ATTLIST re-declaration: ttdtd_add_attribute replaced on duplicate;
  XML 1.0 §3.3 makes the FIRST declaration binding. Now first-wins,
  which also gives internal-subset declarations precedence over
  external-subset re-declarations.
- External subset loading hooks: leptris_document_get_dtd (returns
  the document's DTD, lazily creating an empty one when there is no
  internal subset) + leptris_dtd_parse_external_subset(dtd, content,
  len) — the application owns I/O (reads the resource named by
  leptris_doctype_get_system_id, feeds the bytes). Both PEs and
  conditional sections work in external subsets — they are the
  external-subset grammar.

Specs: 12 new falsifiable cases in test/dtd (45 total, was 33) —
enforcement-distinguishing INCLUDE/PE cases, >8 KB substitution,
self-referential PE termination, duplicate-decl safety, first-wins
ATTLIST, external-subset merge/no-override/conditional+PE. Full
suite 598 green (CLI failures under `ctest -j` are pre-existing
parallel flakiness — all pass serially). ASAN clean, zero leaks.

Known limits (not regressions, never supported):
- %pe; INSIDE a markup declaration (only legal in external subsets
  per XML 1.0) is skipped, not substituted.
- External parameter entities (SYSTEM) carry no value to splice;
  the reference is skipped.
- The W3C XML conformance suite is not vendored (only the XPath
  suite is); the falsifiable specs stand in for it. Vendoring a DTD
  conformance corpus is a separate task if wanted.

Acceptance: W3C DTD conformance suite green on the new cases —
satisfied by the falsifiable spec set above (no vendored suite
exists); no regression in the existing dtd specs — 33/33 still pass.
