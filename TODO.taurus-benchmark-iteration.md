# Taurus Benchmark Suite - Comprehensive Plan

**Goal:** Be 1.0-1.2x faster than pugixml in ALL areas, faster than libxml2 in ALL areas.

**Current Status:** 55.0% pass rate (44/80 tests) - Updated 2025-02-18

**Phase 1 Complete:** Fixed benchmark measurement bug. Child axis shows real performance (0.06x).
**Phase 2.1 Complete:** Dynamic hash table growth (16.6x improvement for large files!).

---

## Executive Summary

### Current Pass Rate: 55.0% (44/80 tests)

**Phase 1 Complete:** Benchmark measurement bug fixed (libxml2 context node).
**Phase 2.1 Complete:** Dynamic hash table growth (16.6x improvement for large files!).

### Performance Analysis by Category

| Category | Tests | Pass | Fail | Pass Rate |
|----------|-------|------|------|-----------|
| Parsing | 10 | 4 | 6 | 40% |
| Traversal | 5 | 3 | 2 | 60% |
| Attributes | 12 | 11 | 1 | 92% |
| Modification | 8 | 4 | 4 | 50% |
| XPath | 20 | 8 | 12 | 40% |
| Scenarios | 4 | 0 | 4 | 0% |
| Serialization | 8 | 4 | 4 | 50% |
| Memory | 4 | 4 | 0 | 100% |

### Parsing Performance Improvement (Phase 2.1)

| Test | Before | After | Improvement |
|------|--------|-------|-------------|
| Large (10MB) | 0.02x | 0.29x | **16.6x faster** |
| Many Attributes | 0.08x | 0.20x | **2.4x faster** |
| Medium (200KB) | 0.32x | 0.35x | 1.1x faster |

---

## Areas of Excellence (40 PASSING)

### XPath Axes (8-125x faster than libxml2!)
| Test | Speedup | Status |
|------|---------|--------|
| following axis | **125.91x** | PASS |
| preceding-sibling axis | **64.97x** | PASS |
| preceding axis | **58.23x** | PASS |
| ancestor-or-self axis | **1.45x** | PASS |
| ancestor axis | **1.36x** | PASS |
| descendant-or-self axis | **1.10x** | PASS |
| descendant axis | **1.06x** | PASS |
| parent axis | **1.04x** | PASS |

### Attribute Access (O(1) hash table - 1.68-16.19x faster!)
| Test | Speedup | Status |
|------|---------|--------|
| 100 attrs lookup | **16.19x** | PASS |
| 50 attrs lookup | **8.80x** | PASS |
| 20 attrs lookup | **4.18x** | PASS |
| 10 attrs lookup | **1.68x** | PASS |
| 4 attrs (inline) | **1.71x** | PASS |
| 1 attr lookup | **1.81x** | PASS |
| Get Last Attribute | **11.55x** | PASS |
| Get Middle Attribute | **5.96x** | PASS |

### DOM Modification (2-3x faster!)
| Test | Speedup | Status |
|------|---------|--------|
| Prepend child | **3.08x** | PASS |
| Append child | **2.96x** | PASS |
| Remove child | **2.65x** | PASS |
| Serialize to string | **2.01x** | PASS |

### Traversal
| Test | Speedup | Status |
|------|---------|--------|
| Deep recursive walk | **5.00x** | PASS |
| Parent access | **1.50x** | PASS |
| First child access | **1.00x** | PASS |

---

## Critical Gaps Analysis (41 FAILING)

### GAP 1: Parsing Performance (6 failures)

**Current Results:**
| Test | vs pugixml | vs libxml2 | Status |
|------|------------|------------|--------|
| Small (0.1KB) | 1.00x | 3.00x | PASS |
| Large (10.8MB) | **0.02x** | 0.13x | CRITICAL |
| Many Attributes | **0.08x** | 0.56x | CRITICAL |
| Medium (210KB) | 0.31x | 1.18x | FAIL |
| Deep (100 levels) | 0.24x | 1.47x | FAIL |
| Wide (1000 siblings) | 0.25x | 0.95x | FAIL |

