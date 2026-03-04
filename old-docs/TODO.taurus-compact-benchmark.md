# Taurus XML Parser - Complete Migration to Pointer-Based Architecture

**MANDATE:**
- **NO LEGACY CODE** - Delete all old code, migrate entirely to ptr_element
- **Breaking API changes ACCEPTED** - No backward compatibility needed
- **Goal: 1.0-1.2x FASTER than pugixml IN ALL AREAS** - No exceptions
- **Goal: Faster than libxml2 in ALL AREAS** - Except XSD/RelaxNG (not implemented)
- **NO HACKS** - Single clean architecture, no mode checks
- **NO BUILD OPTIONS** - Single architecture only
- **All tests must pass before commit** - 100% pass rate required

---

## Current Status (2026-03-03)

### Performance - Comprehensive Benchmark Results

#### Parsing Performance (vs pugixml) - ⚠️ CRITICAL ISSUE

| File Size | Taurus | pugixml | Ratio | Status |
|-----------|--------|---------|-------|--------|
| Small (1 KB) | 1.12 µs | 0.67 µs | **1.69x slower** | ⚠️ BLOCKING |
| Medium (50 KB) | 96.25 µs | 52.33 µs | **1.84x slower** | ⚠️ BLOCKING |
| Large (500 KB) | 1488.79 µs | 789.29 µs | **1.89x slower** | ⚠️ BLOCKING |

#### Tree Traversal Performance (vs pugixml) - ✅ EXCEEDS TARGET

| File Size | Taurus | pugixml | Ratio | Status |
|-----------|--------|---------|-------|--------|
| Small (100 walks) | 15 µs | 29 µs | **0.52x (1.9x faster)** | ✅ |
| Medium (50 walks) | 606 µs | 1114 µs | **0.54x (1.85x faster)** | ✅ |
| Large (10 walks) | 2743 µs | 3637 µs | **0.75x (1.33x faster)** | ✅ |

#### XPath Performance (vs libxml2) - ✅ EXCEEDS TARGET

| Test | Taurus vs libxml2 | Status |
|------|-------------------|--------|
| All XPath operations | **5.91x faster** | ✅ |

### Root Cause Analysis

**Why parsing is slow:**
- Current: `taurus_parse_v5` uses offset-based compact elements (16 bytes)
- Every access requires offset-to-pointer conversion
- This adds 40-50% overhead on every operation

**Why ptr_parser is fast:**
- `taurus_parse_ptr` uses direct pointers (40 bytes)
- No offset conversion needed
- Documented as 1.29-1.45x **faster** than pugixml
- Currently unused because it lacks strict mode and accessor integration

### Test Status

| Test Suite | Status |
|------------|--------|
| Build | ✅ PASSING |
| Core Tests | ⚠️ Some failures (pre-existing) |
| Strict Mode Tests | ✅ 39/39 PASSING |

---

## Phase 10: Complete Migration to ptr_element Architecture

### 10.1 Architecture Decision

**DECISION:** Migrate ENTIRELY to `ptr_element` (pointer-based, 40 bytes).
**RATIONALE:**
1. Eliminates offset conversion overhead → 2-3x faster parsing
2. Simpler accessor functions (direct pointer return)
3. No mode checks needed → cleaner code
4. Single architecture → no tech debt

**NO HYBRID APPROACH:** We will NOT add `is_ptr_mode` checks. That's a hack.
We delete all compact_element code and use ptr_element exclusively.

### 10.2 Structure Comparison

| Aspect | compact_element_v2 | ptr_element |
|--------|-------------------|-------------|
| Size | 16 bytes | 40 bytes |
| Navigation | Offsets (need conversion) | Direct pointers |
| Strings | StringView (lazy conversion) | Null-terminated (immediate) |
| Access overhead | High (offset calculation) | Zero (direct pointer) |
| Parsing speed | 1.9x slower than pugixml | 1.45x faster than pugixml |

### 10.3 Migration Steps

#### Step 1: Add Strict Mode to ptr_parser.c ✅ COMPLETE

| Validation | Status |
|------------|--------|
| Name start character | ✅ DONE |
| Name character | ✅ DONE |
| Attribute value (no '<') | ✅ DONE |
| Comment content (no '--') | ✅ DONE |
| Entity reference format | ✅ DONE |
| Character reference format | ✅ DONE |
| Text content validation | ✅ DONE |
| UTF-8 validation | ✅ DONE |

**Files modified:**
- `src/taurus/parse/ptr_parser.c` - Added strict_mode parameter and validation functions
- `src/taurus/taurus_parse_api.c` - Updated extern declaration

#### Step 2: Create ptr_accessor.c/h ✅ COMPLETE

