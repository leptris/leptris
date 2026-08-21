# TODO 95: DTD parser crash on Linux ASAN — CLOSED

**Priority**: ~~P1~~ Closed
**Status**: Fixed in PR #37
**Effort**: Done

## Original bug

The DTD validator stub spec (PR #34) called
`leptris_dtd_parse("<!ELEMENT root EMPTY>")` before invoking the
validator. On Linux (regular build and ASAN), the parse call SEGV'd:

```
==3579==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000290
#0 strlen (sanitizer_common_interceptors.inc:389)
#1 ttdtd_add_element model.c:260
#2 leptris_dtd_parse_internal_subset parser.c:341
#3 leptris_dtd_parse parser.c:393
```

Same code passed on macOS.

## Root cause

Same as TODO 94 — `strdup()` was implicitly declared as returning
`int` in strict C99 mode, truncating the 64-bit heap pointer to
4 bytes. The truncated value was sign-extended to 8 bytes on
assignment to `element->name` (a `char*`), producing
`0xffffffff<low32>`. The `strlen` on this invalid pointer crashed.

## Fix

PR #37 added `_POSIX_C_SOURCE=200809L` project-wide via
`target_compile_definitions` in `src/CMakeLists.txt`. With the
proper declaration, strdup returns `char*` (8 bytes) and the
DTD parser works correctly on Linux.

The DTD validator spec is now upgraded from "passing NULL DTD" to
actually parsing `<!ELEMENT root EMPTY>` — exercising the path
that previously crashed.

## Followup: leak fix

The same PR fixed a related leak: `leptris_dtd_parse` created its
own pool but `ttdtd_free` was a no-op. Now:

- `LeptrisDTD` has an `owns_pool` flag.
- `leptris_dtd_parse` sets `owns_pool = 1`.
- `ttdtd_free` destroys the pool only when `owns_pool` is set.
- Document-pool DTDs (created internally by the parser) leave
  `owns_pool = 0`; their pool is destroyed with the document.

Also added `ttdtd_element_create_pooled` — DTDElementDecl is now
pool-allocated when the parser has a pool, eliminating a calloc
leak caught by LSan.

## Acceptance

- `DtdValidate.ReturnsNotImplementedAfterParsingDtd` passes on
  Linux ASAN with zero leaks.
- All 118 specs pass under both regular and ASAN builds.
