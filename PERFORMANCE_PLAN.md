# Performance Improvement Plan: Beat libxml2 in ALL XPath Cases

**Created:** 2026-03-06
**Goal:** Achieve >= 1.0x (parity or faster) vs libxml2 in ALL XPath predicate benchmarks
**Deadline:** ASAP

---

## Current Status

### ✅ Already Faster than libxml2

| Category | Taurus vs libxml2 | Status |
|----------|-------------------|--------|
| Parsing (Copy) | 2-3x FASTER | ✅ WIN |
| Parsing (Inplace) | 3-5x FASTER | ✅ WIN |
| XPath Simple (//item) | 2-3x FASTER | ✅ WIN |
| Traversal | ~1.0-1.5x FASTER | ✅ WIN |
| Serialization | 2-4x FASTER | ✅ WIN |

### ❌ Slower than libxml2 (NEEDS WORK)

| Category | Taurus vs libxml2 | Gap |
|----------|-------------------|-----|
| XPath Predicates | 1.5-3.5x SLOWER | Target: >= 1.0x |
| XPath Functions | 1.3-1.6x SLOWER | Target: >= 1.0x |

---

## Root Cause Analysis

### Identified Bottlenecks

1. **O(n*m) Nodeset Comparison** (`evaluator_operators.c`)
   - Compares every node in left nodeset with every node in right nodeset
   - This is O(n*m) complexity

2. **Repeated Memory Allocation** (`evaluator_types.c:20-41`)
   - `get_node_text()` allocates a new string every call
   - Called during every predicate evaluation
   - Causes heap pressure

3. **No Attribute Lookup Cache**
   - Attribute predicates like `[@id='x']` do full lookup each time
   - Hash table lookup is O(1), but repeated lookups during iteration

---

## Optimization Strategy

### Phase 1: Optimize Nodeset Comparison

**Target:** O(n) instead of O(n*m)

**Approach:**
- Add fast path for single-node nodeset comparison
- Use direct string comparison for attributes
- Early exit when attribute doesn't exist

### Phase 2: Eliminate Memory Allocation

**Target:** Zero allocation in predicate comparison hot path

**Approach:**
- Add `get_node_text_direct()` - returns pointer to internal string
- Update comparison operators to use direct comparison
- Avoid `taurus_strdup()` in hot paths

### Phase 3: Add Comparison Cache

**Target:** O(1) attribute access with caching

**Approach:**
- Cache attribute lookups during predicate evaluation
- Invalidate cache when predicate evaluation completes

---

## Implementation Tasks

| Task | Status | Files |
|------|--------|-------|
| Add `get_node_text_direct()` | 🔴 Pending | evaluator_types.c |
| Optimize single-node equality comparison | 🔴 Pending | evaluator_operators.c |
| Add comparison cache | 🔴 Pending | evaluator_operators.c |

---

## Verification

```bash
# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Run benchmarks
./build/benchmarks/ultimate_benchmark

# Target: All XPath tests >= 1.0x vs libxml2
```