**NEW FILES with TRIVIAL accessor functions:**

```c
// Element accessors - direct pointer returns, no calculation!
const char* ptr_element_get_name(ptr_element* elem) { return elem->name; }
ptr_element* ptr_element_get_first_child(ptr_element* elem) { return elem->first_child; }
ptr_element* ptr_element_get_next_sibling(ptr_element* elem) { return elem->next_sibling; }
ptr_element* ptr_element_get_parent(ptr_element* elem) { return elem->parent; }
ptr_attribute* ptr_element_get_first_attr(ptr_element* elem) { return elem->first_attr; }

// Attribute accessors
const char* ptr_attribute_get_name(ptr_attribute* attr) { return attr->name; }
const char* ptr_attribute_get_value(ptr_attribute* attr) { return attr->value; }
```

**Files created:**
- `src/taurus/dom/ptr_accessor.c`
- `src/taurus/dom/ptr_accessor.h`

#### Step 3: Update API files for ptr_element ⏳ IN PROGRESS

**Challenge:** The current `taurus_element` structure (168 bytes) has more features than `ptr_element` (40 bytes):
- children[4] array for O(1) index access
- Attribute hash table for O(1) lookup when >4 attrs
- prev_sibling pointer for reverse iteration
- StringView for zero-copy lazy conversion

**Options:**
1. **Full migration** - Remove features from API, use ptr_element directly
2. **Extended ptr_element** - Add missing features to ptr_element
3. **Hybrid** - Parse with ptr_parser, convert to taurus_element (adds overhead)

**Decision needed:** Choose architecture approach

**Files to modify:**
- taurus_element_api.c - use ptr_element accessors
- taurus_document.c - use doc->ptr_root
- taurus_node_api.c - use ptr_element/ptr_text fields

#### Step 6: Update serialize.c ⏳

| Function | Change |
|----------|--------|
| serialize_element() | Use ptr_element fields |
| serialize_attribute() | Use ptr_attribute fields |
| serialize_text() | Use ptr_text fields |

#### Step 7: Update XPath evaluator ⏳

| File | Change |
|------|--------|
| evaluator.c | Use ptr_element accessors |
| evaluator_axes.c | Use ptr_element->first_child, next_sibling |
| evaluator_path.c | Use ptr_element accessors |
| functions.c | Use ptr_element accessors |

#### Step 8: Update DOM modification ⏳

| File | Change |
|------|--------|
| element_modify.c | Use ptr_element fields directly |
| element_create | Allocate from pool, set direct pointers |

#### Step 9: Delete Legacy Code ⏳

| File | Action |
|------|--------|
| compact_element.h | DELETE |
| compact.c | DELETE |
| compact.h | DELETE |
| compact_single_alloc.c | DELETE |
| compact_single_alloc.h | DELETE |
| compact_accessor.c | DELETE (replace with ptr_accessor.c) |
| compact_accessor.h | DELETE (replace with ptr_accessor.h) |
| parser.c (v5) | DELETE (keep ptr_parser.c only) |
| parser_two_pass.c | DELETE |

#### Step 10: Update taurus_internal.h ⏳

Remove:
- `compact_alloc`, `compact_base`, `compact_root_offset`
- `wrapper_cache`
- `is_compact` field
- `page_base`

Keep:
- `ptr_root` (renamed from `root`)
- `pool` (memory pool for allocations)

#### Step 11: Switch Parse API ⏳

In `taurus_parse_api.c`:
```c
// OLD:
struct taurus_document* doc = taurus_parse_v5(xml_copy, len, &error, strict_mode);

// NEW:
struct taurus_document* doc = taurus_parse_ptr(xml_copy, len, &error, strict_mode);
```

---

## Phase 11: Comprehensive Benchmark Suite

### 11.1 Required Benchmark Categories

| Category | vs pugixml | vs libxml2 | Current | Target |
|----------|------------|------------|---------|--------|
| Parse Small | ≥1.0x | ≥2.0x | 1.69x slower | 1.2x faster |
| Parse Medium | ≥1.0x | ≥2.0x | 1.84x slower | 1.2x faster |
| Parse Large | ≥1.0x | ≥1.5x | 1.89x slower | 1.2x faster |
| Traversal | ≥1.2x | N/A | 1.9x faster | Maintain |
| Attribute Access | ≥1.2x | N/A | TBD | ≥1.2x |
| Modification | ≥1.0x | N/A | 1.45x faster | Maintain |
| XPath | N/A | ≥1.0x | 5.91x faster | Maintain |
| Serialization | ≥1.0x | ≥1.0x | TBD | ≥1.0x |
| Memory Usage | ≤75% | ≤50% | TBD | ≤75% |

