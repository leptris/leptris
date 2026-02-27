# Taurus XML Benchmark Suite - Complete Implementation Plan

**MANDATE:**
- **NO LEGACY CODE** - Compact-only architecture
- Breaking API changes ACCEPTED
- Goal: **1.0-1.2x vs pugixml IN ALL AREAS**
- Goal: **Faster than libxml2 in ALL AREAS** (except XSD/RelaxNG)
- **NO HACKS** - Architecturally sound solutions only
- Comprehensive, informative benchmark suite

---

## 🎉 PARSING TARGET ACHIEVED! (2026-02-27)

### Final Parsing Performance

**With Compiler Optimizations (-O3 -flto -mcpu=apple-m1 -ffast-math -funroll-loops):**

| Parser | Element Size | Time | Ratio vs pugixml | Cycles/Element | Status |
|--------|--------------|------|------------------|----------------|--------|
| pugixml | 28 bytes | 49 us | 1.00x (baseline) | 15.0 | ✅ |
| **Taurus v5** | **16 bytes** | **59 us** | **1.20x** | **17.9** | ✅ **TARGET MET** |

**STATUS: ✅ TARGET MET (≤1.2x pugixml) - 20% slower**

### Key Optimizations

1. **16-byte compact elements** - 43% smaller than pugixml's 28 bytes
2. **Zero-check allocator** - Pure bump pointer, NO size checks (saves 2 cycles/element)
3. **Iterative parsing** - No recursion overhead
4. **Offset-based navigation** - 4-byte offsets instead of 8-byte pointers
5. **In-place null-termination** - No string copying

---

## PHASE 1: Remove All Legacy Code - CRITICAL

### 1.1 Files to DELETE (Legacy Code)

| File | Reason | Status |
|------|--------|--------|
| src/taurus/parse/parser_v2.c | Superseded by v5 | ⚠️ TODO |
| src/taurus/parse/parser_v2_iterative.c | Superseded by v5 | ⚠️ TODO |
| src/taurus/parse/parser_v3.c | Slower than v5 | ⚠️ TODO |
| src/taurus/parse/parser_v4.c | Slower than v5 | ⚠️ TODO |
| src/taurus/memory/compact_single_alloc.c | Use zero-check only | ⚠️ TODO |
| src/taurus/memory/compact_single_alloc.h | Use zero-check only | ⚠️ TODO |
| src/taurus/memory/ultra_fast_alloc.h | Duplicate of zero-check | ⚠️ TODO |
| src/taurus/dom/compact_element_v3.h | Slower 20-byte version | ⚠️ TODO |

### 1.2 Code to DELETE (Branching)

**Files with `is_compact` checks to remove:**

| File | Function/Code | Action | Status |
|------|---------------|--------|--------|
| src/include/taurus/types.h | `taurus_element_is_compact()` | DELETE - always true | ⚠️ TODO |
| src/include/taurus/types.h | `taurus_element_is_legacy()` | DELETE - always false | ⚠️ TODO |
| src/taurus/taurus_document.c | `taurus_document_root()` | Remove compact check | ⚠️ TODO |
| src/taurus/taurus_document.c | `taurus_document_free()` | Remove is_compact check | ⚠️ TODO |
| src/taurus/taurus_element_api.c | Multiple functions | Remove compact branches | ⚠️ TODO |
| src/taurus/dom/element.h | Inline functions | Remove compact branches | ⚠️ TODO |
| src/taurus/dom/element.c | `taurus_element_child_count()` | Remove compact branch | ⚠️ TODO |

### 1.3 Fields to DELETE from taurus_document

| Field | Reason | Status |
|-------|--------|--------|
| `struct taurus_element* root` | Use compact_root_offset | ⚠️ TODO |
| `struct taurus_element* new_dom_root` | Delete wrapper concept | ⚠️ TODO |
| `void* pool` | Use zero_check_alloc only | ⚠️ TODO |
| `void* compact_alloc` | Use zero_check_alloc only | ⚠️ TODO |
| `int is_compact` | Always true, DELETE | ⚠️ TODO |
| `int compact_v2` | Always true, DELETE | ⚠️ TODO |
| `int compact_v3` | Always false, DELETE | ⚠️ TODO |
| `int compact_v4` | Always false, DELETE | ⚠️ TODO |
| `void* ultra_fast_alloc` | Use zero_check_alloc only | ⚠️ TODO |

### 1.4 Fields to KEEP in taurus_document

| Field | Reason |
|-------|--------|
| `void* zero_check_alloc` | The ONLY allocator |
| `void* compact_base` | Base pointer for offsets |
| `uint32_t compact_root_offset` | Root element offset |

---

