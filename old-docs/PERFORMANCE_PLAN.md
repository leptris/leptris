# Performance Improvement Plan: Beat libxml2 in ALL Cases

**Created:** 2026-03-06
**Goal:** Achieve >= 1.0x (parity or faster) vs libxml2 in ALL benchmark categories
**Deadline:** ASAP

---

## Current Performance Status

### ✅ Already Faster than libxml2

| Category | Taurus vs libxml2 | Margin |
|----------|-------------------|--------|
| Parsing (Copy) | 2-3x FASTER | ✅ Good |
| Parsing (Inplace) | 3-5x FASTER | ✅ Excellent |
| XPath Simple (//item) | 2-3x FASTER | ✅ Good |
| Traversal | 1.0-1.5x FASTER | ✅ Acceptable |
| Serialization | 2-4x FASTER | ✅ Good |

### ❌ Slower than libxml2

| Category | Taurus vs libxml2 | Margin | Priority |
|----------|-------------------|--------|----------|
| XPath Predicates | 1.5-3.5x SLOWER | 🔴 Critical | HIGH |
| XPath Complex (functions) | 1.3-1.6x SLOWER | 🟡 Medium | MEDIUM |

---

## Root Cause Analysis

### XPath Predicate Performance Gap

The benchmark shows Taurus is slower for queries like:
- `//item[@id='123']` - Attribute predicates
- `//catalog/item[position() < 5]` - Position predicates
- `//item[price > 100]` - Comparison predicates

**Suspected causes:**
1. **Predicate evaluation** - May not be using efficient data structures
2. **Node set operations** - Union, intersection could be optimized
3. **String comparison** - Attribute value comparisons in predicates
4. **Position tracking** - Position-based predicates may recalculate

### Areas to Investigate

1. **evaluator_operators.c** - Predicate evaluation logic
2. **evaluator_path.c** - Path evaluation with predicates
3. **functions.c** - XPath function implementations
4. **Node set handling** - How we collect and filter nodes

---

## Phase 1: XPath Predicate Optimization (HIGH PRIORITY)

### Target: Achieve >= 1.0x vs libxml2 for ALL XPath queries

### 1.1 Profile XPath Predicate Hot Paths

**Files:** `src/taurus/xpath/evaluator_operators.c`, `evaluator_path.c`

**Tasks:**
- [ ] Profile predicate evaluation with `perf` or Instruments
- [ ] Identify top 3 hot functions in predicate path
- [ ] Document current algorithm complexity

### 1.2 Optimize Attribute Access in Predicates

**Current issue:** `//item[@id='x']` may be doing repeated attribute lookups

**Solutions:**
- [ ] Cache attribute hash lookups during predicate evaluation
- [ ] Use SIMD for attribute name comparison
- [ ] Early exit when attribute not found

### 1.3 Optimize Node Set Operations

**Files:** `src/taurus/xpath/evaluator.c`

**Tasks:**
- [ ] Review node set creation/copying in predicate evaluation
- [ ] Implement lazy node set evaluation where possible
- [ ] Optimize node set union/intersection algorithms

### 1.4 Optimize Position Tracking

**Files:** `src/taurus/xpath/functions.c`

**Tasks:**
- [ ] Cache position() values during iteration
- [ ] Avoid recalculation of position in nested predicates
- [ ] Review last() function implementation

---

## Phase 2: XPath Function Optimization (MEDIUM PRIORITY)

### Target: Achieve >= 1.0x vs libxml2 for XPath functions

### 2.1 String Functions

**Functions:** `concat()`, `substring()`, `translate()`, `normalize-space()`

**Tasks:**
- [ ] Profile string function performance
- [ ] Optimize string concatenation (avoid repeated allocations)
- [ ] SIMD-optimize `contains()` and `starts-with()`

### 2.2 Numeric Functions

**Functions:** `sum()`, `floor()`, `ceiling()`, `round()`

**Tasks:**
- [ ] Review number parsing in `sum()`
- [ ] Optimize numeric conversions

### 2.3 Boolean Functions

**Functions:** `boolean()`, `not()`, `true()`, `false()`, `lang()`

**Tasks:**
- [ ] These should be fast, verify no overhead

---

## Phase 3: DOM Traversal Optimization (LOW PRIORITY)

### Target: Maintain >= 1.0x vs libxml2 for all traversal

### 3.1 Child Iteration

**Tasks:**
- [ ] Review first_child/next_sibling access patterns
- [ ] Consider prefetching for sequential access

### 3.2 Attribute Access

**Tasks:**
- [ ] Verify hash table is being used for O(1) lookup
- [ ] Profile attribute iteration patterns

---

## Phase 4: Parsing Micro-Optimizations (OPTIONAL)

### Target: Maintain or improve current advantage

### 4.1 SIMD Scanning

**Status:** Already implemented in xml_scanner.h

**Tasks:**
- [ ] Verify SIMD paths are being used in hot loops
- [ ] Profile to ensure no SIMD threshold issues

### 4.2 Memory Allocation

**Tasks:**
- [ ] Review pool allocation overhead
- [ ] Consider batch allocation for very large files

---

## Implementation Order

```
Week 1: Phase 1.1-1.2 (Profile + Attribute optimization)
Week 2: Phase 1.3-1.4 (Node sets + Position)
Week 3: Phase 2.1-2.2 (String/Numeric functions)
Week 4: Phase 3 + Verification
```

---

## Success Criteria

After each phase, run benchmarks and verify:

```bash
./build/benchmarks/ultimate_benchmark
```

**Target metrics:**
- All XPath tests: >= 1.0x vs libxml2 (no regressions)
- Parsing: Maintain current 2-3x advantage
- Serialization: Maintain current 2-4x advantage

---

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| Optimization breaks correctness | Run full test suite after each change |
| SIMD optimizations not portable | Keep scalar fallback |
| Memory usage increases | Monitor peak memory in benchmarks |

---

## Files to Modify

| File | Phase | Expected Impact |
|------|-------|-----------------|
| `xpath/evaluator_operators.c` | 1.1-1.2 | High |
| `xpath/evaluator_path.c` | 1.1, 1.3 | High |
| `xpath/functions.c` | 1.4, 2.1-2.3 | Medium |
| `xpath/evaluator.c` | 1.3 | Medium |
| `dom/element.c` | 1.2, 3.2 | Low |

---

## Quick Wins to Try First

1. **Attribute predicate shortcut** - Return false immediately if attribute doesn't exist
2. **Position caching** - Calculate position once per predicate evaluation
3. **String comparison** - Use length check before memcmp in predicates

---

## Verification Commands

```bash
# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Run benchmarks
./build/benchmarks/ultimate_benchmark

# Memory check
leaks --atExit -- ./build/test/c/test_dom
```
