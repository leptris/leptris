# TODO 100 — remaining-work survey (post-PR #53 snapshot)

**Priority**: All
**Status**: active
**Snapshot**: 2026-08-03, after PR #51 (specs), #52 (warnings), #53
(public-type dedup).

## Shipped this session

| PR  | Subject |
|-----|---------|
| #51 | Specs for previously-uncovered public APIs + taurus_element_text ownership fix |
| #52 | Warning-clean build under -Wall -Wextra (P0 enum leak + typedef redefs + dead code) |
| #53 | taurus.h delegates all public types to taurus/types.h (DRY) |

taurus.c stays at ~900 lines.  All 168 ctest pass.  All 15 CI workflows
green.  Build is `-Wall -Wextra` clean on both macOS and Linux.

## What's actually remaining

### Real features with stubs

- **TODO 88 (COW deep copy)** — `taurus_node_thaw` is a stub that just
  flips the `frozen` bit without copying.  Public freeze API is shipped;
  COW on mutation is not.  Needs: pool deep-copy, attribute/string
  re-routing, child subtree clone.  Estimated 1 week.
- **TODO 89 (incremental SAX)** — `taurus_sax_parser_feed` is one-shot
  under the hood.  True incremental parsing needs a resumable parser
  state machine at every chunk boundary.  Estimated 1-2 weeks.
- **TODO 91 Phase 8 (DTD validator)** — current engine does
  EMPTY/ANY/Mixed/Element content + #REQUIRED/#IMPLIED/#FIXED +
  attribute type checking.  Missing: ENTITY/ENTITIES attribute type
  resolution, parameter entities (`%pe;`), choice-model backtracking
  on ambiguous content models.  Estimated 1-2 weeks for full coverage.
- **TODO 92 Phase 2 (XInclude)** — `parse="text"` + xi:fallback
  shipped.  Missing: `parse="xml"` (cross-document node copy with
  pool ownership transfer), xpointer fragment resolution.  Estimated
  1-2 weeks.
- **TODO 69 (XPath conformance suite)** — comprehensive in-tree XPath
  specs exist (~50 cases) but W3C's official conformance suite is not
  wired in.  Estimated 1-2 days for the harness + first batch.

### Hygiene / docs

- **CLAUDE.md accuracy** — three stale items: (a) says xinclude/ is
  "not currently built" (it is), (b) says dtd/validator.c is
  "commented out" (Phases 1-7+FIXED shipped), (c) doesn't mention
  TODOs 94-99.
- **archive/backups/evaluator_axes.c.bak2** — historical; per global
  rule do not delete without user confirmation.  Suggest moving to a
  tag/branch instead of carrying in-tree.

### Language-specific (out of scope for this repo)

- **TODOs 79–85 (FFI)** — `docs/FFI.md` documents the contract.
  Ruby, Python, Rust bindings each need their own repo with
  language-specific FFI tooling.

### Closed-no-change

- **TODO 41 (unified string ownership)** — investigated; current
  design is intentional for the `taurus_parse_string_inplace`
  zero-copy API.
- **TODO 42 (taurus.c split)** — all 4 phases done.
- **TODO 70/93 (architecture review)** — shipped as
  `docs/ARCHITECTURE_REVIEW.md`.
- **TODO 86 (XPath nodeset variables)** — implemented.
- **TODO 87 (namespace list)** — fixed.
- **TODO 90 (compact parser text linking)** — compact parser not
  wired into active call graph; fix would be academic.
- **TODOs 94, 95 (Linux ASAN crashes)** — fixed.

## Recommended next-session priorities

1. **CLAUDE.md accuracy pass** — quick, high-value for future
   contributors.  Half a day.
2. **TODO 92 Phase 2** — XInclude parse="xml" is the most
   user-requested missing feature.  1-2 weeks.
3. **TODO 88 (COW deep copy)** — needed before the freeze API is
   actually safe to use for mutation workflows.  1 week.
4. **TODO 69 (XPath conformance suite)** — final XPath correctness
   backstop.  1-2 days.
5. **TODO 91 Phase 8 (DTD validator completeness)** — closes DTD
   feature surface.  1-2 weeks.
6. **TODO 89 (incremental SAX)** — only blocking for streaming
   use cases; design first, then implement.  1-2 weeks.