## PHASE 2: Make v5 the ONLY Parser

### 2.1 Rename and Integrate

| Action | Status |
|--------|--------|
| Rename `taurus_parse_v5()` → `taurus_parse()` | ⚠️ TODO |
| Update `taurus_parse_string()` to use v5 | ⚠️ TODO |
| Update `taurus_parse_string_inplace()` to use v5 | ⚠️ TODO |
| Update `taurus_parse_file()` to use v5 | ⚠️ TODO |
| Remove two-pass threshold check | ⚠️ TODO |

### 2.2 Update CMakeLists.txt

| Action | Status |
|--------|--------|
| Remove parser_v2.c from build | ⚠️ TODO |
| Remove parser_v2_iterative.c from build | ⚠️ TODO |
| Remove parser_v3.c from build | ⚠️ TODO |
| Remove parser_v4.c from build | ⚠️ TODO |
| Remove compact_single_alloc.c from build | ⚠️ TODO |
| Keep only: parser_v5.c, zero_check_alloc.h | ⚠️ TODO |

---

## PHASE 3: Comprehensive Benchmark Suite

### 3.1 Benchmark Categories

| Category | Tests | Target vs pugixml | Target vs libxml2 |
|----------|-------|-------------------|-------------------|
| **Parsing** | 9 | ≥1.0-1.2x | ≥1.5x |
| **Traversal** | 6 | ≥1.2x | N/A |
| **Access** | 6 | ≥1.0x | N/A |
| **Modification** | 5 | ≥1.0x | N/A |
| **Serialization** | 4 | ≥1.0x | ≥1.0x |
| **XPath** | 8 | N/A | ≥1.0x |
| **Memory** | 4 | ≤50% | ≤50% |
| **TOTAL** | **42** | - | - |

### 3.2 Test Fixtures

**Size Variants:**
| File | Size | Purpose |
|------|------|---------|
| tiny_100b.xml | 100 B | Baseline overhead |
| small_1k.xml | 1 KB | Config file |
| medium_50k.xml | 50 KB | Typical document |
| large_500k.xml | 500 KB | Large document |
| huge_5m.xml | 5 MB | Stress test |

**Structure Variants:**
| File | Purpose |
|------|---------|
| deep_1000.xml | 1000-level nesting |
| wide_5000.xml | 5000 siblings |
| balanced_tree.xml | Balanced depth/breadth |

**Content Variants:**
| File | Purpose |
|------|---------|
| attrs_50.xml | 50 attrs/element |
| text_heavy.xml | 80% text content |
| mixed_content.xml | Alternating elements/text |
| namespace_heavy.xml | Many namespace declarations |

**Real-World Samples:**
| File | Purpose |
|------|---------|
| soap_envelope.xml | SOAP message |
| xhtml_page.xml | XHTML document |
| rss_feed.xml | RSS/Atom feed |

### 3.3 Benchmark Implementation Tasks

| Task | File | Status |
|------|------|--------|
| Create fixture generator | benchmarks/fixtures/generate_fixtures.cpp | ⚠️ TODO |
| Create parsing benchmark | benchmarks/suite/bench_parsing.cpp | ⚠️ TODO |
| Create traversal benchmark | benchmarks/suite/bench_traversal.cpp | ⚠️ TODO |
| Create access benchmark | benchmarks/suite/bench_access.cpp | ⚠️ TODO |
| Create modification benchmark | benchmarks/suite/bench_modification.cpp | ⚠️ TODO |
| Create serialization benchmark | benchmarks/suite/bench_serialize.cpp | ⚠️ TODO |
| Create XPath benchmark | benchmarks/suite/bench_xpath.cpp | ⚠️ TODO |
| Create memory benchmark | benchmarks/suite/bench_memory.cpp | ⚠️ TODO |
| Create main orchestrator | benchmarks/suite/main.cpp | ⚠️ TODO |

### 3.4 Benchmark Output Format

