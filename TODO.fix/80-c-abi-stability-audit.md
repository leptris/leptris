# TODO 80: C ABI stability audit

**Priority**: P1 (foundation for FFI)
**Status**: Planned
**Effort**: M

## Problem

The public API in `src/include/taurus/` is the FFI contract.  Today
it's clean (opaque handles, status params, ownership docs) but there
are a few rough edges that would trip up bindings:

1. **Mixing types for the same concept.** Some functions return
   `const char*`, some return `char*` (caller frees).  Easy to get
   wrong from a binding.

2. **Boolean as int.** Several functions use `int` where the value
   is really boolean (0 or 1).  Bindings to languages with proper
   bools have to coerce.

3. **No size_t consistency.** Some lengths are `size_t`, some `int`.
   Cross-language FFI prefers `size_t` everywhere.

4. **Output parameters vs. return values.** Some functions return
   the result; others take `TaurusStatus*` as out-param.  Pick one
   pattern per category.

5. **Macro pollution.** `TAURUS_API` macro may expand differently
   on Windows vs. Unix; bindings need to know.

## Fix

### Audit pass

For each public function, ask:

- Are all length-like parameters `size_t`?
- Are boolean-like parameters and returns documented as such?
- Is ownership documented ("Memory:" comment)?
- Is the signature stable across platforms?

Fix the inconsistencies.  Most are mechanical.

### Symbol export control

Add a linker version script:

```
taurus.map:
TAURUS_0.1 {
    global:
        taurus_*;
    local: *;
};
```

Only `taurus_*` symbols are exported.  Internal helpers stay private.

### Smoke test

Add `test/abi/test_abi.c` that calls every public function.  Catches
accidental removals.

## Tests

- ABI test (above).
- `nm build/src/libtaurus.so | grep ' T '` shows only `taurus_*`
  symbols.

## Architecture notes

Once bindings exist (TODO 81-84), the ABI is effectively frozen.
Doing the audit now — before any bindings ship — is much cheaper
than fixing it after.
