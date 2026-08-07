# TODO 94: XPath variables crash on Linux ASAN — CLOSED

**Priority**: ~~P1~~ Closed
**Status**: Fixed in PR #37
**Effort**: Done

## Original bug

XPath variable specs (`XPathVariables.BooleanVariableEvaluates`, etc.)
crashed on Linux (both regular and ASAN builds) at `strcmp` in
`xpath_variable_set_get_const` (xpath_variables.c:325), faulting on
near-NULL address 0x570. Same code passed on macOS.

## Root cause (discovered via CI-driven debugging)

Taurus compiled with `-std=c99 -C_EXTENSIONS OFF` (strict C99 mode).
In that mode, `strdup()` — a POSIX function — is **not declared** by
`<string.h>`. Without a declaration, the compiler implicitly treated
it as `int strdup(...)` returning 4 bytes.

On macOS, libc happens to return pointers that fit in 32 bits and
the upper bits survive the int return slot.

On Linux x86_64, `strdup` returns full 64-bit pointers — but the
implicit-int return truncated them to 4 bytes. The truncated value
was then sign-extended when assigned to a `char*` field, producing
`0xffffffff<low32>` — an invalid pointer that crashed on the next
deref.

The CI-driven debug print showed the smoking gun:
```
DEBUG [0]: var=0x5585bdf2d900 name=0xffffffffbdf2d920
```
The `name` field's upper 4 bytes were `0xffffffff` (sign extension
of a negative int32), the lower 4 bytes were the low half of the
real heap address.

## Fix

`src/CMakeLists.txt` now declares:

```cmake
target_compile_definitions(taurus_objects PUBLIC _POSIX_C_SOURCE=200809L)
```

This makes `<string.h>` declare `strdup()` with its correct `char*`
return type. All strdup callers are safe.

Also added `<strings.h>` includes in `element_query.c` and
`xpath/functions.c` — POSIX `strcasecmp`/`strncasecmp` live in
`<strings.h>`, not `<string.h>`.

## Specs re-added

The four `XPathVariables.*` specs that backed out earlier are now
re-added and pass under Linux ASAN.

## Acceptance

- All 118 specs pass under both regular and ASAN builds (Linux + macOS).
- `XPathVariables.{Boolean,Number,String}VariableEvaluates` and
  `XPathVariables.UnknownVariableReturnsEmpty` are in the suite.