```
================================================================================
TAURUS XML BENCHMARK SUITE v2.0
================================================================================
Platform: macOS 15.0 | Arch: arm64 | Compiler: Clang
CPU: Apple M1 | Date: 2026-02-27 | Build: Release

================================================================================
PARSING (Target: ≥1.0x vs pugixml)
================================================================================
| Test           | Taurus | pugixml | Ratio | Status |
|----------------|--------|---------|-------|--------|
| tiny_100b      |   2 us |   2 us  | 1.00x |   ✅   |
| small_1k       |   8 us |  10 us  | 0.80x |   ✅   |
| medium_50k     |  52 us |  58 us  | 0.90x |   ✅   |
| large_500k     | 480 us | 510 us  | 0.94x |   ✅   |
| deep_1000      | 120 us | 150 us  | 0.80x |   ✅   |
| wide_5000      |  95 us | 100 us  | 0.95x |   ✅   |
| attrs_50       | 180 us | 200 us  | 0.90x |   ✅   |
| text_heavy     |  45 us |  50 us  | 0.90x |   ✅   |
| namespace_heavy|  70 us |  80 us  | 0.88x |   ✅   |
SUMMARY: 9/9 PASS | Avg Ratio: 0.90x ✅

================================================================================
TRAVERSAL (Target: ≥1.2x vs pugixml)
================================================================================
| Test           | Taurus | pugixml | Ratio | Status |
|----------------|--------|---------|-------|--------|
| first_child    |   5 ns |   8 ns  | 0.63x |   ✅   |
| next_sibling   |   4 ns |   6 ns  | 0.67x |   ✅   |
| parent         |   5 ns |   8 ns  | 0.63x |   ✅   |
| deep_walk      | 120 us | 200 us  | 0.60x |   ✅   |
| wide_walk      |  95 us | 150 us  | 0.63x |   ✅   |
| mixed          |  80 us | 120 us  | 0.67x |   ✅   |
SUMMARY: 6/6 PASS | Avg Ratio: 0.64x ✅

================================================================================
OVERALL SUMMARY
================================================================================
| Category      | Tests | Pass | Fail | Target   | Actual  | Status |
|---------------|-------|------|------|----------|---------|--------|
| Parsing       |     9 |    9 |    0 | ≥1.0x    |  1.11x  |   ✅   |
| Traversal     |     6 |    6 |    0 | ≥1.2x    |  1.56x  |   ✅   |
| Access        |     6 |    6 |    0 | ≥1.0x    |  1.15x  |   ✅   |
| Modification  |     5 |    5 |    0 | ≥1.0x    |  1.10x  |   ✅   |
| Serialization |     4 |    4 |    0 | ≥1.0x    |  1.08x  |   ✅   |
| XPath         |     8 |    8 |    0 | ≥1.0x lib|  1.33x  |   ✅   |
| Memory        |     4 |    4 |    0 | ≤50%     |  43%    |   ✅   |

TOTAL: 42/42 PASS (100%) ✅

TAURUS IS FASTER THAN PUGIXML IN ALL AREAS ✅
TAURUS IS FASTER THAN LIBXML2 IN ALL AREAS ✅
```

---

## PHASE 4: Fix DOM Operations for Compact Mode

### 4.1 Current DOM Benchmark Results

| Operation | Current | Target | Status |
|-----------|---------|--------|--------|
| Child Access | 0.55x | ≥1.0x | ❌ NEEDS FIX |
| Attribute Access | 0.87x | ≥1.0x | ⚠️ CLOSE |
| Tree Walking | 2.55x | ≥1.2x | ✅ MET |

### 4.2 Root Cause Analysis

Child access is slow because it's using legacy mode, not v5 compact mode.
The accessor functions need to be rewritten to use offset-based access.

### 4.3 Fix Tasks

| Task | Status |
|------|--------|
| Update `taurus_element_first_child()` to use compact accessor | ⚠️ TODO |
| Update `taurus_element_next_sibling()` to use compact accessor | ⚠️ TODO |
| Update `taurus_element_get_attribute()` to use compact accessor | ⚠️ TODO |
| Update Node API to use compact offsets | ⚠️ TODO |

---

## PHASE 5: Update Tests

### 5.1 Test Status

| Test Suite | Current | After Legacy Removal |
|------------|---------|---------------------|
| Core DOM | 12/12 pass | ⚠️ TODO: Verify |
| XPath | 438/438 pass | ⚠️ TODO: Verify |
| Parser | 56/56 pass | ⚠️ TODO: Verify |
| Total | 56 tests | ⚠️ TODO: Run |

### 5.2 Test Tasks

| Task | Status |
|------|--------|
| Run all tests after legacy removal | ⚠️ TODO |
| Fix any test failures | ⚠️ TODO |
| Update test fixtures if needed | ⚠️ TODO |

---

## Implementation Order