**Root Cause:** No SIMD for tag detection (< > /)

**Solution:** Phase A - SIMD Parser Integration

---

### GAP 2: XPath Axes (5 failures + 1 BUG)

**Current Results:**
| Test | Speedup | Issue |
|------|---------|-------|
| child axis | 0.00x | **BUG** - libxml2 returns 0.00us |
| union operator | 0.14x | Needs hash dedup |
| following-sibling | 0.58x | Nodeset overhead |
| namespace axis | 0.29x | Slow resolution |
| self axis | 0.23x | Nodeset overhead |
| attribute axis | 0.22x | Nodeset overhead |

**Root Causes:**
1. Child axis bug - measurement error or empty result
2. Union uses O(n²) deduplication
3. Axes create nodesets with malloc overhead

**Solution:** Phase B - XPath Optimization

---

### GAP 3: DOM Modification Fine-tuning (4 failures)

**Current Results:**
| Test | Speedup | Status |
|------|---------|--------|
| Clone element | 1.00x | FAIL (borderline) |
| Set text content | 0.96x | FAIL |
| Set attribute | 0.83x | FAIL |
| Remove attribute | 0.77x | FAIL |

**Root Cause:** Hash table overhead for small attribute counts

**Solution:** Phase C - DOM Fine-tuning

---

### GAP 4: Real-World Scenarios (4 failures)

**Current Results:**
| Test | Speedup | Status |
|------|---------|--------|
| RSS Feed | 0.34x | FAIL |
| Config File | 0.37x | FAIL |
| SOAP Response | 0.34x | FAIL |
| SVG Processing | 0.48x | FAIL |

**Root Cause:** Parsing speed dominates these tests

**Solution:** Fix parsing, scenarios will improve

---

### GAP 5: Serialization (4 failures)

**Current Results:**
| Test | Speedup | Status |
|------|---------|--------|
| CDATA sections | 2.11x | PASS |
| Single element | 1.15x | PASS |
| Entity encoding | 1.05x | PASS |
| Small | 1.12x | PASS |
| Large | **0.16x** | CRITICAL |
| Medium | 0.89x | FAIL |
| Pretty print | 0.91x | FAIL |
| Minimal | 0.89x | FAIL |

**Root Cause:** Buffer allocation strategy for large files

**Solution:** Phase D - Serialization Optimization

---

## Implementation Plan

### PHASE 1: Fix XPath Child Axis Bug (COMPLETED - 2025-02-18)

**Problem:** `child axis: child::section` shows 0.00x speedup

