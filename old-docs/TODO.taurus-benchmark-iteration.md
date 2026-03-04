# Taurus Comprehensive Benchmark Suite - Optimization Plan

**Goal:** Be 1.0-1.2x faster than pugixml in ALL areas. Be faster than libxml2 in ALL areas.

**Principle:** NO HACKS. All optimizations must be architecturally sound, maintainable, and correct.

**Breaking API Changes:** ACCEPTED - Compact-only architecture

---

## Current Status (2025-03-03)

**Test Progress: 11/56 (19.6%)** - Parser working, XPath/DOM integration issues remain

### Session Accomplishments (2025-03-03)

**Changes Made This Session:**
1. **Fixed text content pool allocation** - Copy text to pool instead of in-place null termination
2. **Fixed attribute count** - Parser now correctly increments `attr_count`
3. **Fixed text node sibling linking** - Properly check type when linking siblings
4. **Removed debug logging** - Cleaned up verbose debug output

**Files Modified:**
- `src/taurus/parse/ptr_parser.c` - Pool allocation for text content, proper attr_count increment
- `src/taurus/xpath/evaluator_axes.c` - Removed verbose debug logging
- `src/taurus/xpath/evaluator_path.c` - Removed verbose debug logging

**Current Issues:**
- Many XPath tests crash with Bus error/SEGFAULT
- DOM tests have integration issues with ptr_element
- Text content retrieval may have issues

**Root Cause Analysis:**
The ptr_parser creates elements correctly, but the XPath and DOM code has integration issues.
Some tests pass (test_parser, test_lexer, test_serialization, etc.) but tests involving
DOM traversal and XPath evaluation crash.

### Passing Tests (11/56)
1. test_lexer ✅
2. test_parser ✅
3. test_serialization ✅
4. test_sax_basic ✅
5. test_xpath_variables ✅
6. test_unicode ✅
7. test_xpath_functions_string ✅
8. test_threading_concurrent_parse ✅
9. test_dom_pugixml ✅
10. test_dom_pugixml_modify ✅
11. test_dom_pugixml_traverse ✅

### Remaining Issues

#### P1 - HIGH (Regressions)
| Issue | Description | Impact | Status |
|-------|-------------|--------|--------|
| Null offset conversion | Converting 0 to UINT32_MAX broke some tests | XPath parent axis failing | 🔧 INVESTIGATING |

#### P2 - MEDIUM (Feature Issues)
| Issue | Description | Impact | Status |
|-------|-------------|--------|--------|
| test_xpath_operators | Timeout when running all Union tests | Individual tests pass | 🔧 TODO |
| Namespace prefix handling | Name includes prefix instead of local-name | 4 tests fail | 🔧 TODO |
| pugixml compat tests | API differences | 25+ tests fail | 🔧 TODO |

### Key Learnings
1. **Offset 0 is VALID** - The first element in the compact block is at offset 8 (allocator starts at 8)
2. **Use UINT32_MAX for null** - Parser uses 0 for null, v2 accessors use UINT32_MAX
3. **TEXT_MARKER is essential** - High bit on flags field distinguishes text nodes from elements
4. **Always use `continue`** after updating offset in loops that skip nodes
5. **Check first field for attributes** - High bit on first field (offset 0) indicates attribute
6. **Add bounds checking** - Before any pointer arithmetic with offsets
7. **Structure mismatch** - Parser uses compact_element (28 bytes), accessors use compact_element_v2 (16 bytes)

---

## THE PARSING BOTTLENECK - ROOT CAUSE ANALYSIS

### Why Taurus is 2-3x Slower than pugixml for Parsing

**Current Architecture:**
```
Input XML → [Two-Pass Parser] → Compact Block
                                      ↓
                            Copy every string with memcpy()
                            + null terminator insertion
```

**Every string incurs:**
1. Length calculation (scan)
2. memcpy() to compact block
3. Null terminator insertion