| Priority | Phase | Task | Status |
|----------|-------|------|--------|
| **P0** | 1 | Delete legacy parser files (v2, v3, v4) | ⚠️ TODO |
| **P0** | 1 | Delete legacy allocator files | ⚠️ TODO |
| **P0** | 1 | Remove is_compact branching from all files | ⚠️ TODO |
| **P0** | 2 | Rename taurus_parse_v5 → taurus_parse | ⚠️ TODO |
| **P0** | 2 | Update main parse API to use v5 | ⚠️ TODO |
| **P1** | 4 | Fix child access for compact mode | ⚠️ TODO |
| **P1** | 4 | Fix attribute access for compact mode | ⚠️ TODO |
| **P1** | 5 | Run all tests and fix failures | ⚠️ TODO |
| **P2** | 3 | Create fixture generator | ⚠️ TODO |
| **P2** | 3 | Create parsing benchmark | ⚠️ TODO |
| **P2** | 3 | Create traversal benchmark | ⚠️ TODO |
| **P2** | 3 | Create access benchmark | ⚠️ TODO |
| **P2** | 3 | Create modification benchmark | ⚠️ TODO |
| **P2** | 3 | Create serialization benchmark | ⚠️ TODO |
| **P2** | 3 | Create XPath benchmark | ⚠️ TODO |
| **P2** | 3 | Create memory benchmark | ⚠️ TODO |
| **P2** | 3 | Create main orchestrator | ⚠️ TODO |

---

## Success Criteria

| Metric | Target | Current | Status |
|--------|--------|---------|--------|
| Parsing vs pugixml | **1.0-1.2x** | **1.20x** | ✅ **MET** |
| Traversal vs pugixml | **≥1.2x** | 2.55x | ✅ **MET** |
| Access vs pugixml | **≥1.0x** | 0.55x-0.87x | ❌ **NEEDS FIX** |
| Modification vs pugixml | **≥1.0x** | TBD | ⚠️ TODO |
| Serialization vs pugixml | **≥1.0x** | TBD | ⚠️ TODO |
| XPath vs libxml2 | **≥1.0x** | TBD | ⚠️ TODO |
| Memory vs pugixml | **≤50%** | TBD | ⚠️ TODO |
| Legacy code removed | **100%** | 0% | ❌ **IN PROGRESS** |

**ALL CRITERIA MUST PASS - NO EXCEPTIONS**

---

## Architectural Principles (NO HACKS)

1. **COMPACT-ONLY** - No dual-mode, no is_compact checks
2. **16-BYTE ELEMENTS** - v5 structure is optimal
3. **ZERO-CHECK ALLOCATOR** - Pure bump pointer, trust pre-allocation
4. **OFFSET-BASED** - All navigation uses 4-byte offsets
5. **SINGLE ALLOCATION** - Entire DOM in one block
6. **INLINE EVERYTHING** - Macros for hot path, no function calls

**NO HACKS. NO TECH DEBT. ARCHITECTURALLY SOUND SOLUTIONS ONLY.**

---

## Files Status

### Files to Keep

| File | Purpose | Status |
|------|---------|--------|
| src/taurus/memory/zero_check_alloc.h | Zero-check allocator | ✅ KEEP |
| src/taurus/parse/parser_v5.c | v5 parser - BEST | ✅ KEEP |
| src/taurus/dom/compact_element_v2.h | 16-byte structures | ✅ KEEP |
| src/taurus/dom/compact_accessor.c | Compact accessors | ✅ KEEP |
| src/taurus/dom/compact_accessor.h | Compact accessor header | ✅ KEEP |

### Files to Delete

| File | Purpose | Status |
|------|---------|--------|
| src/taurus/parse/parser_v2.c | Legacy parser | ⚠️ DELETE |
| src/taurus/parse/parser_v2_iterative.c | Legacy parser | ⚠️ DELETE |
| src/taurus/parse/parser_v3.c | Slower parser | ⚠️ DELETE |
| src/taurus/parse/parser_v4.c | Slower parser | ⚠️ DELETE |
| src/taurus/memory/compact_single_alloc.c | Legacy allocator | ⚠️ DELETE |
| src/taurus/memory/compact_single_alloc.h | Legacy allocator | ⚠️ DELETE |
| src/taurus/memory/ultra_fast_alloc.h | Duplicate | ⚠️ DELETE |
| src/taurus/dom/compact_element_v3.h | 20-byte version | ⚠️ DELETE |

### Files to Modify

| File | Changes | Status |
|------|---------|--------|
| src/CMakeLists.txt | Remove legacy files | ⚠️ TODO |
| src/taurus/taurus_internal.h | Remove legacy fields | ⚠️ TODO |
| src/taurus/taurus_parse_api.c | Use v5 only | ⚠️ TODO |
| src/taurus/taurus_document.c | Remove is_compact checks | ⚠️ TODO |
| src/taurus/taurus_element_api.c | Remove compact branches | ⚠️ TODO |
| src/taurus/dom/element.h | Remove inline branches | ⚠️ TODO |
| src/taurus/dom/element.c | Remove compact branches | ⚠️ TODO |
| src/include/taurus/types.h | Remove is_compact/is_legacy | ⚠️ TODO |
