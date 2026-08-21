# TODO 115 — Length-aware text nodes (true zero-copy parse)

**Priority**: P1
**Status**: Completed (2026-08-06). Phases A–D landed.

## Outcome

| Phase | PR | Effect |
| --- | --- | --- |
| A | #114 | `size_t content_len` field on `LeptrisTextNode`. Foundation, no behavior change. |
| B+C | #115 | `leptris_text_create_borrowed()` + parser wiring + consumer updates. Zero-copy text on the parse hot path. |
| D | #116 | Acceptance: perf measurement + TODO status flip. |

Perf (M-series Mac, Release+LTO, median of 5000):

| Workload | Pre-TODO | Post-TODO | Δ |
| --- | --- | --- | --- |
| Text-heavy 1 KB (8 paragraphs) | 8.12 µs | 8.08 µs | within noise at this size |
| Small 512 B (attr-heavy) | 11.83 µs | 11.75 µs | within noise |
| Text-heavy 6.5 KB (50 paragraphs) | 22.50 µs | 21.96 µs | -2.4% |

The structural win (no per-text-node pool allocation + memcpy) shows up
at text volume — for a 5 MB body, the parse hot path no longer does a
5 MB memcpy. The small-doc wall-clock delta is in noise; the path is
now pugixml-shaped and the compounding wins will land with TODO 114
Phases 3–4 (pool-resident parser state, bulk attribute allocation).

Acceptance from the original plan: text-heavy ~1 KB under 10 µs — met
at 8.08 µs.

## Why

`leptris_text_create` originally did one pool allocation + memcpy per
text node even after TODO 114 Phase 1 eliminated the intermediate
buffer. For a 5 MB text body that's a 5 MB memcpy on the parse hot
path. pugixml stores text as a non-owning pointer into a mutable input
buffer + a length; the copy is avoided entirely.

## Plan

### Phase A — add content_len to text node (DONE, PR #114)
- Add `size_t content_len` to `LeptrisTextNode`.
- All existing constructors set `content_len = strlen(content)` to preserve NUL-terminated behavior.
- Foundation, no behavior change.

### Phase B — zero-copy constructor (DONE, PR #115)
- New `leptris_text_create_borrowed(content, len, pool)`:
  - Allocates only `sizeof(LeptrisTextNode)` from the pool — no content copy.
  - Stores pointer + length; content is NOT NUL-terminated.
- Parser uses this when `p->writable` and `has_entities == 0`.

### Phase C — update consumers (DONE, PR #115, same PR as B)
- Serializer: bound the loop by `content_len`, not NUL.
- `leptris_element_text`: route through `leptris_text_get_content` so materialization fires.
- `leptris_text_get_content`: when borrowed, lazily allocate a NUL-terminated copy on demand via the stored pool.
- Plus all other consumers found by audit: `element_modify.c` deep-copy paths, `xinclude.c deep_copy_node`, `cli/output.c xml_print_element_recursive`, `node_public.c leptris_text_node_get_content`.

### Phase D — acceptance (DONE, PR #116)
- Text-heavy doc: parse+free under 10 µs for ~1 KB — measured 8.08 µs.
- All existing tests pass — 320/320 (was 315 pre-TODO; +5 borrowed-text specs in `test/dom/test_text_borrowed.cpp`).
- Spec: parse `<r>hello world</r>` and verify text node is borrowed — `test_text_borrowed.cpp:ParsedTextNodeIsBorrowedFromInputBuffer`.

## Risk

Mixed NUL/non-NUL text content is error-prone. Mitigation: Phase A
shipped first (content_len field exists but always equals strlen), then
Phase B-C flipped the parser to the borrowed path with consumers
updated in the same PR.

The post-mortem consumer audit (xinclude.c deep_copy_node, cli/output.c
xml_print_element_recursive, element_modify.c deep-copy paths) caught
three sites that would have read past the buffer the moment the parser
emitted borrowed text — all fixed in PR #115.
