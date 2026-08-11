# TODO 155 — Element struct compaction (88 → 64 bytes)

## Status

**Phase B DONE** in v0.15.0 (88→80 bytes).
**Phase C DONE** in v0.16.0 (80→72 bytes).
**Phase A pending** (drop `document` field, 72→64 bytes).
**Phase D pending** (line: uint32 → uint16, save 2 bytes — modest).

Current: **72 bytes** (was 88). Target: **64 bytes** (fits one cache line).

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
TaurusNode base              (24)  type, frozen, version, line, binding_wrapper
TaurusCompactHeader header    (2)
uint8_t attr_count            (1)
uint16_t child_count          (2)
                             (3 pad)
char* name                    (8)
struct taurus_ns_cache* ns    (8)
int32_t parent_off            (4)
int32_t first_child_off       (4)
int32_t last_child_off        (4)
int32_t next_sibling_off      (4)
int32_t first_attribute_off   (4)
int32_t last_attribute_off    (4)
struct taurus_namespace* ns2  (8)   ← redundant w/ ns_cache
struct taurus_document* doc   (8)
```

`namespaces` (the linked-list head) overlaps with `ns_cache` in
role — both carry namespace info. Storing both is 16 bytes wasted
on the common case where the element has zero or one namespace.

## Plan

### Phase A — Eliminate `document` pointer (saves 8 bytes)

Every element is reachable from its document's `new_dom_root`. The
document pointer is only used to:

- Walk back to the doc for `taurus_element_get_document(elem)` (public API).
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
`struct taurus_ns_cache*` that carries prefix, URI, AND a linked
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

