# TODO 155 — Element struct compaction (88 → 64 bytes)

## Status

**ALL PHASES COMPLETE.** Element is now 64 bytes — fits one cache line.

- Phase B: v0.15.0 (88→80, merged namespaces into ns_cache)
- Phase C: v0.16.0 (80→72, dropped last_child_off + last_attribute_off)
- Phase A: v0.17.0 (72→64, dropped document field + root→doc hash)

## Phase A — Drop `document` field (72 → 64 bytes, fits one cache line)

### Why it's hard

The field is read in ~53 sites across 14 files. Most reads are
simple (`elem->document` → `leptris_element_get_document(elem)`), but
the `leptris_element_*_copy` family in element_modify.c recurses
deeply, reading `parent->document->pool` on every iteration.

### Refined design (after two failed attempts)

**Architecture**:

```
struct leptris_element {
    /* ... 56 bytes of existing fields ... */
    /* NO document field. 64 bytes total. */
};

/* New file: dom/root_doc_map.{c,h} */
struct leptris_document* leptris_element_get_document(LeptrisElement);
LeptrisMemoryPool*        leptris_element_get_pool(LeptrisElement);
```

Internally, `leptris_element_get_document(elem)` walks `parent_off`
to the root element, then looks up the root in a thread-local hash
table (`g_root_doc_buckets[256]`).

**Registration lifecycle**:
- Parse: `leptris_root_doc_register(root, doc)` at end of `direct_parse_internal`.
- `leptris_element_create_doc`: register the new element as a "root"
  of its doc. When later attached to a tree via `append_child`, the
  registration becomes stale but is never consulted (walks go past
  this element to the actual tree root).
- `leptris_document_free`: `leptris_root_doc_unregister(root)` before
  destroying pool.

**Copy-function refactor** (the hard part):

The `leptris_element_append_copy`, `prepend_copy`, `insert_copy_*`
functions recurse and read `parent->document->pool` on every step.
With the field gone, each recursion would walk to root — O(depth)
per node, O(N × depth) total.

Refactor: add internal recursive helpers that take an explicit
`pool` parameter:

```c
static LeptrisElement element_copy_internal(
    LeptrisElement parent, LeptrisElement source,
    LeptrisMemoryPool* pool, struct leptris_document* doc);
```

The PUBLIC API computes `pool` and `doc` ONCE via
`leptris_element_get_pool(parent)`, then threads them through.

### Migration plan (sub-PRs)

Each sub-PR is independently testable. Land in sequence:

**Sub-PR 1**: Add `dom/root_doc_map.{c,h}` with helpers that
_initially_ just return `elem->document`. No field removal yet.
Migrate the simple read sites (element.c, element_query.c,
element_compact.c, node_public.c, serialize.c, c14n.c,
leptris_memory.c, leptris.c). No behavior change. ~25 sites.

**Sub-PR 2**: Refactor `leptris_element_*_copy` in element_modify.c
to use internal helpers with explicit pool/doc parameters. ~12
sites in the copy family + ~10 other writes in element_modify.c.

**Sub-PR 3**: Add register/unregister calls (parse, create_doc,
document_free). Switch `leptris_element_get_document` from returning
`elem->document` to walk + hash lookup. Field stays for now;
both old and new paths work.

**Sub-PR 4**: Remove the field. All reads go through helpers.
Static assert: `sizeof(leptris_element) == 64`. Minor version bump.

### Estimated impact

For a 1000-element doc, walking the tree touches each element once.
With 72-byte elements, each spans 2 cache lines (1.13 lines). With
64-byte elements, each fits in 1 line. Cache line fetches drop from
~1130 to 1000 per traversal. ~12% reduction in cache traffic.

