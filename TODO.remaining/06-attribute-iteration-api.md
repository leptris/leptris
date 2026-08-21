# 06 — Attribute iteration API (TODO 191 remainder + FFI gap)

DONE (2026-08-22).

Shipped:
- `leptris_element_first_attribute(elem)` → LeptrisAttribute,
  `leptris_attribute_next(attr)`,
  `leptris_attribute_get_name(attr)`,
  `leptris_attribute_get_value(elem, attr)` (entity-expanding, same
  contract as leptris_element_attribute; elem supplies the pool —
  the 40 B attr layout carries no doc backpointer by law).
  Handle iteration is O(n) total where the _at(index) accessors
  re-walk the list per call (O(n²) for a full enumeration).
- Specs: 4 new in test/dom (walk-all in order, NULL contracts,
  entity expansion, mutation-append visibility) — 602 tests green
  serially, ASAN clean.
- Exercised from both in-repo bindings: Ruby Element#each_attribute
  (+ attached leptris_element_attribute_count, which element.rb
  called without ever attaching it — latent NoMethodError), Python
  Element.attributes(). Ruby 26/26, Python 24/24 against the fresh
  shared library.
- Shared-library export audit: every LEPTRIS_API declaration across
  all public headers (171 symbols) is exported from the SHARED=ON
  dylib (179 exported). The one declared-but-missing symbol,
  `leptris_parse_string_compact`, was a PHANTOM — declared in three
  headers, never defined, never called, never exported by any build
  (the compact parser became the default path long ago). Deleted
  from leptris.h, dom/document.h, memory/compact_allocator.h. Not an
  ABI break: no binary ever contained the symbol.
- Python cdef corrected to mirror the real 2-arg
  leptris_element_attribute signature (it declared a 3-arg
  default_value variant that does not exist; callers passed NULL so
  it worked by accident).
