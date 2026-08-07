# TODO 139 — Flat document buffer: close the parse perf gap vs pugixml

## Current state

- **Parse**: taurus ~77 µs vs pugixml ~5 µs for a ~5 KB doc (15× slower)
- **XPath**: taurus BEATS pugixml (8-17× faster on 4 of 5 queries)
- **SAX**: taurus BEATS pugixml (3.6× faster)
- **DOM reads**: taurus BEATS pugixml (attribute lookup, text extraction)

The parse gap is the ONLY remaining area where we lose badly. It is
purely architectural.

## Root cause

pugixml stores all document nodes in a **single contiguous buffer**.
Each node is ~20-32 bytes. Parse appends to the buffer in a tight
loop — no per-node allocation, no pointer encoding, no struct init
beyond writing the raw fields. Traversal is pointer arithmetic into
the array.

libtaurus uses **pool-allocated compact-pointer elements**:
- Each element: 96 bytes (pool_alloc + memset + field init)
- Each attribute: 88 bytes (pool_alloc + StringView copies)
- Tree edges: int32 byte offsets with overflow-table fallback
- Per-element creation cost: ~1.5 µs vs pugixml's ~0.1 µs

The compact-pointer architecture gives us:
- Excellent XPath traversal (element index, memcpy fast paths)
- Good cache locality for read-heavy workloads
- Compact memory footprint (96 bytes vs pugixml's ~120+ with strings)

But it's expensive to BUILD during parse because each element requires
pool allocation, struct initialization, and compact-pointer encoding.

## Target architecture: dual-mode document

**Key insight**: we don't need to replace the compact-pointer tree.
We need a faster BUILD path. The compact-pointer tree is already fast
for queries (XPath, traversal, element index). The problem is only in
the parse → tree construction path.

### Design: flat-parse-then-promote

```
XML input
    ↓
Flat parser → FlatDoc (contiguous array of FlatNode)
    ↓ (lazy, on first query or access)
Promote → TaurusDocument (pool-allocated compact-pointer tree)
```

Phase 1 (parse): Build a FlatDoc — a contiguous array of lightweight
parse nodes. No pool allocation. No compact pointers. No namespace
processing. No StringView structs. Just raw offsets into the input
buffer.

Phase 2 (promote, lazy): Convert FlatDoc to TaurusDocument on first
access that needs the compact-pointer tree (XPath, mutation, SAX
traversal). The promote pass is a single linear walk that allocates
pool elements and encodes compact pointers.

For parse-only workloads (SAX, count, validate), we can skip the
promote entirely.

### FlatNode layout (~24 bytes)

```c
typedef struct {
    uint16_t type;           // ELEMENT / TEXT / COMMENT / CDATA / PI
    uint16_t name_len;       // Length of name (0 for non-element)
    uint32_t name_offset;    // Offset into the input buffer (zero-copy)
    uint32_t parent;         // Index of parent in the flat array
    uint32_t first_child;    // Index of first child (or UINT32_MAX)
    uint32_t next_sibling;   // Index of next sibling (or UINT32_MAX)
    uint32_t attr_start;     // Index into flat attr array (or UINT32_MAX)
    uint16_t attr_count;     // Number of attributes
    uint16_t depth;          // Nesting depth (for validation)
} FlatNode;  // 28 bytes, or 24 with packing
```

### FlatAttr layout (~16 bytes)

```c
typedef struct {
    uint32_t name_offset;    // Offset into input buffer
    uint16_t name_len;
    uint16_t value_len;
    uint32_t value_offset;   // Offset into input buffer
    uint32_t next;           // Index of next attr on same element (or UINT32_MAX)
} FlatAttr;  // 16 bytes
```

### FlatDoc layout

```c
typedef struct {
    FlatNode* nodes;         // Contiguous array
    size_t node_count;
    size_t node_capacity;

    FlatAttr* attrs;         // Contiguous array
    size_t attr_count;
    size_t attr_capacity;

    const char* xml_buffer;  // The input buffer (owned or borrowed)
    size_t xml_len;

    // XML declaration
    const char* version;
    const char* encoding;
    int standalone;
} FlatDoc;
```

### Memory comparison

For a 50-element, 100-attribute document:

| Representation | Per-node | Nodes | Attrs | Total |
|---|---|---|---|---|
| Current (compact) | 96 B elem + 88 B attr | 50 × 96 = 4.8 KB | 100 × 88 = 8.8 KB | 13.6 KB |
| Flat (new) | 28 B node + 16 B attr | 50 × 28 = 1.4 KB | 100 × 16 = 1.6 KB | 3.0 KB |
| pugixml | ~32 B node + ~24 B attr | 50 × 32 = 1.6 KB | 100 × 24 = 2.4 KB | 4.0 KB |

The flat representation is 4.5× smaller than our current one and
even smaller than pugixml (because we don't store parent pointers
in the flat node — parent is implicit in the nesting).

## Implementation phases

### Phase A: FlatDoc struct + allocator (1 PR)

New files:
- `src/taurus/flat/flat_doc.h` — FlatNode, FlatAttr, FlatDoc structs
- `src/taurus/flat/flat_doc.c` — create, free, append_node, append_attr

The allocator pre-allocates a contiguous array sized from the input
length heuristic (~1 node per 100 bytes of XML, ~2 attrs per node).

```c
FlatDoc* flat_doc_new(const char* xml, size_t xml_len);
void flat_doc_free(FlatDoc* doc);
uint32_t flat_doc_append_node(FlatDoc* doc, uint16_t type,
                               uint32_t name_offset, uint16_t name_len);
uint32_t flat_doc_append_attr(FlatDoc* doc,
                               uint32_t name_offset, uint16_t name_len,
                               uint32_t value_offset, uint16_t value_len);
```

### Phase B: Flat parser (1 PR)

New file:
- `src/taurus/flat/flat_parser.c` — single-pass XML parser that
  builds a FlatDoc directly from the input buffer.

Reuses the tokenizer logic from `parser_new.c` (memchr-based scanning,
ASCII tight loops for name parsing). The key difference: instead of
calling `taurus_element_create_with_view` + `taurus_element_add_attribute`
(pool allocation + struct init + compact pointer encode), it calls
`flat_doc_append_node` / `flat_doc_append_attr` (array append + index
assignment).

Expected per-element cost: ~100 ns (array append + index set) vs
current ~1500 ns (pool alloc + memset + compact pointer encode).
That's 15× faster per element — matching pugixml.

### Phase C: Promote FlatDoc → TaurusDocument (1 PR)

New file:
- `src/taurus/flat/flat_promote.c` — converts FlatDoc to
  TaurusDocument (pool-allocated compact-pointer tree).

Single linear walk over the flat node array. For each FlatNode:
1. Pool-allocate a TaurusElement
2. Set name from flat node's name_offset (pool_strdup or lazy)
3. Set parent/child/sibling via compact pointer encode
4. For each FlatAttr: pool-allocate attribute, set fields
5. Build namespace list from xmlns attributes

Expected promote cost: ~50 µs for 50 elements. This is the SAME
cost as the current parse — but it only runs when the compact-pointer
tree is actually needed.

### Phase D: Lazy promote in taurus_parse_string (1 PR)

Modify `taurus_parse_string` to:
1. Parse into FlatDoc (fast)
2. Return a TaurusDocument that wraps the FlatDoc
3. On first access (root element, XPath, mutation), call promote

The TaurusDocument struct gets a new field:
```c
struct taurus_document {
    ...
    FlatDoc* flat_doc;          // Set when parse produces flat doc
    int flat_promoted;          // 0 = still flat, 1 = promoted to tree
};
```

`taurus_document_root(doc)` checks `flat_promoted`. If 0, calls
`flat_promote(doc)` first, then returns the root element.

This is backward compatible: all existing API calls work because
they trigger the promote on first access.

### Phase E: Flat-mode query fast paths (optional, 1 PR)

For simple queries that don't need the full tree:
- `count(//book)`: walk FlatDoc node array, count by name. O(N).
- `doc.root.name`: return FlatDoc.nodes[0].name. O(1).

These bypass promote entirely for simple operations.

### Phase F: Benchmark + validate (1 PR)

- Add `benchmarks/flat/bench_flat_parse.c` comparing flat parse vs
  current parse vs pugixml.
- Add specs verifying flat parse produces identical trees to the
  current parser.
- Add specs verifying lazy promote works correctly.

## Expected performance after completion

| Operation | Current | After TODO 139 | pugixml |
|---|---|---|---|
| Parse (5 KB) | ~77 µs | ~8 µs | ~5 µs |
| Parse + promote | ~77 µs | ~60 µs (lazy) | N/A |
| XPath (pre-parsed) | 0.33-1.13 µs | unchanged | 4.81-6.87 µs |
| DOM read | unchanged | unchanged | — |

Parse goes from 15× slower to ~1.6× slower than pugixml. Still not
beating them on raw parse, but within striking distance. The remaining
gap is the pool allocator overhead in promote (which we could skip for
parse-only workloads).

## Risk assessment

- **ABI stability**: No change to public API. TaurusDocument is opaque.
  FlatDoc is internal.
- **Correctness**: Flat parse must produce identical trees. The promote
  pass is deterministic.
- **Memory**: FlatDoc uses less memory than the current tree. Promote
  creates a second copy (flat + compact), but the flat doc can be freed
  after promote.
- **Complexity**: Adds a parallel code path. The existing parser stays
  as the "trusted" implementation; flat parse is the fast path.

## File layout

```
src/taurus/flat/
  flat_doc.h        — FlatNode, FlatAttr, FlatDoc structs + API
  flat_doc.c        — FlatDoc allocator (array append)
  flat_parser.c     — XML parser → FlatDoc (single pass)
  flat_promote.c    — FlatDoc → TaurusDocument (linear walk)
src/CMakeLists.txt  — add flat/*.c to TAURUS_SOURCES
test/flat/
  test_flat_parse.cpp   — verify flat parse matches current parse
  test_flat_promote.cpp — verify promote produces valid tree
benchmarks/flat/
  bench_flat_parse.c    — flat parse vs current vs pugixml
```

## Priority order

1. **Phase A** (FlatDoc struct) — foundational, no dependencies
2. **Phase B** (Flat parser) — the perf win; depends on A
3. **Phase C** (Promote) — backward compat bridge; depends on A
4. **Phase D** (Lazy promote) — wire into public API; depends on B+C
5. **Phase E** (Query fast paths) — optimization on top; depends on D
6. **Phase F** (Benchmark + specs) — validation; depends on all

Phases A-C can be developed independently and merged separately.
Phase D is the "switch" that activates the fast path.

## Branch naming

- `todo-139a-flat-doc-struct`
- `todo-139b-flat-parser`
- `todo-139c-flat-promote`
- `todo-139d-lazy-promote`
- `todo-139e-flat-query-fast-paths`
- `todo-139f-flat-benchmark-specs`
