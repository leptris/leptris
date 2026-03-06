# Continuation Plan: Beat libxml2 in XPath Predicates

**Created:** 2026-03-07
**Goal:** Achieve >= 1.0x vs libxml2 in ALL XPath predicate benchmarks
**Strategy:** Implement libxml2's hash-based early exit optimization

---

## Current Status

| Category | Taurus vs libxml2 | Status |
|----------|-------------------|--------|
| XPath Predicates | 1.6x SLOWER | 🔴 Needs work |
| XPath Simple | 2-3x FASTER | ✅ Good |

---

## Root Cause Analysis

### Why libxml2 is Faster (Discovered from Source Analysis)

libxml2 uses a **hash-based early exit** in `xmlXPathEqualNodeSetString()`:

```c
hash = xmlXPathStringHash(str);  // Hash of comparison string
for (i = 0; i < ns->nodeNr; i++) {
    if (xmlXPathNodeValHash(ns->nodeTab[i]) == hash) {
        // Only do full string comparison if hashes match!
        str2 = xmlNodeGetContent(ns->nodeTab[i]);
        if (xmlStrEqual(str, str2)) {
            return 1;  // Match found
        }
    }
}
```

**Key Insight:** The hash is just `string[0] + (string[1] << 8)` (first two characters). This is:
- O(1) to compute
- Eliminates most non-matches without allocation
- Only does full string extraction when hash matches

### Implemented Optimizations

1. **Hash-based comparison** ✅
   - Added `xpath_fast_hash()` and `xpath_node_val_hash()`
   - Added `xpath_nodeset_equals_string_hash()` with early exit
   - Updated comparison operators to use hash-based comparison

2. **Fast path for attribute predicates** ✅
   - Direct attribute access without nodeset creation
   - Pattern: `[@attr='value']`

### Why Hash Optimization Didn't Help Much

The hash comparison is fast, but the REAL bottleneck is:
1. **O(n) attribute lookup** via `ptr_element_find_attr()` - linked list traversal
2. This happens for EVERY element being tested in the predicate

---

## Next Steps

### Option 1: Add Attribute Hash Table (RECOMMENDED)

**File:** `src/taurus/dom/ptr_element.h`, `element_modify.c`

Add hash table for O(1) attribute lookup:
- Create hash table when element has >= 3 attributes
- Use simple hash like first character
- Fall back to linked list for small attribute counts

### Option 2: Optimize Attribute Linked List

**File:** `src/taurus/dom/ptr_accessor.c`

Keep frequently accessed attributes at head of list:
- Move matched attribute to head on successful lookup
- Cache last accessed attribute

### Option 3: Profile to Find Other Bottlenecks

Use profiler to identify hot paths we haven't optimized.

---

## Verification

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/benchmarks/ultimate_benchmark

# Target: XPath predicates >= 1.0x vs libxml2
# Current: ~1.6x slower
```
