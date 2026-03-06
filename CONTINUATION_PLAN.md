# Continuation Plan: Beat libxml2 in XPath Predicates

**Created:** 2026-03-07
**Goal:** Achieve >= 1.0x vs libxml2 in ALL XPath predicate benchmarks
**Strategy:** Implement libxml2's hash-based early exit optimization

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

### Why Our Optimization Didn't Help Much

Our fast path (`fast_path_attr_string_predicate`) does direct string comparison:
1. Get attribute value (O(n) linked list traversal)
2. Do full `strcmp()` comparison

libxml2's approach:
1. Compute hash of target string (O(1))
2. For each node, compute node's hash (O(1))
3. Only do full comparison if hashes match (rare)

---

## Implementation Plan

### Phase 1: Add Fast Hash Functions

**File:** `src/taurus/xpath/evaluator_types.c`

Add functions:
- `xpath_fast_hash(const char* str)` - Returns hash of first two chars
- `xpath_node_val_hash(void* node)` - Returns hash of node's text content

### Phase 2: Optimize Nodeset-String Comparison

**File:** `src/taurus/xpath/evaluator_operators.c`

Update `evaluate_operator()` for EQUAL/NOT_EQUAL:
- Compute hash of string literal ONCE
- For each node in nodeset, compare hashes first
- Only do full string comparison if hashes match

### Phase 3: Optimize Predicate Fast Path

**File:** `src/taurus/xpath/evaluator_path.c`

Update `fast_path_attr_string_predicate()`:
- Add hash-based early exit
- Compute hash of literal string once
- Get attribute value and compare hash before full strcmp

---

## Expected Impact

- **Hash comparison:** O(1) vs O(n) for string comparison
- **Early exit:** Eliminates 90%+ of full string comparisons
- **Memory:** No allocation needed for hash computation

**Target:** >= 1.0x vs libxml2 (currently 1.6x slower)

---

## Tasks

| # | Task | Status | File |
|---|------|--------|------|
| 1 | Add `xpath_fast_hash()` function | 🔴 Pending | evaluator_types.c |
| 2 | Add `xpath_node_val_hash()` function | 🔴 Pending | evaluator_types.c |
| 3 | Update comparison operators with hash early exit | 🔴 Pending | evaluator_operators.c |
| 4 | Update predicate fast path with hash | 🔴 Pending | evaluator_path.c |
| 5 | Run benchmarks to verify improvement | 🔴 Pending | - |

---

## Verification

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/benchmarks/ultimate_benchmark

# Target: XPath predicates >= 1.0x vs libxml2
```