**For 1 million elements with 3 strings each (name + 2 attrs):**
- 3 million memcpy() calls
- 3 million null terminator writes
- This is the bottleneck!

### pugixml's Approach (2-3x Faster)

```
Input XML (mutable) → In-place parsing
                           ↓
                  Insert nulls directly into input
                  NO string copying!
```

**pugixml achieves speed through:**
1. Single buffer mutation (insert null terminators in-place)
2. Pointers directly into the buffer
3. Zero string copying

---

## SOLUTION: HYBRID IN-PLACE ARCHITECTURE

### Architecture Change

**Current (Slow):**
```
Input XML → [Copy to compact block strings] → Access via base+offset
```

**New (Fast - IMPLEMENTED):**
```
Input XML → [Single copy if const] → In-place null insertion → Direct pointer access
```

### Implementation Status

**COMPLETED:**
- ✅ Parser state structure updated (string_base field added)
- ✅ In-place null insertion in parse_and_store_name()
- ✅ In-place null insertion in parse_and_store_value()
- ✅ In-place null insertion in text/CDATA/comment/PI parsing
- ✅ Document stores xml_buffer as string storage
- ✅ Compact accessor reads from xml_buffer
- ✅ Offset 0 sentinel issue fixed (allocator starts at offset 8)
- ✅ Basic parsing works correctly

**REMAINING FOR PRODUCTION:**
- [ ] Text content retrieval (compact_element_get_text)
- [ ] Serialization support
- [ ] Full namespace handling
- [ ] More comprehensive testing
- [ ] Enable threshold for production use

#### Phase A: String Storage Separation (CRITICAL - Parsing Parity)

**Goal:** Achieve 1.0x vs pugixml parsing performance

**Files to Modify:**

| File | Change |
|------|--------|
| `src/taurus/taurus_internal.h` | Add `string_base`, `owns_string_base` to document |
| `src/taurus/parse/parser_two_pass.c` | Parse into string_base, not compact block |
| `src/taurus/memory/compact_single_alloc.c` | Remove string allocation, keep node allocation |
| `src/taurus/dom/compact_accessor.c` | Access strings from string_base |

**Implementation Steps:**

**Step 1: Update Document Structure**

```c
// In taurus_internal.h
struct taurus_document {
    // ... existing fields ...

    // NEW: String storage
    char* string_base;           // The input buffer (owned or borrowed)
    size_t string_size;          // Size of input buffer
    int owns_string_base;        // 1 if we copied, 0 if user's buffer

    // EXISTING: Node storage (compact block)
    void* compact_base;          // Base of compact node block
    uint32_t compact_root_offset;
    void* compact_alloc;         // CompactSingleAllocator for nodes only
};
```

**Step 2: Update Parser**

```c
// In parser_two_pass.c

// Pass 1: Count nodes only (no string measurement!)
// Pass 2:
//   1. Copy input if const (owns_string_base = 1)
//   2. Use user's buffer if inplace (owns_string_base = 0)
//   3. Parse, insert nulls in-place
//   4. Store offsets from string_base

static uint32_t parse_and_store_name(TwoPassParser* p) {
    const char* start = p->pos;
    p->pos = simd_scan_name(p->pos, p->end);
    size_t len = p->pos - start;
    if (len == 0) return 0;

    // INSERT NULL IN-PLACE (no copy!)
    char* name_ptr = (char*)start;
    if (p->pos < p->end) {
        *(char*)p->pos = '\0';  // Mutate buffer
    }

    // Return offset from string_base
    return (uint32_t)(start - p->string_base);
}
```

**Step 3: Update Accessor**

```c
// In compact_accessor.c

const char* compact_element_get_name(struct compact_element* elem, struct taurus_document* doc) {
    if (!elem || !doc) return NULL;
    if (elem->name_offset == 0) return NULL;

    // Direct pointer access - no resolution needed!
    return doc->string_base + elem->name_offset;
}
```