Combined with single-arena allocation (TODO 154) and SIMD parse
loops (TODO 157), the medium-doc parse target is ~25 µs (vs
pugixml's 18 µs) — within 1.4× of pugixml.

## Why

Each element is **88 bytes** today; pugixml's node is **44 bytes**.
Two consequences:

1. **Cache footprint**: 88-byte elements span 2 cache lines (64 B
   each). Every first-touch fetches 2 lines. pugixml's 44-byte
   elements fit in 1 line. On a 24 KB medium doc with ~1000 elements
   that's 1000 extra cache line fetches = ~50 µs at 50 ns/line.
   Half the gap vs pugixml comes from this alone.
2. **Memory bandwidth**: walking the tree touches 2× the bytes.

## Current layout (88 bytes)

```
LeptrisNode base              (24)  type, frozen, version, line, binding_wrapper
LeptrisCompactHeader header    (2)
uint8_t attr_count            (1)
uint16_t child_count          (2)
                             (3 pad)
char* name                    (8)
struct leptris_ns_cache* ns    (8)
int32_t parent_off            (4)
int32_t first_child_off       (4)
int32_t last_child_off        (4)
int32_t next_sibling_off      (4)
int32_t first_attribute_off   (4)
int32_t last_attribute_off    (4)
struct leptris_namespace* ns2  (8)   ← redundant w/ ns_cache
struct leptris_document* doc   (8)
```

`namespaces` (the linked-list head) overlaps with `ns_cache` in
role — both carry namespace info. Storing both is 16 bytes wasted
on the common case where the element has zero or one namespace.

## Plan

### Phase A — Eliminate `document` pointer (saves 8 bytes)

Every element is reachable from its document's `new_dom_root`. The
document pointer is only used to:

- Walk back to the doc for `leptris_element_get_document(elem)` (public API).
- Look up custom XPath functions during evaluation.
- Get the pool/allocator for new children (mutation API).

**Strategy**: store the document pointer only on the **root**
element. Non-root elements walk up via `parent_off` until they hit
the root. Cost: O(depth) for document lookups. Depth ≤ 32 in
practice (XML is rarely deeply nested). The hot path (iteration,
XPath) doesn't need document access.

Add a `is_root` flag in `header.flags` so the walk stops at root.

### Phase B — Merge `namespaces` linked list into `ns_cache` (saves 8 bytes)

Replace the `namespaces` pointer + `ns_cache` pointer with a single
`struct leptris_ns_cache*` that carries prefix, URI, AND a linked
list of additional namespaces (rare case). Elements without
namespaces pay nothing (NULL pointer). Elements with one namespace
pay 16 bytes (one ns_cache alloc, ~free from pool). Elements with
multiple namespaces chain via ns_cache->next.

### Phase C — Drop `last_child_off` + `last_attribute_off` (saves 8 bytes)

Append-heavy workloads use `last_child_off` for O(1) append. But:

- The parse path always appends, so it sets `last_child_off` once
  per child. We can replace `append` with `append to head + reverse`
  in O(1) per element. The reversal happens at the end of parsing.
- For mutation: pay O(child_count) on append. Most appends happen
  during construction; the cost amortizes.

Net: 16 bytes saved (drop last_child AND last_attribute), offset by
~5% slowdown on append-heavy mutation benchmarks.

Alternative: keep `last_child_off` as a 16-bit offset (saves 2 bytes
instead of 4). Use 16-bit when child_count < 32k, fall back to
32-bit via overflow flag otherwise.

### Phase D — `line` becomes `uint16_t` (saves 2 bytes)

`base.line` is `uint32_t` (4 bytes). Real-world XML rarely exceeds
65535 lines. Compress to uint16_t with an overflow sentinel (0xFFFF
means "line unknown / > 64k"). Most parsed docs have lines < 64k.

### Combined effect

| Phase | Bytes saved | Running total |
|-------|-------------|---------------|
| A     | 8           | 80            |
| B     | 8           | 72            |
| C     | 8           | 64            |
| D     | 2           | 62 (+2 pad to 64) |

Target: **64 bytes**. Fits in one cache line.

## Risk

- **ABI**: Element struct size changes. Major version bump required.
- **Validation**: every `elem->document`, `elem->namespaces`,
  `elem->last_child_off` access must be audited and rewritten.
- **Performance regression**: dropping `last_child_off` slows down
  `append_child`. Mitigation: keep last_child as 16-bit offset
  in Phase C.

## Expected impact

Medium (24 KB) doc, 1000 elements:

- 1000 fewer cache line fetches → ~50 µs saved
- Plus reduced memory bandwidth → another ~20 µs

Target: medium doc 132 µs → ~60 µs (parity with pugixml's 20 µs
within 3×; combined with SIMD TODO 157 → ~25 µs).

## Status

Pending. Each phase is a separate PR.

