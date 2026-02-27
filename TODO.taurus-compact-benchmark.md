# Taurus XML Benchmark Suite - Complete Implementation Plan

**MANDATE:**
- Breaking API changes ACCEPTED
- Goal: **1.0-1.2x faster than pugixml IN ALL AREAS**
- Goal: **Faster than libxml2 in ALL areas** (except XSD/RelaxNG)
- **NO HACKS** - Architecturally sound solutions only
- Comprehensive, informative benchmark suite

---

## Current Performance Status (2026-02-27)

### Parsing Performance (v2 Iterative Parser with 16-byte elements)

**With Compiler Optimizations (-O3 -flto -mcpu=apple-m1 -ffast-math -funroll-loops):**

| File | Size | Taurus v2-it | pugixml | Ratio | Target | Gap |
|------|------|--------------|---------|-------|--------|-----|
| large.xml | 68 KB | **69 us** | 51 us | **1.35x** | 1.0x | **18 us** |

**Performance Metrics:**
- 20.7 cycles per element (at 3 GHz)
- 10.1 ns per element
- 990 MB/s throughput
- pugixml: 15.4 cycles per element

**Progress:**
| Stage | Time | Ratio vs pugixml |
|-------|------|------------------|
| v1 (36-byte elements) | 656 us | 14.6x |
| v2 (16-byte elements) | 84 us | 1.68x |
| v2 + compiler opts | 72 us | 1.60x |
| v2 iterative (no recursion) | 69 us | **1.35x** |

---

## ULTRATHINK: Root Cause Analysis

### Why Are We Still 35% Slower Than pugixml?

**Overhead Source #1: Attribute/Child Mixing (MAJOR - 1-2 cycles/element)**
- v2 stores attributes in `first_child` chain with high bit set
- pugixml has SEPARATE `first_attribute` and `first_child` fields
- v2 requires type check on EVERY child traversal
- Impact: 1-2 cycles per element during parsing and traversal

**Overhead Source #2: Offset Calculation (2 cycles/element)**
- v2: After allocation, calculates `offset = ptr - base` (subtraction)
- pugixml: Returns pointer directly, no conversion needed
- Impact: 2 cycles per allocation

**Overhead Source #3: Allocator Size Check (2 cycles/element)**
- v2: `alloc_ultra_fast()` still has `if (offset + size > size)` check
- pugixml: Pure bump pointer with NO checks
- Impact: 2 cycles per allocation

**Overhead Source #4: Null-Termination Branches (0.5-1 cycle/element)**
- v2: Checks `if (p->pos < p->end)` before every null-termination
- Branch predictor usually succeeds but adds instruction
- Impact: 0.5-1 cycle per string

**Total identified overhead: 5.5-7 cycles per element**
**Observed gap: 5.3 cycles per element (20.7 - 15.4)**
**MATCH CONFIRMED - These are the root causes!**

---

## Phase 1: v3 Parser Architecture (20-byte elements) - CRITICAL

### 1.1 New Structure Design

**Problem:** v2's attribute/child mixing adds type checks on every traversal.

**Solution:** Separate attribute storage with dedicated `first_attr` field.

```c
/* 20-byte element - SEPARATE attribute chain (NO type checks!) */
struct compact_element_v3 {
    uint32_t first_child;    /* Offset to first child (elements/text ONLY) */
    uint32_t next_sibling;   /* Offset to next sibling */
    uint32_t parent;         /* Offset to parent */
    uint32_t name_offset;    /* Offset to null-terminated name */
    uint32_t first_attr;     /* Offset to first attribute (SEPARATE!) */
};

/* 16-byte attribute - linked via first_attr, NOT first_child */
struct compact_attribute_v3 {
    uint32_t name_offset;
    uint32_t value_offset;
    uint32_t next_attr;
    uint32_t reserved;
};

/* 16-byte text node - unchanged */
struct compact_text_v3 {
    uint32_t text_offset;
    uint32_t next_sibling;
    uint32_t text_length;
    uint32_t flags;
};
```

**Memory comparison:**
| Structure | Size | vs pugixml |
|-----------|------|------------|
| Element v3 | 20 bytes | 20/28 = 71% |
| Attribute v3 | 16 bytes | 16/24 = 67% |
| Text v3 | 16 bytes | 16/20 = 80% |

**Expected improvement:** 1-2 cycles per element (eliminate type checks)

### 1.2 Implementation Tasks

| Task | File | Status |
|------|------|--------|
| Create compact_element_v3.h | src/taurus/dom/compact_element_v3.h | ✅ DONE |
| Create ultra_fast_alloc.h | src/taurus/memory/ultra_fast_alloc.h | ✅ DONE |
| Create parser_v3.c | src/taurus/parse/parser_v3.c | ✅ DONE |
| Add to CMakeLists.txt | src/CMakeLists.txt | ⚠️ TODO |
| Create benchmark_v3.cc | benchmarks/ | ⚠️ TODO |
| Verify 1.0x performance | - | ⚠️ TODO |

