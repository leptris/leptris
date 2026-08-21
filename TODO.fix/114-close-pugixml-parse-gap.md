# TODO 114 — Close pugixml parse gap

**Priority**: P0 (user goal: "dominate over pugixml")
**Status**: Open. Baseline measured 2026-08-06.

## Current state (Release+LTO, ~1 KB doc, M-series Mac)

| Op                          | leptris | pugixml | gap   |
| --------------------------- | ------ | ------- | ----- |
| Parse + free (small)        | 15 µs  | ~2 µs   | 7.5×  |
| Parse + free (attr-heavy)   | 71 µs  | ~10 µs  | 7×    |
| XPath `//book` + parse      | 148 µs | ~5 µs   | 30×   |

## Where the time goes (parser_parse_text profiling)

Per text node today:
1. `malloc(len+1)` — intermediate buffer
2. `memcpy(len)` — into intermediate
3. `pool_alloc(sizeof(node) + len + 1)` — final storage
4. `memcpy(len)` — into pool
5. `free(intermediate)`

Steps 1, 2, 5 are pure waste for the no-entity (≈95% of text) case. The
intermediate buffer exists only because the legacy non-writable parser
needed a NUL-terminated copy to pass to `leptris_text_create`. In
writable mode we can skip it.

## Plan

### Phase 1 — eliminate intermediate text buffer (this PR)
- In `parser_parse_text`, when `p->writable` and `has_entities == 0`:
  skip the `LEPTRIS_ALLOC_N` + memcpy; pass `start` directly to
  `leptris_text_create` which already does its own pool copy.
- No API change. Saves 1 malloc + 1 memcpy + 1 free per text node.
- Expected: 10-20% parse speedup on text-heavy docs.

### Phase 2 — length-aware text nodes (future)
- Add `size_t content_len` to `LeptrisTextNode`.
- For writable + no-entity case, point `content` at the in-buffer text
  (zero-copy). NUL not required since length is known.
- Skip both copies. Saves another memcpy per text node.
- Needs serializer, text-content concatenation, and `leptris_element_text`
  updates to handle non-NUL-terminated content.

### Phase 3 — pool-resident parser state (future)
- The Parser struct itself is heap-allocated via `LEPTRIS_ALLOC` per
  `leptris_parse_string` call. Pool-route it to remove one malloc per
  parse.
- Minor win (~1 µs) but every microsecond counts at the pugixml gap.

### Phase 4 — bulk attribute allocation (future)
- Today each attribute is a separate `pool_alloc(sizeof(attr))`. For
  elements with N attrs, that's N bumps.
- Pool a "attribute block" per element: one alloc for struct + N attrs
  when N is known after parsing all attrs.
- Requires two-pass parse OR storing attrs in a temp stack then
  bulk-allocating. Non-trivial; revisit after Phase 2.

## Acceptance

- Small-doc parse+free under 8 µs (currently 15 µs)
- Attribute-heavy parse+free under 30 µs (currently 71 µs)
- All 315 tests pass under Release and ASAN
- 0 leaks
