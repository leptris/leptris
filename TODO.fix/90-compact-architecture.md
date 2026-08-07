# TODO 90 — Compact-pointer migration (resumed)

**Priority**: P0 (user directive: "DO NOT STOP WITH PHASES. FINISH ALL THE WORK")
**Status**: Phase 1 + 2a complete; Phase 2b/2c/2d in flight
**Supersedes**: the "Phase 2b deferred" note in earlier 90 status updates.

## Current state — 104 bytes

```
TaurusNode base              8 B
TaurusCompactHeader header   2 B
uint8_t attr_count           1 B
uint16_t child_count         2 B
(pad)                        3 B
char* name                   8 B
char* prefix                 8 B
char* namespace_uri          8 B
struct taurus_element* parent          8 B   ← target of Phase 2b
struct taurus_node* first_child        8 B   ← target of Phase 2b
struct taurus_node* last_child         8 B   ← target of Phase 2b
struct taurus_node* next_sibling       8 B   ← target of Phase 2b
struct taurus_attribute* first_attribute  8 B   ← target of Phase 2d
struct taurus_attribute* last_attribute   8 B   ← target of Phase 2d
struct taurus_namespace* namespaces  8 B
struct taurus_document* document     8 B
                             ─────
                             104 B
```

## Phase plan

| Phase | Target | Saving | New size | Status |
|---|---|---|---|---|
| 1 | StringView + children_array removal | -64 B | 104 B | merged (PRs #82-85) |
| 2a | Field reorder, padding elimination | -8 B | 104 B | merged (PR #86) |
| 2b | Element tree pointers → int32_t offsets | -16 B | 88 B | in flight |
| 2c | Other node types' next_sibling → int32_t | -0 B (consistency) | 88 B | pending |
| 2d | Element attribute pointers → int32_t offsets | -8 B | 80 B | pending |
| 2e | String pointers → pool-relative offsets | -12 B | 68 B | stretch goal |

## Phase 2b: Element tree pointers as int32_t self-relative offsets

### Design

Each tree pointer becomes an `int32_t` holding the byte offset from the
element's own address to the target's address:

```c
int32_t parent_off;       /* 0 = NULL, else (char*)parent - (char*)this */
int32_t first_child_off;
int32_t last_child_off;
int32_t next_sibling_off;
```

Accessors compute the pointer on read:

```c
static inline TaurusElement taurus_elem_parent(const TaurusElement e) {
    if (!e || e->parent_off == 0) return NULL;
    return (TaurusElement)((const char*)e + e->parent_off);
}
```

### Why int32_t is safe

Pool allocator pages are 32KB each, allocated via malloc. On 64-bit
systems with ASLR, distinct malloc'd pages for a single document land
within ±2GB of each other in practice (a 2GB heap span for a single
document would be a 2GB document — far past any realistic workload).
A debug assert fires if a setter ever sees an offset that doesn't fit
in int32_t.

### Migration steps

1. Add inline accessors (`taurus_elem_parent`, `taurus_elem_set_parent`,
   etc.) to element.h.
2. Rename struct fields (`parent` → `parent_off`, etc.) so direct
   access breaks the build. The compiler enumerates every site.
3. For each compile error, replace `elem->parent` (read) with
   `taurus_elem_parent(elem)`, and `elem->parent = x` (write) with
   `taurus_elem_set_parent(elem, x)`.
4. Tighten the size static_assert from 112 → 88.
5. Run full test suite.

### NULL encoding

Offset 0 = NULL. Elements are pool-allocated and at least 8-byte
aligned, so no two distinct elements can have offset 0 between them.

## Phase 2c: Other node types follow suit

`struct taurus_text_node`, `taurus_comment_node`, `taurus_cdata_node`,
`taurus_pi_node` each carry a `void* next_sibling`. Convert each to
`int32_t next_sibling_off` and update `taurus_node_get_next_sibling()`
to dispatch on node type.

No size win on the element struct itself, but consistency: every
linked-list edge in the tree uses the same encoding, so traversal
code is uniform.

## Phase 2d: Attribute pointers

`first_attribute` and `last_attribute` become `int32_t` offsets.
Saves 8 bytes; new element size 80 bytes.

Attributes are also pool-allocated, same safety argument as Phase 2b.

## Phase 2e (stretch): String pointers

`name`, `prefix`, `namespace_uri` are pool-strdup'd char*. Could
become offsets into the document's string pool. Saves 12 bytes if
we use 4-byte offsets instead of 8-byte pointers.

Higher risk: requires looking up `document->pool->base` on every
string access. Hot path. Defer until Phase 2b/2c/2d land cleanly.

## Verification

After each phase:
- Full test suite (`ctest -j4`) — all tests pass.
- `_Static_assert(sizeof(struct taurus_element) <= N)` updated.
- `nm build-shared/src/libtaurus.dylib | grep " T "` still shows
  only `taurus_*` symbols (no accidental leakage).
- macOS `leaks --atExit -- ./build/test/c/test_dom` clean.
