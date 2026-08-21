# 06 — Attribute iteration API (TODO 191 remainder + FFI gap)

Bindings cannot enumerate attributes: there is no first/next
attribute iteration in the public API (only count + lookup-by-name
+ typed getters). Known since the Python binding (TODO 82).

Add:
- `leptris_attribute_first(elem)` / `leptris_attribute_next(attr)`
  (or a by-index variant that is O(1) via the parse-adjacent
  layout).
- Spec coverage in test/dom + exercised from both bindings.

Also TODO 191's build side: LEPTRIS_BUILD_SHARED=ON currently fails
to export the full symbol set for FFI (the static objects are
linked into CLI/tests instead); fix the export map so the shared
library is a complete FFI surface.