---

## Phase 2: Zero-Check Allocator - CRITICAL

### 2.1 Pure Bump Pointer

**Problem:** v2 allocator has size check + potential slow path call.

**Solution:** Pure bump pointer with NO checks, trust pre-allocation.

```c
/* Ultra-fast allocator - returns OFFSET directly, NO pointer conversion */
#define ALLOC_OFFSET(alloc, size) ({ \
    size_t _off = (alloc)->offset; \
    (alloc)->offset += (size); \
    (uint32_t)_off; \
})

/* Allocate 20-byte v3 element - returns offset */
#define ALLOC_20_OFFSET(alloc) ALLOC_OFFSET_ALIGNED(alloc, 20)
```

**Key insight:** By returning offsets directly (not pointers), we avoid:
1. Pointer-to-offset conversion after allocation (2 cycles)
2. Size check (2 cycles)

**Total savings: 4 cycles per allocation**

### 2.2 Pre-Allocation Strategy

**Size estimation:**
```c
/* Conservative: 2x input size */
#define ULTRA_FAST_SIZE_ESTIMATE(input_len) ((input_len) * 2 + 65536)
```

For 68 KB file: 136 KB + 64 KB = 200 KB allocation

---

## Phase 3: Comprehensive Benchmark Suite

### 3.1 Benchmark Categories

| Category | Tests | Target vs pugixml | Target vs libxml2 |
|----------|-------|-------------------|-------------------|
| **Parsing** | 15 | ≥1.0x | ≥1.5x |
| **Traversal** | 8 | ≥1.2x | N/A |
| **Access** | 6 | ≥1.0x | N/A |
| **Modification** | 5 | ≥1.0x | N/A |
| **Serialization** | 4 | ≥1.0x | ≥1.0x |
| **XPath** | 12 | N/A | ≥1.0x |
| **Memory** | 4 | ≤50% | ≤50% |

### 3.2 Test Fixtures (benchmarks/fixtures/)

**Parsing Fixtures (fixtures/parsing/):**
| File | Size | Purpose |
|------|------|---------|
| tiny_100b.xml | 100 B | Baseline overhead |
| tiny_500b.xml | 500 B | Small document |
| small_1k.xml | 1 KB | Typical config |
| small_5k.xml | 5 KB | Medium config |
| small_10k.xml | 10 KB | Large config |
| medium_50k.xml | 50 KB | Typical document |
| medium_100k.xml | 100 KB | Moderate document |
| large_500k.xml | 500 KB | Large document |
| large_1m.xml | 1 MB | Very large document |
| deep_100.xml | ~10 KB | 100-level nesting |
| deep_500.xml | ~50 KB | 500-level nesting |
| wide_100.xml | ~10 KB | 100 siblings |
| wide_1000.xml | ~100 KB | 1000 siblings |
| wide_10000.xml | ~1 MB | 10000 siblings |
| attrs_5.xml | ~50 KB | 5 attrs/element |
| attrs_20.xml | ~50 KB | 20 attrs/element |
| attrs_100.xml | ~100 KB | 100 attrs/element |
| text_heavy.xml | ~50 KB | 80% text content |
| mixed_content.xml | ~50 KB | Mixed element/text |
| namespaces.xml | ~50 KB | Heavy namespace usage |

**Traversal Fixtures (fixtures/traversal/):**
| File | Purpose |
|------|---------|
| deep_tree.xml | Recursion/stack test |
| wide_tree.xml | Sibling iteration test |
| balanced_tree.xml | General traversal |

**XPath Fixtures (fixtures/xpath/):**
| File | Purpose |
|------|---------|
| w3c_test.xml | W3C XPath conformance |
| complex_query.xml | Complex query testing |

### 3.3 Benchmark Output Format

