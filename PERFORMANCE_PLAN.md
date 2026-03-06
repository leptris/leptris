# Performance Improvement Plan: Beat libxml2 in ALL XPath Cases

**Created:** 2026-03-06
**Goal:** Achieve >= 1.0x (parity or faster) vs libxml2 in ALL XPath predicate benchmarks
**Deadline:** ASAP
**Last Updated:** 2026-03-06

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
| XPath Predicates | 1.6-1.7x SLOWER | Target: >= 1.0x |
| XPath Functions | 1.3-1.6x SLOWER | Target: >= 1.0x |

---

## Root Cause Analysis

### Identified Bottlenecks

1. **O(n) Attribute Lookup** (`ptr_accessor.c:95-106`)
   - `ptr_element_find_attr()` uses linear search through linked list
   - Called for EVERY predicate evaluation like `[@id='x']`
   - libxml2 also uses linked lists but may have different optimization

2. **AST-based Optimizations Needed**
   - Fast path for `[@attr='value']` pattern implemented but needs better matching
   - Currently not triggering due to AST structure differences

---

## Implementation Tasks

| Task | Status | Files |
|------|--------|-------|
| Add `get_node_text_direct()` | ✅ Complete | evaluator_types.c |
| Optimize single-node equality comparison | ✅ Complete | evaluator_operators.c |
| Add fast path for `[@attr='value']` | ✅ Complete | evaluator_path.c |
| Add attribute hash table | 🔴 Pending | ptr_element.h, element_modify.c |

---

## Next Steps

1. **Add attribute hash table** (Major optimization)
   - Add O(1) attribute lookup via hash table
   - Would reduce predicate evaluation from O(n) to O(1)

2. **Optimize attribute axis evaluation**
   - Short-circuit attribute lookup for simple predicates
   - Avoid nodeset creation for single attribute access

---

## Verification

```bash
# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Run benchmarks
./build/benchmarks/ultimate_benchmark
```