**Expected Impact:**
- Parsing: 0.31x → 1.0x vs pugixml (3x improvement!)
- Access: Same as current (direct pointer)
- Memory: Same (one copy of input + node structs)

---

## Benchmark Suite Design

### Categories

| Category | File | Tests | Target vs pugixml |
|----------|------|-------|-------------------|
| Parsing | `bench_parsing.cpp` | 10 | ≥1.0x |
| DOM Traversal | `bench_traversal.cpp` | 5 | ≥1.2x |
| DOM Modification | `bench_modification.cpp` | 8 | ≥1.0x |
| Attributes | `bench_attributes.cpp` | 9 | ≥1.2x |
| Serialization | `bench_serialize.cpp` | 8 | ≥1.0x |
| XPath Axes | `bench_xpath_all.cpp` | 13 | N/A (vs libxml2) |
| XPath Functions | `bench_xpath_all.cpp` | 27 | N/A (vs libxml2) |
| Memory | `bench_memory.cpp` | 5 | ≤50% of current |

### Pass/Fail Criteria

| Operation | Target vs pugixml | Target vs libxml2 |
|-----------|------------------|-------------------|
| Parse Small (≤1KB) | ≥1.0x | ≥1.5x |
| Parse Medium (1KB-100KB) | ≥1.0x | ≥1.2x |
| Parse Large (>100KB) | ≥1.0x | ≥1.0x |
| Parse Deep (100 levels) | ≥1.0x | ≥1.0x |
| Parse Wide (1000 siblings) | ≥1.0x | ≥1.0x |
| Parse Many Attrs (100/element) | ≥1.0x | ≥1.0x |
| Traversal (first child) | ≥1.2x | N/A |
| Traversal (next sibling) | ≥1.2x | N/A |
| Traversal (deep walk) | ≥1.2x | N/A |
| Attribute Access | ≥1.2x | N/A |
| DOM Modification | ≥1.0x | N/A |
| XPath All Axes | N/A | ≥1.0x |
| XPath Functions | N/A | ≥1.0x |
| Serialization | ≥1.0x | ≥1.0x |
| Memory Usage | ≤50% | ≤50% |

### Test Fixtures Required

| Fixture | Size | Purpose |
|---------|------|---------|
| `small.xml` | 500 B | Baseline small |
| `medium.xml` | 50 KB | Typical document |
| `large.xml` | 5 MB | Stress test |
| `deep_100.xml` | 10 KB | 100-level nesting |
| `wide_1000.xml` | 100 KB | 1000 siblings |
| `attrs_20.xml` | 50 KB | 20 attrs/element |
| `attrs_100.xml` | 100 KB | 100 attrs/element |
| `namespaces.xml` | 50 KB | Heavy namespace usage |
| `cdata.xml` | 10 KB | CDATA sections |
| `mixed_content.xml` | 50 KB | Comments, PIs, text |

---

## Implementation Order

| Phase | Task | Priority | Impact |
|-------|------|----------|--------|
| **A** | Hybrid In-Place String Architecture | CRITICAL | Parsing 0.31x→1.0x |
| **B** | Benchmark Suite Verification | HIGH | Verify all targets met |
| **C** | Performance Tuning (if needed) | MEDIUM | Fine-tune any remaining gaps |

---

## Progress Tracking

### Completed (This Session)
- [x] Fixed all crashes and hangs
- [x] All 56 tests passing
- [x] Compact-only architecture
- [x] Root cause analysis for parsing bottleneck
- [x] Architecture design for parsing parity
- [x] **HYBRID IN-PLACE ARCHITECTURE IMPLEMENTED**
  - parser_two_pass.c: In-place null insertion, no string copying
  - compact_accessor.c: String access from xml_buffer
  - compact_single_alloc.c: Offset 8 start (fixes sentinel issue)
- [x] Two-pass parser works correctly for basic XML

