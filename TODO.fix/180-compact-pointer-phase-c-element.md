# TODO 180 — Compact pointer Phase C (element tree migration)

**Priority**: P0 (biggest single lever; closes 1.5–2× of the gap)
**Status**: **BLOCKED** — see "Design constraint discovered" below

## Goal

Migrate `struct leptris_element`'s tree pointers (`parent_off`,
`first_child_off`, `last_child_off`, `next_sibling_off`,
`first_attribute_off`) from `int32_t` self-relative offsets to
`compact_pointer_1byte` with overflow side-table. Element struct
shrinks 64 → ~24 bytes (3 cache lines saved per walk step).

## Design constraint discovered (2026-08-14)

**Attempted migration to cp16 (±256 KB range) failed three tests:**
- `SerializeRoundTrip.GrowsBufferForHugeTextContent` — silent data loss
- `SerializeRoundTrip.HugeTextContentStaysAttachedToParent` — silent data loss
- `HighDocCountStress.ParseVerifyFree5000Docs` — segfault

**Root cause**: leptris's pool allocator (`src/leptris/memory/pool.c`)
allocates pages of 32 KB each via `malloc`. Distinct malloc'd pages
for one document can land **megabytes apart** in the process address
space (especially on macOS ASLR and Linux glibc). Element tree edges
span the whole document — easily exceeding cp16's ±256 KB range
when the document has > ~500 elements (one 32 KB pool page worth).

When the encoder silently truncates the offset to int16, decode
returns a wrong pointer, corrupting the tree.

**Phase B (TODO 179) doesn't have this problem** because text/comment/
cdata/pi sibling chains typically fit in one pool page (siblings are
allocated sequentially after the parent). Element tree edges span
pages.

**direct_parse.c is overflow-table-free by design** (line 672) to
avoid cross-document contamination under high doc counts (issue #261).
Using the overflow table from direct_parse would reintroduce that bug.

## Unblocking options

### Option A — Contiguous pool arena (preferred)

Redesign `LeptrisMemoryPool` to allocate one contiguous arena per
document (single `malloc` for the whole doc), bump-pointer within.
Tree edges stay within the arena, ±256 KB covers docs up to 256 KB
(extendable to int32 for larger docs).

Trade-off: must estimate arena size up front (or grow via realloc
which may relocate — invalidating all pointers). pugixml does this
via its `xml_allocator` which pre-allocates pages and chains them.

### Option B — Per-document overflow table

Add `struct leptris_pointer_overflow* ptr_overflow` to `LeptrisDocument`.
Lazy-allocated on first overflow. `direct_parse.c` calls
`leptris_compact_set_current_document(doc)` at parse start; the
overflow table tags entries by document for safe cleanup.

Trade-off: re-introduces overflow-table cost on the parse hot path.
Per-doc table allocation + cleanup adds overhead. Issue #261's
concern (cross-document contamination) is solved by per-doc tables
but at memory cost.

### Option C — Hybrid: cp16 for first 256 KB, int32 fallback

Detect at parse time whether the doc fits in 256 KB. If yes, use
cp16 throughout (one code path). If no, use int32 throughout.
Branch at parse start, no per-edge dispatch.

Trade-off: simpler than A/B but doesn't help with large docs.

## Recommendation

**Defer Phase C until Option A (contiguous arena) is implemented.**
Option A is the right architectural fix — it eliminates the
overflow-table complexity entirely and matches pugixml's allocator
design. Track as a prerequisite TODO.

In the meantime, Phase D (TODO 181) and Phase E (TODO 182) can
proceed in limited form:
- Phase D attribute list: attributes are allocated in one block
  (`attr_block` in direct_parse), so they're contiguous. cp16 works
  for the attribute `next` field.
- Phase E compact_string: per-document string pool can be contiguous
  by design.

## Estimated impact (when unblocked)

1.5–2× on tree-walk benchmarks (`bench_dom_leptris` traversal).
Likely 1.3–1.5× on K=100 attr benchmark (attr struct still 72 B
until [[181-compact-pointer-phase-d-attributes]]).

## Risk

**HIGH**. Overflow path correctness must be bulletproof. pugixml
has had bugs in this area. Mitigations:

1. Adversarial test inputs must pass.
2. ASAN + UBSAN clean across all tests.
3. Memory-leak check on macOS (`leaks --atExit`) and Linux (valgrind).
4. Fuzz test (existing `test/fuzz/`) for 10M iterations before merge.

## References

- Depends on: [[178-compact-pointer-phase-a]], [[179-compact-pointer-phase-b-nodes]]
- Blocked on: contiguous pool arena (Option A)
- Next: [[181-compact-pointer-phase-d-attributes]]
- Background: [[149-pugixml-architecture-study]], [[169-compact-1-byte-in-page-pointers]]
