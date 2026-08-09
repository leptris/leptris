# TODO 149 — pugixml architecture study (reference doc)

## Purpose

A reference document distilling the engineering choices that let
pugixml hit ~5 µs parse on a 5 KB plain XML doc, vs our current
~140 µs on 24 KB (~18 µs per pugixml). Future perf work cites
this when adopting techniques.

## pugixml core techniques (as of v1.16)

### 1. Compact node layout — 44 bytes per node

```cpp
struct xml_node_struct {
    xml_node_struct* parent;            // 8
    xml_node_struct* first_child;       // 8
    xml_node_struct* prev_sibling_c;    // 8  (last_child if first)
    xml_node_struct* next_sibling;      // 8
    char_t* name;                       // 8
    char_t* value;                      // 8
    uintptr_t header;                   // 8  (type + attributePageSize)
};
```

vs our 88-byte element. Key differences:
- No `child_count` / `attr_count` (computed lazily)
- No `document` pointer on every node (single doc owns the tree)
- No namespace list head (namespaces are first-class children)
- No frozen/version bitfield

We can't compress to 44 bytes without losing
`child_count` (man-page contract) and the COW version field. But
we can drop `document` (8 bytes) by recovering it from a
document-keyed side table.

### 2. Single allocation arena

pugixml's `xml_allocator`:
- Allocates pages of 32 KB by default.
- Each page is bump-pointer; no per-alloc metadata.
- Oversized allocations fall back to a separate page-sized block.
- All nodes from one document share the arena.

Our pool is the same model (TODO 26). The gap is element size,
not allocator overhead.

### 3. In-place NUL termination

```cpp
chartype_t* name = cursor;
while (IS_CHARTYPE(*cursor, ct_symbol)) ++cursor;
char_t delim = *cursor;
*cursor = '\0';  // NUL-term in place
// ... later: *cursor = delim; (restore)
```

We do this. direct_parse NUL-terminates names after the open-tag
scan completes; attr names after `=` is consumed; values at the
closing quote.

### 4. Lookup tables for char classification

pugixml has one shared `ct_*` enum and one 256-byte chartype
table. Tests the bit AND the type in one indexed access.

```cpp
enum chartype_t {
    ct_parse_pcdata = 1, ct_parse_attr = 2, ct_parse_ws = 4,
    ct_symbol = 8, ct_start_symbol = 16, ct_digit = 32,
    ct_space = 64, ...
};
extern unsigned char g_chartype_table[256];
#define IS_CHARTYPE(c, t) (g_chartype_table[(unsigned char)c] & (t))
```

vs our two separate tables (`dp_name_char_lut`,
`dp_name_start_lut`, `dp_ws_lut`) in direct_parse and ANOTHER set
in flat_parser. DRY violation.

**Action:** consolidate into one shared chartype table with bitflags.

### 5. Computed-goto state machine (parser_pragen)

pugixml's `parse_*` functions use computed goto (`__builtin_cpu_supports`
aside, the technique is labels-as-values + dispatch table):

```cpp
static const void* g_parse_dispatch[256] = {
    ['<'] = &&L_angle, ['&'] = &&L_amp, ...
};
goto *g_parse_dispatch[(unsigned char)*cursor];
L_angle: ...
```

Eliminates the switch's predictable branch mispredict (~1 cycle
per dispatch). Net ~5% on hot loop.

We use switch. **Action:** experiment with computed goto in
`direct_parse.c`'s main scan loop.

### 6. Precompiled header + LTO

pugixml ships with PCH that includes `<pugixml.hpp>` body, and
builds with LTO. This inlines the parser loop into a single
function with no call overhead.

CMake has `target_precompile_headers`. Worth adopting.

### 7. Text whitespace collapsing

pugixml's `parse_ws_pcdata` mode collapses adjacent whitespace
runs in the parser, not the serializer. Reduces text-node count
for docs with significant indentation.

### 8. No exceptions, no RTTI

pugixml compiles with `-fno-exceptions -fno-rtti`. Smaller code,
faster unwinding. We already do this (C99).

## Where we're already better

- DOM mutation API (pugixml's is awkward — parent/child rewiring
  is manual).
- XPath bytecode VM (TODO 120) — pugixml has no XPath.
- Compact pointer encoding (TODO 90) — pugixml uses raw 8-byte
  pointers, we use 4-byte int32 offsets.

## Where the 8x gap actually comes from

Measured on 24 KB plain XML, ~2300 attrs:

| Stage                      | pugixml | libtaurus | Δ      |
|----------------------------|---------|-----------|--------|
| Bulk node alloc           | ~5 ns   | ~5 ns     | same   |
| Per-node init             | 5 ns    | 30 ns     | +25 ns |
| Per-attr struct           | 5 ns    | 80 ns     | +75 ns |
| Per-attr name/value copy  | 0 (zero-copy) | 0 (zero-copy in direct_parse) | same |
| Per-element wire (parent + sibling offsets) | 10 ns | 10 ns | same |
| String interning          | 0       | 0 (direct_parse skips) | same |
| **Total per element**     | **25 ns** | **125 ns** | +100 ns |

× 600 elements = 60 µs of the 75 µs gap. Per-attr is dominant.

## Phase ordering

1. **Phase 1 — Chartype consolidation** (DONE v0.6.2): shared
   `taurus_chartype_table` with CT_NAME_START / CT_NAME / CT_WS flags.
   direct_parse and flat_parser migrated; binary shrinks by 3 × 256 B
   per TU.
2. **Phase 2 — Computed-goto dispatch** (ANALYZED, DEFERRED): the
   `<`-dispatch in direct_parse has only 5 cases (NAME_START, `/`,
   `!`, `?`, default). For a 21K-line doc (~5K tags), that's ~25K
   dispatches ≈ 8 µs total at 1 cycle/dispatch. Computed-goto would
   save at most 0.5 cycle/dispatch ≈ 12 µs — unmeasurable. The hot
   paths (name scan via LUT, text scan via memchr) are already faster
   than scalar computed-goto. Not worth the complexity.
3. **Phase 3 — Legacy parser chartype migration** (DONE): parser_new.c
   now routes ASCII fast-path of `parser_is_name_start_inline` /
   `parser_is_name_char_inline` / `parser_is_whitespace_inline`
   through the shared table. UTF-8 multibyte fallback preserved.
4. **TODO 150 Phase 2e** — Compress element struct from 80 → 72 bytes.
   See TODO 150 for the deferral rationale (Phase 2e-A needs an
   alternative O(1) doc-lookup mechanism; Phase 2e-B requires
   namespace-on-attrs refactor with c14n regression risk).

## Status

Phases 1 and 3 shipped. Phase 2 deferred with measured justification
(the dispatch is too narrow and the hot paths already use faster
techniques). Phase 4 tracked under TODO 150.