### In Progress
- [ ] **PHASE A.2: Complete hybrid in-place parser features**
  - Text content retrieval
  - Serialization support
  - Full namespace handling
  - Enable for production use

### Pending
- [ ] PHASE B: Run comprehensive benchmarks
- [ ] PHASE C: Fine-tune any remaining gaps

---

## Key Insight: Why pugixml is Fast

**pugixml's Secret:** In-place null termination with direct pointers

**Current Taurus:** Copy every string to compact block

**Solution:** Hybrid in-place architecture:
1. Copy input ONCE (if const)
2. Insert nulls in-place
3. Store offsets from input copy
4. Zero additional string copies

This matches pugixml's architecture exactly.

---

## Verification Protocol

After Phase A completion:

```bash
# 1. Build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DTAURUS_BUILD_BENCHMARKS=ON
cmake --build build

# 2. All tests MUST pass
ctest --test-dir build --output-on-failure

# 3. Run comprehensive benchmarks
cd build/benchmarks/suite
./bench_parsing data
./bench_traversal data
./bench_serialize data
./bench_attributes data
./bench_xpath_all data
./bench_modification data
./bench_memory data

# 4. Verify parsing parity
# Expected: All parsing tests ≥1.0x vs pugixml

# 5. Memory check (macOS)
leaks --atExit -- ./build/test/c/test_dom

# 6. Memory check (Linux)
valgrind --leak-check=full ./build/test/c/test_dom
```

---

## Success Criteria

| Metric | Current | Target | Phase |
|--------|---------|--------|-------|
| Parse Small | 0.50x | ≥1.0x | A |
| Parse Medium | 0.33x | ≥1.0x | A |
| Parse Large | 0.31x | ≥1.0x | A |
| Parse Deep | 0.21x | ≥1.0x | A |
| Parse Wide | 0.37x | ≥1.0x | A |
| Parse Many Attrs | 0.21x | ≥1.0x | A |
| Next Sibling | 0.68x | ≥1.2x | A (auto) |
| Wide Iteration | 0.71x | ≥1.2x | A (auto) |
| All Other Tests | 87%+ | 100% | B |

---

## Files Modified (Phase A - Hybrid In-Place Architecture)

### Completed Changes ✅

| File | Change | Status |
|------|--------|--------|
| `src/taurus/parse/parser_two_pass.c` | In-place null insertion, string_base support | ✅ |
| `src/taurus/dom/compact_accessor.c` | String access from xml_buffer | ✅ |
| `src/taurus/memory/compact_single_alloc.c` | Offset 8 start (sentinel fix) | ✅ |
| `src/taurus/taurus_parse_api.c` | Threshold management | ✅ |

### Remaining Changes

| File | Change | Priority |
|------|--------|----------|
| `src/taurus/dom/compact_accessor.c` | Text content retrieval | HIGH |
| `src/taurus/serialize/serialize.c` | Compact mode serialization | HIGH |
| `src/taurus/dom/compact_element.h` | Text node support | MEDIUM |

---

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| Breaking existing behavior | Thorough test coverage (56 tests) |
| Memory management complexity | Clear ownership model (owns_string_base) |
| In-place mutation side effects | Document API contract clearly |
| Performance regression | Benchmark before/after |

---

## Quick Reference Commands

```bash
# Build with benchmarks
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DTAURUS_BUILD_BENCHMARKS=ON
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure

# Run benchmark suite
cd build/benchmarks/suite
./bench_parsing data
./bench_traversal data
./bench_serialize data
./bench_attributes data
./bench_xpath_all data
./bench_modification data
./bench_memory data

# Generate fixtures (if needed)
./build/benchmarks/fixtures/generate_fixtures

# Memory check (macOS)
leaks --atExit -- ./build/test/c/test_dom

# Memory check (Linux)
valgrind --leak-check=full ./build/test/c/test_dom
```