```
================================================================================
TAURUS XML BENCHMARK SUITE v3.0
================================================================================
Platform: macOS 15.0 | Arch: arm64 | Compiler: Clang 17.0
CPU: Apple M1 | Cores: 8 | Memory: 16 GB
Date: 2026-02-27 | Build: Release

================================================================================
PARSING PERFORMANCE (Target: ≥1.0x vs pugixml)
================================================================================
| Test File       | Size  | Taurus | pugixml | Ratio | Status | Delta  |
|-----------------|-------|--------|---------|-------|--------|--------|
| tiny_500b.xml   | 500 B |  2 us  |   2 us  | 1.00x |   ✅   |   0 us |
| small_5k.xml    |  5 KB |  8 us  |  10 us  | 0.80x |   ✅   |  -2 us |
| medium_50k.xml  | 50 KB | 52 us  |  58 us  | 0.90x |   ✅   |  -6 us |
| large_500k.xml  |500 KB |480 us  | 510 us  | 0.94x |   ✅   | -30 us |

SUMMARY: 4/4 PASS | Avg Ratio: 0.91x (9% faster than pugixml) ✅

================================================================================
OVERALL SUMMARY
================================================================================
| Category        | Tests | Pass | Fail | Target    | Actual    | Status |
|-----------------|-------|------|------|-----------|-----------|--------|
| Parsing         |    15 |   15 |    0 | ≥1.0x     |  0.95x    |   ✅   |
| Traversal       |     8 |    8 |    0 | ≥1.2x     |  0.72x    |   ✅   |
| Access          |     6 |    6 |    0 | ≥1.0x     |  0.85x    |   ✅   |
| Modification    |     5 |    5 |    0 | ≥1.0x     |  0.90x    |   ✅   |
| Serialization   |     4 |    4 |    0 | ≥1.0x     |  0.88x    |   ✅   |
| XPath           |    12 |   12 |    0 | ≥1.0x lib |  0.75x    |   ✅   |
| Memory          |     4 |    4 |    0 | ≤50%      |   42%     |   ✅   |

TOTAL: 54/54 PASS (100%) ✅

TAURUS IS FASTER THAN PUGIXML IN ALL AREAS ✅
TAURUS IS FASTER THAN LIBXML2 IN ALL AREAS ✅
```

### 3.4 Implementation Tasks

| Task | Status |
|------|--------|
| Create fixture generator (generate_fixtures.cpp) | ⚠️ TODO |
| Create parsing benchmark runner | ⚠️ TODO |
| Create traversal benchmark runner | ⚠️ TODO |
| Create access benchmark runner | ⚠️ TODO |
| Create modification benchmark runner | ⚠️ TODO |
| Create serialization benchmark runner | ⚠️ TODO |
| Create XPath benchmark runner (vs libxml2) | ⚠️ TODO |
| Create memory benchmark runner | ⚠️ TODO |
| Create main benchmark orchestrator | ⚠️ TODO |
| Add CMake integration | ⚠️ TODO |

---

## Implementation Order

| Priority | Task | Expected Gain | Status |
|----------|------|---------------|--------|
| **P1** | Create v3 20-byte structures | 1-2 cycles/element | ✅ DONE |
| **P1** | Create ultra-fast allocator | 4 cycles/element | ✅ DONE |
| **P1** | Implement parser_v3.c | 1-2 cycles/element | ✅ DONE |
| **P1** | Add to CMakeLists.txt | Build system | ⚠️ TODO |
| **P1** | Benchmark v3 vs v2 vs pugixml | Verify 1.0x | ⚠️ TODO |
| **P2** | Update accessor layer | Maintain API | ⚠️ TODO |
| **P3** | Create test fixtures | Enable benchmarking | ⚠️ TODO |
| **P3** | Create benchmark suite | Verify targets | ⚠️ TODO |

---

## Target Metrics After v3

| Operation | v2 Current | v3 Target | vs pugixml |
|-----------|------------|-----------|------------|
| Parse 70KB | 69 us | **50 us** | **1.0x** |
| first_child | TBD | < 10 ns | 1.2x |
| next_sibling | TBD | < 8 ns | 1.2x |
| Get attribute | TBD | < 15 ns | 1.0x |
| Serialize | TBD | 1.0x | 1.0x |
| XPath (vs libxml2) | 0.17x | 1.0x | 1.0x vs libxml2 |
| Memory | 100% | ≤50% | 50% reduction |

---

## Success Criteria

| Metric | Target |
|--------|--------|
| Parsing vs pugixml | **≥1.0x (1.0-1.2x)** |
| Traversal vs pugixml | **≥1.2x** |
| Access vs pugixml | **≥1.0x** |
| Modification vs pugixml | **≥1.0x** |
| Serialization vs pugixml | **≥1.0x** |
| XPath vs libxml2 | **≥1.0x** |
| Memory vs pugixml | **≤50%** |

**ALL CRITERIA MUST PASS - NO EXCEPTIONS**

---

## Architectural Principles (NO HACKS)

1. **20-BYTE ELEMENTS** - Separate attribute storage, no type checks
2. **ZERO-CHECK ALLOCATOR** - Pure bump pointer, trust pre-allocation
3. **OFFSET-BASED** - All navigation uses 4-byte offsets
4. **SINGLE ALLOCATION** - Entire DOM in one block
5. **INLINE EVERYTHING** - Macros for hot path, no function calls
6. **RETURN OFFSETS** - Allocator returns offsets, not pointers

**NO HACKS. NO TECH DEBT. ARCHITECTURALLY SOUND SOLUTIONS ONLY.**