**Investigation & Fix:**
1. Root cause: libxml2 context was set to document node, not root element
2. `child::section` from document context returns empty (documents don't have element children)
3. Fixed by adding `xmlXPathSetContextNode(root_element, context)` in benchmark code
4. Applied fix to all three test functions in bench_xpath_all.cpp

**Result:** Child axis now shows 0.06x (real measurement, but reveals performance gap)
- This is a real performance issue: Taurus child axis is 16x slower than libxml2
- Will be addressed in Phase 3: XPath Optimization

**Files Modified:**
- `benchmarks/suite/bench_xpath_all.cpp` - Fixed libxml2 context node setting

---

### PHASE 2: Parser Scalability Fix (1 week) - CRITICAL

**Root Cause Analysis:**
- SIMD is already integrated in parser hot paths
- Real bottleneck: String deduplication hash table has only 128 buckets
- For 10MB files: ~1M strings → 7800 entries/bucket → O(7800) lookups
- Each string intern creates 3 allocations (string, entry, key copy)
- This explains why parsing is 50x slower for large files vs 3x for medium

**Goal:** Fix O(n²) behavior in string deduplication → 10-20x improvement

**Tasks:**

#### 2.1: Dynamic Hash Table Sizing (2 days) - HIGH IMPACT
**Files:**
- `src/taurus/memory/pool.c`
- `src/taurus/memory/pool.h`

**Problem:** Fixed 128 buckets don't scale to millions of strings

**Solution:**
```c
// Start with 1024 buckets, grow when load factor > 0.75
void taurus_hash_table_grow(StringHashTable* table);
// Calculate: 1M strings / 1024 buckets = ~1000 entries/bucket
// After growth: 1M strings / 16384 buckets = ~60 entries/bucket
```

**Expected:** Large file 0.02x → 0.10x

#### 2.2: Reduce String Intern Overhead (2 days)
**Files:**
- `src/taurus/memory/pool.c`

**Problem:** 3 allocations per unique string (string, entry, key copy)

**Solution:**
```c
// Single allocation for string + entry
typedef struct {
    char key_data[FLEXIBLE_ARRAY];  // key + cached string together
    uint32_t key_length;
    uint32_t hash;
    StringHashEntry* next;
} StringHashEntry;
```

**Expected:** Additional 1.5x improvement

#### 2.3: SIMD Already Integrated (0 days)
**Status:** SIMD is already used in:
- `parse_name_view()` - `simd_scan_name()`
- `parse_attribute_value_view()` - `simd_find_quote_end()`
- `parser_parse_text()` - `simd_find_xml_special()`

No additional SIMD work needed for parsing hot paths.

**Phase 2 Target:** Parsing at >=0.20x pugixml (4 tests fixed)

---

### PHASE 3: XPath Optimization (1 week) - HIGH

**Goal:** 3-5x XPath improvement for failing axes

#### 3.1: Fix Union Deduplication (2 days)
**Files:**
- `src/taurus/xpath/evaluator_operators.c`

**Problem:** O(n²) deduplication

**Solution:**
```c
// Use hash set for O(1) duplicate detection
typedef struct {
    TaurusElement* elements;
    size_t* hashes;
    size_t capacity;
    size_t size;
} XPathHashSet;
```

**Expected:** Union 0.14x → 1.0x+

#### 3.2: Pool-Allocated Nodesets (2 days)
**Files:**
- `src/taurus/xpath/nodeset.c`
- `src/taurus/xpath/evaluator.c`

**Problem:** malloc for every nodeset

**Solution:**
```c
XPathNodeSet* xpath_nodeset_new_pooled(XPathContext* ctx);
void xpath_nodeset_reset(XPathNodeSet* set);
```

**Expected:** 2-3x for self/attribute/namespace axes

#### 3.3: Optimize following-sibling (1 day)
**Files:**
- `src/taurus/xpath/evaluator_axes.c`

**Analysis:** Following-sibling should be fast but shows 0.58x

**Solution:** Use pooled nodesets, reduce overhead

**Expected:** following-sibling 0.58x → 1.0x+

**Phase 3 Target:** All XPath at >=1.0x libxml2 (5 tests fixed)

---

### PHASE 4: DOM Fine-tuning (3 days) - MEDIUM

**Goal:** 1.2-1.5x DOM improvement

#### 4.1: Set Attribute Optimization (1 day)
**Files:**
- `src/taurus/dom/element_modify.c`

**Problem:** Hash table overhead for small counts

**Solution:** Lazy hash table - only create when >4 attrs

**Expected:** Set attribute 0.83x → 1.0x+

#### 4.2: Remove Attribute Optimization (1 day)
**Files:**
- `src/taurus/dom/element_modify.c`

**Solution:** Optimize inline array removal, lazy hash

**Expected:** Remove attribute 0.77x → 1.0x+

#### 4.3: Set Text Optimization (0.5 day)
**Files:**
- `src/taurus/dom/element_modify.c`

**Solution:** Pool-allocate text content

**Expected:** Set text 0.96x → 1.0x+

**Phase 4 Target:** All DOM at >=1.0x pugixml (4 tests fixed)

---

### PHASE 5: Serialization Optimization (2 days) - MEDIUM

**Goal:** Fix large file serialization

#### 5.1: Buffer Strategy (1 day)
**Files:**
- `src/taurus/serialize/serialize.c`

**Problem:** Poor scaling for large files (0.16x)

**Solution:**
1. Pre-calculate output size
2. Single allocation for known-size output
3. Larger initial buffer (16KB)

**Expected:** Large serialize 0.16x → 0.5x+

#### 5.2: Streaming Optimization (1 day)
**Files:**
- `src/taurus/serialize/serialize.c`

**Solution:** Chunk-based serialization for very large files

**Expected:** Medium serialize 0.89x → 1.0x+

**Phase 5 Target:** Serialization at >=1.0x for small/medium (3 tests fixed)

---

### PHASE 6: Traversal Fine-tuning (1 day) - LOW

**Goal:** Fix remaining traversal issues

#### 6.1: Next Sibling Optimization
**Files:**
- `src/taurus/dom/element.h`

**Problem:** Next sibling shows 0.83x due to type checking

**Solution:** Optimize type checking in inline function

**Expected:** Next sibling 0.83x → 1.0x+

**Phase 6 Target:** All traversal at >=1.0x pugixml (2 tests fixed)

---

## Progress Tracking

### Completed
- [x] O(1) Attribute Access (inline array + hash table)
- [x] last_attribute pointer for O(1) append
- [x] Hash-based deduplication for XPath union (partial)
- [x] All 58 unit tests pass
- [x] Benchmark suite infrastructure (81 tests)
- [x] Phase 1: Fix XPath child axis bug (benchmark measurement issue fixed)
- [x] Phase 2.1: Dynamic hash table growth (16.6x improvement for large files!)

### In Progress
- [ ] Phase 2.2: Reduce string intern overhead

### Pending (in order)
- [ ] Phase 3: XPath Optimization
- [ ] Phase 4: DOM Fine-tuning
- [ ] Phase 5: Serialization Optimization
- [ ] Phase 6: Traversal Fine-tuning

---

## Expected Test Fix Count

| Phase | Tests Fixed | Running Total | Notes |
|-------|-------------|---------------|-------|
| Phase 1 (Bug fix) | 0 | 39/81 (48.1%) | Fixed measurement, revealed real gap |
| Phase 2.1 (Hash growth) | 5 | 44/80 (55.0%) | **16.6x improvement for large files** |
| Phase 2.2 (String intern) | 2 | 46/80 (57.5%) | Reduce intern overhead |
| Phase 3 (XPath) | 5 | 51/80 (63.8%) | Axes + union optimization |
| Phase 4 (DOM) | 4 | 55/80 (68.8%) | DOM fine-tuning |
| Phase 5 (Serialize) | 3 | 58/80 (72.5%) | Buffer optimization |
| Phase 6 (Traversal) | 2 | 60/80 (75.0%) | Next sibling optimization |

**Note:**
- Phase 2.1 completed: Hash table now grows dynamically, maintaining O(1) lookups
- Scenarios (4 tests) will improve automatically with parsing fixes.
- Child axis (0.06x) will be addressed in Phase 3.

---

## Success Criteria

| Metric | Current | After All Phases |
|--------|---------|------------------|
| Pass Rate | 49.4% (40/81) | **>= 90% (73/81)** |
| Parsing vs pugixml | 0.02-0.31x | >= 0.33x |
| All DOM | 0.77-3.08x | >= 1.0x |
| All XPath | 0.00-125.91x | >= 1.0x |
| Serialization | 0.16-2.11x | >= 0.5x large, >=1.0x small |
| Memory | 100% | <= 110% |

---

## Quick Reference

### Run All Benchmarks
```bash
cd benchmarks/suite
DATA_DIR=../fixtures/data QUICK_MODE=1 ./run_all_benchmarks.sh
```

### Run Specific Benchmark
```bash
./build/benchmarks/suite/bench_parsing ../fixtures/data
./build/benchmarks/suite/bench_xpath_all ../fixtures/data
```

### Build
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DTAURUS_BUILD_BENCHMARKS=ON
cmake --build build
```

### Test
```bash
ctest --test-dir build --output-on-failure
```

---

## Notes

1. **Parsing Target Rationale:** pugixml has 15+ years of SIMD optimization. 0.33x is realistic stretch goal.

2. **XPath Excellence:** We're already 50-125x faster for some axes - maintain this advantage!

3. **The Path to 90%+:** SIMD parsing + XPath optimization + DOM fine-tuning = 73/81 tests passing.

4. **Bug First:** The child axis 0.00x must be investigated first - it's likely a simple fix.
