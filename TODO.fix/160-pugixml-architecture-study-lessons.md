# TODO 160 — Pugixml architecture study: lessons and adoption

## Why

The previous TODOs (154-159) attack specific bottlenecks. This one
captures the **architectural lessons** from studying pugixml so we
can apply them holistically, not just micro-optimize.

Pugixml is the gold standard for fast XML parsing. It's a ~6000-line
header-only library. Beating it means matching its design
decisions, not just copying its code.

## pugixml's architectural principles

### 1. One allocation per document

pugixml's `xml_document::load_buffer` does **one** malloc for the
entire document — the document struct, all nodes, all attributes,
and all string storage live in one heap block. No per-node malloc.
No pool. No free-list.

Our pool allocator is close but still does 2 mallocs (pool struct +
first page). TODO 154 fixes this.

### 2. In-place NUL termination

pugixml modifies the caller's buffer in-place, NUL-terminating at
token boundaries. No string copies. Names are pointers into the
input buffer.

Our `direct_parse` already does this. ✓

### 3. Compact node storage

pugixml's `xml_node_struct` is 40-44 bytes. Ours is 88. TODO 155
fixes this. The main reductions:

- Drop `document` pointer (walk to root).
- Merge namespace caches.
- Use 16-bit sibling offsets where possible.
- Drop `last_child_off` (pay O(child_count) on append).

### 4. Single node-type struct

pugixml has ONE struct for all node types. Type is a field.
Text/comment/CDATA/PI reuse the same memory layout (the `value`
field stores the text content).

We have separate structs (LeptrisElement, LeptrisTextNode,
LeptrisCommentNode, etc.). They share the LeptrisNode base but have
different layouts beyond it. This forces type-dispatched tree
walks (slow).

Adopting pugixml's single-struct model would unify all node types.
Major refactor; defer until TODO 155 + 156 land.

### 5. SSE2 in the parse hot path

pugixml's whitespace skip and name scan use SSE2. The gain is 4-16×
on those loops. We have the scaffolding; TODO 157 integrates it.

### 6. Monomorphic XPath

pugixml's XPath uses computed-goto dispatch with tight per-opcode
loops. No virtual functions, no allocator-per-result, no string
interning. TODO 159 adopts these.

### 7. Header-only hot functions

pugixml's hot accessors are `force_inline` in the header. Always
inlined. TODO 158 adopts this.

### 8. No exceptions, no RTTI

pugixml is pure C++ without exceptions or RTTI. Smaller, faster.

Our C codebase already meets this. ✓

## What pugixml does NOT do (we should keep doing)

### 1. DTD validation
pugixml doesn't validate against DTDs. We do (TODO 91 Phase 8
pending). Keep this — it's a feature, not a perf cost on most docs.

### 2. Entity expansion
pugixml has limited entity support. We do full DTD entity
expansion. Keep.

### 3. XPath variables
pugixml has variable support but limited. Ours is more flexible.

### 4. C14N canonicalization
pugixml lacks C14N. We have it.

### 5. Per-node binding wrappers (FFI)
pugixml's C API doesn't cache FFI wrappers. We do (#262). Critical
for Ruby FFI performance.

## Plan

This is a study document, not actionable work. Each item references
the TODO that implements the lesson.

| Lesson | TODO |
|--------|------|
| One allocation | 154 |
| Compact storage | 155 |
| Compact attr storage | 156 |
| SSE2 in hot path | 157 |
| Inline accessors | 158 |
| Monomorphic XPath | 159 |

## Status

Reference. Update as lessons evolve.
