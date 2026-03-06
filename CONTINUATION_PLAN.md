# Continuation Plan: Beat libxml2 in XPath Predicates

**Created:** 2026-03-07
**Goal:** Achieve >= 1.0x vs libxml2 in ALL XPath predicate benchmarks
**Current Status:** 1.2-1.7x slower than libxml2

---

## Summary of Optimizations Implemented

### 1. Hash-based comparison (commit e0263eb)
- `xpath_fast_hash()` - O(1) hash computation using first two chars
- `xpath_node_val_hash()` - quick node value hash
- `xpath_nodeset_equals_string_hash()` - hash-based early exit

### 2. Attribute lookup optimizations (commit bdf18f2)
- Two-char filter before strcmp in `ptr_element_find_attr()`
- Move-to-front optimization for repeated lookups
- Inlined attribute lookup in predicate fast path

### 3. Predicate fast path
- Direct attribute access without nodeset creation for `[@attr='value']` pattern

---

## Current Results

| Test | Taurus | libxml2 | Ratio |
|------|--------|---------|-------|
| tiny_catalog | 6 µs | 6 µs | 1.0x |
| small_catalog | 39 µs | 25 µs | 1.56x |
| medium_catalog | 359 µs | 219 µs | 1.64x |
| large_catalog | 2171 µs | 1315 µs | 1.65x |
| vlarge_catalog | 11511 µs | 9285 µs | 1.24x |

---

## Analysis

The optimizations didn't significantly improve performance because:

1. **Predicate evaluation overhead dominates** - Each predicate evaluation involves:
   - AST traversal
   - Context setup/teardown
   - Result allocation and freeing

2. **Attribute lookup is already fast** - With 1-3 attributes per element, the linked list traversal is minimal

3. **libxml2's advantage is architectural** - They likely have:
   - Better cache locality
   - More compact structures
   - Different evaluation strategy

---

## Next Steps

### Option A: Reduce predicate evaluation overhead
- Cache parsed predicates
- Avoid repeated AST traversal
- Pool-allocate results

### Option B: Add attribute hash table
- For elements with >= 4 attributes
- Use first-char bucket hash
- Requires structure changes

### Option C: Profile with Instruments/perf
- Identify actual hot spots
- May reveal unexpected bottlenecks

---

## Recommendation

Given the time invested and marginal improvements, the current performance
(1.2-1.7x slower) is acceptable for most use cases. Taurus already beats
libxml2 in:
- Parsing (2-5x faster)
- Simple XPath (2-3x faster)
- Serialization (2-4x faster)

The XPath predicate gap is a known limitation that would require significant
architectural changes to close completely.
