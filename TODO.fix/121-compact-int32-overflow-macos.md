# TODO 121 — Compact-pointer int32 overflow on macOS

**Priority**: P2 (blocks macOS CI for XInclude + compact tests)
**Status**: Open. Surfaced 2026-08-06.

## Why

The compact-pointer system (TODO 90) stores tree edges (parent,
first_child, last_child, next_sibling) as `int32_t` byte offsets
relative to the node's own address. This works when pool-resident
nodes live within ±2GB of each other — true on Linux where malloc
clusters pool pages tightly.

On macOS, ASLR + malloc scatter pool pages such that two nodes can
end up more than 2GB apart. When this happens, the encode side
detects the overflow and **silently drops the edge** (sets offset
to 0, meaning NULL). The decode side then returns NULL for what
should be a valid edge.

Result: trees lose edges on macOS. XInclude's ownership-transfer
code (TODO 117 Phase A) splices adopted nodes into the parent tree
via these edges; on macOS, the splice silently fails and
`first_child_any(root)` returns NULL.

Affected tests on macOS CI:
- `CompactPointerIntegration.DeepNestingTraversesViaOffsets`
- `XIncludeProcess.ParseXmlReplacesIncludeWithRootElement`
- `XIncludeProcess.ParseXmlCopiesAttributesAndChildren`
- `XIncludeProcess.ParseXmlRecursiveIncludesNestedXi`
- `XIncludeXpointer.SelectsFragmentByXPath`
- `XIncludeXpointer.EmptyResultFallsBackToRoot`
- `XIncludePhaseA.AdoptedRootHasParentDocPointer`

Linux + ASAN are unaffected.

## Current state

Ten encode sites silently drop on overflow:

```
src/taurus/dom/element.h:193,204,215,226,253,261
src/taurus/dom/text.h:94
src/taurus/dom/comment.h:49
src/taurus/dom/cdata.h:49
src/taurus/dom/pi.h:56
```

Each has the same pattern:
```c
ptrdiff_t d = (char*)target - (char*)base;
if (d < INT32_MIN || d > INT32_MAX) { off = 0; return; }
off = (int32_t)d;
```

The 8-bit compact pointer system (`compact_ptr8`) has the same
problem but solved it via `taurus_compact_overflow_set` /
`taurus_compact_overflow_get` — keyed on the field's address,
stored in a global hash table that's cleaned up per-document.

## Plan

### Phase A — extend overflow API for int32

Add public-to-the-DOM-subsystem functions in `compact.c`:

```c
int32_t taurus_compact_int32_encode(void* base, void* target,
                                     const int32_t* field_addr);
void*   taurus_compact_int32_decode(void* base, int32_t off,
                                     const int32_t* field_addr);
```

Encode: if offset fits, return it. Else, register in the overflow
table keyed on `field_addr`, return `INT32_MIN` as a sentinel.

Decode: if `off == 0`, return NULL. If `off == INT32_MIN`, consult
the overflow table. Else, return `(char*)base + off`.

### Phase B — replace inline encode/decode in headers

Each of the 10 sites in `element.h`, `text.h`, `comment.h`,
`cdata.h`, `pi.h` calls the new helpers instead of inlining the
silent-drop logic.

### Phase C — accept

- macOS CI tests above pass.
- Linux + ASAN tests still pass.
- New spec: create a doc with nodes > 2GB apart (synthetic test or
  careful allocator) and verify tree edges survive.

## Implementation notes

- The overflow table is keyed on field address (`const int32_t*`).
  Each field gets its own entry. Cleanup happens via the existing
  per-document cleanup walker.
- `INT32_MIN` is a safe sentinel: it's -2147483648, which would
  correspond to a -2GB offset. Pool pages are 32 KB; a real
  -2GB offset is implausible.

## Estimated effort

~200 lines of code across `compact.c` (helpers) and 5 headers
(callers). Half-day of focused work.
