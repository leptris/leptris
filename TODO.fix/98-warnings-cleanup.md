# TODO 98: Warnings cleanup — enum leak, typedef redefs, dead code

**Priority**: P0 (correctness bug) + P1 (hygiene)
**Status**: done

The `-Wall -Wextra` build produces ~25 distinct warnings. Three
classes, all real:

## Class 1 — enum-conversion (P0 correctness bug)

Four sites assign `LEPTRIS_ERROR_MEMORY_ALLOCATION` (internal
`leptris_error_code`, value `1`) to a `LeptrisStatus*` out-param:

* `src/leptris/leptris.c:435`   (`leptris_parse_string` UTF-16-BOM path)
* `src/leptris/leptris.c:472`   (`leptris_parse_string` UTF-16-no-BOM path)
* `src/leptris/encoding/wrapper.c:45` (`leptris_parse_string_with_options`)
* `src/leptris/encoding/wrapper.c:73` (same, no-BOM branch)

The public contract (`include/leptris/types.h:41`) says memory failure
is `LEPTRIS_ERROR_MEMORY = -1`. Writing `1` instead is a public API
bug — callers checking `status == LEPTRIS_ERROR_MEMORY` will miss the
failure entirely.

**Fix**: use the public constant `LEPTRIS_ERROR_MEMORY` at every public
out-param write. Internal `leptris_error_code` stays for `error.c`'s
thread-local `last_error`, which is a separate channel.

## Class 2 — typedef redefinition (C11 feature, C99 build)

Same typedefs defined in multiple headers. C11 allows it; the project
targets C99, so `-Wtypedef-redefinition` fires.

* `LeptrisElement` redefined in `dom/element.h:26` (already in `types.h`)
* `LeptrisDocument` redefined in `dtd.h:19`
* `LeptrisDTD` redefined in `common/types_internal.h:21`
* `leptris_allocation_function` / `leptris_deallocation_function`
  redefined in `memory/pool.h:25-26` and `leptris.h:1508,1514`
* `LeptrisDoctypeNode` redefined in `leptris.c:67`

**Fix**: guard each typedef with a per-typedef macro, the way
`types.h` already does (`LEPTRIS_INTERNAL_TYPES_DEFINED`). One
canonical definition per type, all re-includes become no-ops.

## Class 3 — unused variables / dead functions

* `element_query.c:713` — `const char* start = text;` in `_uint`, never read
* `element_query.c:761` — same in `_double`
* `content_check.c:115` — static `op_name()` never called
* `c14n.c:133` — static `compare_namespaces()` never called

**Fix**: delete each.

## Verification

* `cmake --build build` is warning-clean under `-Wall -Wextra`.
* Add a DOM spec that calls `leptris_parse_string` on a huge synthetic
  UTF-16 input under malloc failure (or unit-test the path with a
  stubbed allocator) to lock in the public contract — at minimum,
  add a regression spec that exercises `leptris_parse_string(NULL, 0, &st)`
  and asserts `st` is a documented public value.
