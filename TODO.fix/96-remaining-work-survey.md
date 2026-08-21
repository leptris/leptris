# TODO 96: Comprehensive remaining-work survey

**Priority**: All
**Status**: Snapshot as of 2026-08-03 (post-PR #35)
**Effort**: Variable

## What's done

This session merged 13 PRs to main (#23–#35):

| PR  | Subject |
|-----|---------|
| #23 | ASAN crash fixes (parser field init, SAX ns_prefixes) |
| #24 | docs refresh (building.md, CHANGELOG) |
| #25 | leptris.c phase 2 split (Node + XPath public API) |
| #26 | man page version drift |
| #27 | leptris.c phase 3 split (Element query API) |
| #28 | relocate leptris_element_hash_value |
| #29 | XPath nodeset variable support (TODO 86) |
| #30 | leptris.c phase 4 split (C14N) |
| #31 | architecture review (TODO 70/93) |
| #32 | namespace list fix (TODO 87) |
| #33 | XInclude classifier implementations + process stub |
| #34 | DTD validation stub + error_free |
| #35 | arch review update |

leptris.c went from **2646 → 900 lines**. Every previously
declared-but-undefined public API function now has a defined symbol.
Test count: **105 → 114**. All 15 CI workflows green on main.

## What's actually remaining

### Real Linux-only bugs (need Linux env to debug)

- **TODO 94** — XPath variable specs crash on Linux ASAN at
  `strcmp` in `xpath_variable_set_get_const` (fault 0x570).
  Same code passes on macOS. Root cause unknown.
- **TODO 95** — DTD parser crashes on Linux ASAN at `strlen` in
  `ttdtd_add_element` (fault 0x290). Same code passes on macOS.
  Likely sibling of TODO 94.

Both crash in non-ASAN Linux builds too, so they're real bugs.
Pattern: read from near-NULL pointer after a struct-field access.
Local macOS debugging has not reproduced either.

### Large features with stubs in place

- **TODO 91 (DTD validator)** — stub returns
  `LEPTRIS_ERROR_NOT_IMPLEMENTED`. Full engine needs content-model
  grammar matcher, ATTLIST enforcement, attribute type checking.
  Estimated 1-2 weeks.
- **TODO 92 (XInclude processor)** — classifier helpers ship;
  processor is a stub. Full impl needs href resolution,
  parse="xml"|"text" dispatch, fallback, xpointer. Estimated
  1-2 weeks.

### Large features not started

- **TODO 89 (incremental SAX)** — `leptris_sax_parser_feed` is
  one-shot under the hood. True incremental parsing requires
  resumable parser state at every chunk boundary.
- **TODO 69 (XPath conformance suite)** — W3C test suite not in
  tree. Needs download + harness + result comparison.

### Design done, implementation language-specific

- **TODOs 79–85 (FFI)** — `docs/FFI.md` documents the contract.
  Ruby, Python, Rust bindings each need their own repo with
  language-specific FFI tooling (ffi gem, cffi, bindgen).

### Deferred / closed-no-change

- **TODO 41 (unified string ownership)** — investigated; current
  design is intentional for the `leptris_parse_string_inplace`
  zero-copy API.
- **TODO 42 (leptris.c split)** — all 4 phases done.
- **TODO 70/93 (architecture review)** — `docs/ARCHITECTURE_REVIEW.md`
  shipped.
- **TODO 86 (XPath nodeset variables)** — implemented.
- **TODO 87 (namespace list)** — fixed.
- **TODO 88 (COW deep copy)** — freeze API itself incomplete;
  deferred until there's a consumer asking for immutable docs.
- **TODO 90 (compact parser text linking)** — compact parser not
  wired into active call graph; fix would be academic.

## Recommended next-session priorities

1. **Reproduce TODO 94 / 95 on a Linux machine** (Docker or VM).
   Use `gdb` to print `set->variables[0]` and `element->name`
   immediately before the faulting read.
2. **TODO 91 Phase 1** — basic DTD validation (EMPTY/ANY content
   models + #REQUIRED attribute check). 1-2 days.
3. **TODO 92 Phase 1** — basic XInclude (parse="text" only, local
   files). 2-3 days.
4. **TODO 69** — download W3C XPath conformance suite, build the
   harness. 1-2 days.
5. **TODO 89** — design the incremental SAX state machine. 1 week.

## Closure audit (what got finished this session)

- All previously declared-but-undefined public API functions now
  have implementations (real or stubs returning NOT_IMPLEMENTED).
- leptris.c is navigable (900 lines, focused on document lifecycle).
- 4 new focused translation units (`node_public.c`,
  `element_query.c`, `xpath_public.c`, `c14n.c`).
- Public headers have `_Static_assert` ABI-stability guards.
- vcpkg overlay port under `ports/leptris/`.
- Architecture review captures state and known debt.
- TODO.fix/ contains 35+ files documenting all work, completed
  and pending.

The codebase is markedly more maintainable than at session start.
Remaining work is well-scoped and documented.
