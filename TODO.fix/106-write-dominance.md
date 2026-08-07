# TODO 106 — DOM write dominance vs pugixml

**Priority**: P1 (user goal: "achieve WRITE dominance")
**Status**: baseline established; pugixml dominates

## Baseline (local macOS, M-series)

| Operation | taurus | pugixml | libxml2 | taurus vs pugixml | taurus vs libxml2 |
|---|---|---|---|---|---|
| Append 1000 children | 49.92 µs | 12.38 µs | 61.10 µs | **4.0x slower** | 1.22x faster |
| Set 100 attributes | 80.76 µs | 11.54 µs | 36.97 µs | **7.0x slower** | 2.2x slower |
| Set text content | 1.21 µs | 0.70 µs | 0.79 µs | 1.7x slower | 1.5x slower |
| Parse small + 10 writes | 36.34 µs | 2.31 µs | 13.28 µs | 15.7x slower | 2.7x slower |
| Parse medium + 10 writes | 113.42 µs | 5.62 µs | 48.78 µs | 20.2x slower | 2.3x slower |
| Parse large + 10 writes | 220.34 µs | 7.64 µs | 96.72 µs | 28.8x slower | 2.3x slower |

pugixml beats both taurus AND libxml2 by huge margins on every write
operation. Taurus is roughly competitive with libxml2 — faster on
append, slower on attrs.

## Why pugixml wins

Architectural advantages pugixml has over both taurus and libxml2:

1. **Compact storage**: pugixml packs a node into 44 bytes (vs
   taurus's ~96).  Better cache locality, fewer allocations per page.
2. **Custom slab allocator**: no per-node pool call. Bulk-allocates
   pages, doles out from a free list. Cheaper than taurus's pool.
3. **No StringView conversion**: pugixml stores char* directly into
   a string buffer that grows in chunks.  Taurus pays pool_strdup
   per name + per attribute name + per attribute value.
4. **No attribute hash interning**: taurus's per-attribute intern
   lookup is ~200ns × 100 attrs = 20µs of pure interning overhead.
5. **Inlined mutation paths**: pugixml is a header-only library; the
   compiler sees everything. Taurus's mutation entry points are
   in element_modify.c, called across a TU boundary.

## Plan

### Phase 1 — attribute fast path (closes ~50% of the attr gap)

The `taurus_element_add_attribute` path does eager name interning
via the pool's hash table. For mutation workloads (not parsing),
interning is wasted work — the user knows the attr is unique.

Add a non-interning path: `taurus_element_set_attribute` (the
public mutation API) calls a new `taurus_element_add_attribute_fast`
that skips the hash lookup and just pool_strdup's name + value.

Reward: ~30 µs off the 100-attr benchmark (~80 µs → ~50 µs).
Still 4.3x slower than pugixml, but real progress.

### Phase 2 — bulk element allocation (closes ~30% of the append gap)

Add `taurus_element_create_batch(doc, name, n)` that pre-allocates
N elements from a single pool slab. The user pattern "create N
identical children" is common enough to optimize.

Reward: ~10 µs off the 1000-append benchmark (~50 µs → ~40 µs).
Still 3.3x slower than pugixml.

### Phase 3 — node size reduction (architectural, multi-week)

Taurus's element struct is ~96 bytes. pugixml's is 44. The gap is
mostly:
- 32 bytes of TaurusStringView (name, prefix, namespace_uri) per
  element (3 × 16 bytes)
- 24 bytes of cached cstr pointers (name, prefix, namespace_uri)
- Element pointer fields (parent, first/last child, next sibling)

pugixml uses compact pointer encoding (similar to taurus's compact
mode that's currently unused).  Could reduce taurus's element to
~64 bytes by using compact pointers and dropping eager cstr cache.

Multi-week refactor. Defer.

### Phase 4 — pool allocator tuning

taurus's pool uses 32KB pages by default. pugixml uses 4KB-32KB
depending on need. The page size affects cache behavior.

Profile-guided: try 4KB, 8KB, 16KB pages, measure impact on write
workloads. Reward: maybe 5-10%.

## Acceptance

User goal "WRITE dominance over both libraries" is aggressive.

Realistic interim targets:
- **Set 100 attrs**: taurus within 2x of pugixml (was 7x; Phase 1 + 2)
- **Append 1000 children**: taurus within 2x of pugixml (was 4x; Phase 2)
- **Parse + writes**: dominated by parse cost (TODO 103). Close parse gap
  first; write gap will follow.

Beating pugixml on writes is not realistic without Phase 3 (architectural
node-size reduction). Document as a long-term goal.