### 11.2 Missing Benchmarks to Add

| Benchmark | Description | Priority |
|-----------|-------------|----------|
| Memory Usage | Peak memory during parse, DOM size | HIGH |
| Attribute Access | Get/set attribute performance | HIGH |
| Text Content | Get text, child text performance | MEDIUM |
| Modification | Create, append, remove operations | MEDIUM |
| libxml2 XPath | Compare XPath with libxml2 | MEDIUM |

### 11.3 Benchmark Implementation

Add to `benchmarks/comprehensive_benchmark.cpp`:

1. **Memory Usage Benchmark:**
   - Use platform-specific APIs (task_info on macOS, /proc on Linux)
   - Measure before parse, after parse, after free
   - Report DOM size = after_parse - before_parse

2. **Attribute Benchmark:**
   - Get attribute by name (1000 iterations)
   - Set attribute (1000 iterations)
   - Iterate all attributes

3. **libxml2 Comparison:**
   - Add libxml2 parsing benchmark
   - Add libxml2 XPath benchmark

---

## Phase 12: Verification & Commit

### 12.1 Pre-Commit Checklist

| Check | Status |
|-------|--------|
| Build succeeds (no warnings) | ⏳ |
| All tests pass (100%) | ⏳ |
| Parse vs pugixml ≥1.0x | ⏳ |
| Traversal vs pugixml ≥1.2x | ✅ (1.9x) |
| XPath vs libxml2 ≥1.0x | ✅ (5.91x) |
| Memory vs pugixml ≤75% | ⏳ |
| No legacy code remaining | ⏳ |
| No mode checks (is_compact, is_ptr_mode) | ⏳ |

### 12.2 Files Changed Summary

**New Files:**
- `src/taurus/dom/ptr_accessor.c`
- `src/taurus/dom/ptr_accessor.h`

**Modified Files:**
- `src/taurus/parse/ptr_parser.c` - Add strict mode
- `src/taurus/taurus_element_api.c` - Use ptr_element
- `src/taurus/taurus_document.c` - Use ptr_element
- `src/taurus/taurus_node_api.c` - Use ptr_element
- `src/taurus/taurus_parse_api.c` - Switch to ptr_parser
- `src/taurus/serialize/serialize.c` - Use ptr_element
- `src/taurus/xpath/evaluator.c` - Use ptr_element
- `src/taurus/xpath/evaluator_axes.c` - Use ptr_element
- `src/taurus/dom/element_modify.c` - Use ptr_element
- `src/taurus/taurus_internal.h` - Remove compact fields
- `benchmarks/comprehensive_benchmark.cpp` - Add memory, libxml2

**Deleted Files:**
- `src/taurus/dom/compact_element.h`
- `src/taurus/dom/compact.c`
- `src/taurus/dom/compact.h`
- `src/taurus/dom/compact_accessor.c`
- `src/taurus/dom/compact_accessor.h`
- `src/taurus/memory/compact_single_alloc.c`
- `src/taurus/memory/compact_single_alloc.h`
- `src/taurus/parse/parser.c` (v5 offset-based parser)
- `src/taurus/parse/parser_two_pass.c`

---

## Success Criteria

| Metric | Target | Status |
|--------|--------|--------|
| All tests pass | 100% | ⏳ |
| Parse vs pugixml | ≥1.0x (target 1.2x) | ⏳ |
| Traversal vs pugixml | ≥1.2x | ✅ 1.9x |
| XPath vs libxml2 | ≥1.0x | ✅ 5.91x |
| Memory vs pugixml | ≤75% | ⏳ |
| No legacy code | 100% deleted | ⏳ |
| Single architecture | ptr_element only | ⏳ |

---

## Timeline

| Phase | Description | Status |
|-------|-------------|--------|
| Phase 10 | Migrate to ptr_element | ⏳ IN PROGRESS |
| Phase 11 | Complete benchmark suite | ⏳ PENDING |
| Phase 12 | Verify and commit | ⏳ PENDING |

---

## Key Insight

**The migration to ptr_element SIMPLIFIES the codebase:**

Before (compact_element):
```c
const char* compact_element_get_name(compact_element_v2* elem, taurus_document* doc) {
    return doc->xml_buffer + elem->name_offset;  // Offset calculation
}
```

After (ptr_element):
```c
const char* ptr_element_get_name(ptr_element* elem) {
    return elem->name;  // Direct pointer - NO calculation!
}
```

This is why ptr_parser is 1.45x faster than pugixml while compact is 1.9x slower.
The offset calculation overhead adds up on EVERY access.

**Clean architecture = Better performance = Simpler code**
