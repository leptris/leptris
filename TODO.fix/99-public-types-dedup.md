# TODO 99 — eliminate typedef/enum duplication across public headers

**Priority**: P1 (architecture / DRY)
**Status**: done

## Symptom

`leptris.h` and `leptris/types.h` both defined the same public types:
opaque handles (`LeptrisDocument`, `LeptrisElement`, …), `LeptrisStatus`,
`LeptrisXPathResultType`, `LeptrisSerializeOptions`, `LeptrisC14NVersion`,
`LeptrisXPathVariableType`, `LeptrisXPathVariableSet`, and the
`leptris_allocation_function` / `leptris_deallocation_function` typedefs.

Internal headers (`dom/element.h`, `xpath/evaluator.h`, `dtd.h`,
`memory/pool.h`) had their *own* copies of subsets of these typedefs,
band-aided with `LEPTRIS_INTERNAL_TYPES_DEFINED` /
`LEPTRIS_ALLOCATION_FUNCTION_DEFINED` guards (added in TODO 12 and
TODO 98).  Three copies of the same definition is the textbook DRY
violation.

## Fix

Make `leptris/types.h` the **single canonical source** of every public
type.  Other headers delegate:

* `leptris.h` now `#include "leptris/types.h"` and drops its local
  copies of all the typedefs/enums/structs listed above.  Only the
  `LEPTRIS_API` macro and function declarations remain.
* `memory/pool.h` now `#include "leptris/types.h"` directly for the
  allocator-hook typedefs (instead of carrying its own guarded
  copies).  Removes the `LEPTRIS_ALLOCATION_FUNCTION_DEFINED` band-aid.
* `serialize/serialize.c` had a private copy of `LeptrisSerializeOptions`
  — deleted; uses the public type via `leptris/types.h`.
* `leptris/types.h` gains `LeptrisNodeRef` (previously only in
  `leptris.h`).

## Why this is the right model

The two public headers now have a clear, MECE contract:

* `leptris/types.h` — every shared type, no functions.  Include this
  when you only need the type surface (FFI bindings, opaque-pointer
  users).
* `leptris.h` — full API: includes `leptris/types.h`, adds the
  `LEPTRIS_API` macro and every function declaration.

Adding a new public type now means editing **one** file.  The
internal-header guards (`LEPTRIS_INTERNAL_TYPES_DEFINED`) stay in place
where they still serve to break ordering cycles between internal
headers — they cost nothing and document intent.

## Verification

* Warning-clean build under `-Wall -Wextra`.
* 168/168 ctest pass.
* macOS `leaks --atExit` clean on `test_dom`, `test_xpath`,
  `test_sax`, `test_dtd`, `test_xinclude`.
* `examples/` and CLI still build without modification — the public
  include surface is unchanged, only the implementation hierarchy
  moved.
