# Taurus ptr_element Architecture Migration - Master Plan

**Deadline: ASAP - Compressed Timeline**

**Principle:** Correctness of architecture over temporary test failures. Tests will be updated to match the new architecture.

---

## Executive Summary

### Goal
Migrate Taurus entirely to the `ptr_element` (pointer-based) architecture:
- Delete all legacy compact/offset-based code
- Use direct pointers for O(1) access
- Target: 1.0-1.2x faster than pugixml in all areas
- Target: 100% test pass rate

### Current Status (2025-03-03)
- **Test Progress: 11/56 (19.6%)**
- **Parser: WORKING** - ptr_parser creates correct tree structure
- **XPath/DOM: INTEGRATION ISSUES** - Type mismatches causing crashes

---

## Architecture Overview

### ptr_element Structure (72 bytes)
```c
struct ptr_element {
    uint32_t type;              // Node type (0=element, 1=text, etc.)
    uint32_t frozen_version;    // COW support
    struct ptr_element* next_sibling;
    struct ptr_element* prev_sibling;
    struct ptr_element* first_child;
    struct ptr_element* last_child;  // For O(1) append
    struct ptr_element* parent;
    struct ptr_attribute* first_attr;
    uint16_t attr_count;
    uint16_t child_count;
    const char* name;
    const char* namespace_uri;
    struct taurus_document* document;
};
```

### ptr_text Structure (24 bytes)
```c
struct ptr_text {
    uint32_t type;              // PTR_NODE_TYPE_TEXT or PTR_NODE_TYPE_CDATA
    uint32_t frozen_version;
    struct ptr_node* next_sibling;
    struct ptr_node* prev_sibling;
    const char* text;           // Pool-allocated, null-terminated
};
```

### ptr_node Union
```c
struct ptr_node {
    uint32_t type;              // First field for type detection
    uint32_t frozen_version;
    union {
        struct ptr_element elem;
        struct ptr_text text;
    } u;
};
```

---

## Implementation Status

### Completed
- [x] ptr_element structure definition (72 bytes)
- [x] ptr_attribute structure definition (32 bytes)
- [x] ptr_text structure definition (24 bytes)
- [x] ptr_accessor.c/h - direct pointer accessors
- [x] ptr_parser.c - pointer-based XML parser
- [x] Strict mode support in ptr_parser
- [x] Element type initialization fix
- [x] Element name null-termination (after '/' check)
- [x] Text content pool allocation (not in-place modification)
- [x] Text/element sibling linking with type checking
- [x] attr_count increment fix
- [x] Switch from v5 parser to ptr_parser in API

### In Progress
- [ ] **CRITICAL**: Fix XPath evaluator for ptr_element
  - Type mismatches between ptr_node and TaurusNode
  - Nodeset iteration crashes (SEGFAULT/Bus error)
- [ ] **CRITICAL**: Fix DOM traversal for ptr_element
  - Memory layout assumptions in legacy code

### Pending
- [ ] Update serialize.c for ptr_element
- [ ] Update C14N for ptr_element
- [ ] Update XInclude for ptr_element
- [ ] Delete all legacy compact code
- [ ] Update test expectations where needed
- [ ] Achieve 100% test pass rate
- [ ] Run benchmarks vs pugixml

---

## Phase 1: Fix Core Type System (CRITICAL)

### Problem Analysis
The XPath and DOM code uses `TaurusNode` which has a different layout than `ptr_node`:

**TaurusNode (legacy):**
```c
typedef struct taurus_node {
    TaurusNodeTypeEnum type;    // 4 bytes
    unsigned int frozen : 1;    // bit field
    unsigned int version : 31;  // bit field
    struct taurus_node* next_sibling;
    struct taurus_node* prev_sibling;
} TaurusNode;  // 20 bytes
```

**ptr_element:**
```c
struct ptr_element {
    uint32_t type;              // 4 bytes
    uint32_t frozen_version;    // 4 bytes (not bit field)
    struct ptr_element* next_sibling;
    struct ptr_element* prev_sibling;
    // ... more fields
};  // 72 bytes
```

### Solution Options

**Option A: Update All Code to Use ptr_element Directly**
- Pro: Clean architecture
- Con: Large code change

**Option B: Make TaurusNode a typedef of ptr_node**
- Pro: Minimal code change
- Con: May hide type issues

**Option C: Create Adapter Layer**
- Pro: Isolates changes
- Con: Performance overhead

### Recommended: Option A
Update all code to use ptr_element/ptr_text/ptr_node types directly. This is the architecturally correct solution.

### Files to Update

| File | Change | Priority |
|------|--------|----------|
| `src/taurus/xpath/evaluator.c` | Use ptr_node types | CRITICAL |
| `src/taurus/xpath/evaluator_axes.c` | Use ptr_element for iteration | CRITICAL |
| `src/taurus/xpath/evaluator_path.c` | Use ptr_element for node tests | CRITICAL |
| `src/taurus/xpath/evaluator_types.c` | Update type detection | CRITICAL |
| `src/taurus/taurus_node_api.c` | Return ptr_node types | HIGH |
| `src/taurus/dom/node.c` | Use ptr_node internally | HIGH |
| `src/taurus/serialize/serialize.c` | Use ptr_element fields | MEDIUM |

---

## Phase 2: Update XPath Evaluator

### evaluator_axes.c Changes
```c
// OLD (broken):
TaurusNode* child_node = (TaurusNode*)child_elem;
if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) { ... }

// NEW (correct):
struct ptr_node* child = (struct ptr_node*)child_elem;
if (child->type == PTR_NODE_TYPE_ELEMENT) { ... }
```

### evaluator_path.c Changes
```c
// OLD:
static TaurusElement node_as_element(void* node) {
    TaurusNodeType first_field = *(TaurusNodeType*)node;
    if (first_field == TAURUS_NODE_ATTRIBUTE) return NULL;
    return (TaurusElement)node;
}

// NEW:
static struct ptr_element* node_as_element(void* node) {
    if (!node) return NULL;
    uint32_t type = *(uint32_t*)node;
    if (type == TAURUS_NODE_ATTRIBUTE) return NULL;
    if (type != PTR_NODE_TYPE_ELEMENT) return NULL;
    return (struct ptr_element*)node;
}
```

---

## Phase 3: Update DOM APIs

### taurus_node_api.c Changes
- `taurus_node_get_type()` - Read from ptr_node->type
- `taurus_node_first_child()` - Use ptr_element->first_child
- `taurus_node_next_sibling()` - Use ptr_element->next_sibling
- Handle text nodes via ptr_text structure

---

## Phase 4: Update Serialization

### serialize.c Changes
- Use `ptr_element->name` directly
- Use `ptr_element->first_attr` for attribute iteration
- Use `ptr_element->first_child` for child iteration
- Handle text nodes via `ptr_text->text`

---

## Phase 5: Cleanup

### Delete Legacy Files
- `src/taurus/dom/compact_element.h` (replaced by ptr_element.h)
- `src/taurus/dom/compact_accessor.c` (replaced by ptr_accessor.c)
- `src/taurus/memory/compact_single_alloc.c` (replaced by pool.c)
- `src/taurus/parse/parser_two_pass.c` (replaced by ptr_parser.c)

### Remove Legacy Fields
- `doc->compact_root_offset` - use `doc->ptr_root`
- `doc->compact_base` - no longer needed
- All `is_compact` conditionals - always use ptr mode

---

## Phase 6: Testing & Benchmarking

### Test Categories

| Category | Current | Target | Notes |
|----------|---------|--------|-------|
| Unit Tests | 11/56 | 56/56 | Fix type mismatches |
| XPath W3C | Crashing | 438/438 | Fix evaluator |
| DOM Operations | Crashing | All pass | Fix traversal |
| Serialization | Passing | All pass | Minor updates |
| Memory Safety | Unknown | No leaks | Valgrind/leaks |

### Benchmark Targets vs pugixml

| Operation | Target | Current |
|-----------|--------|---------|
| Parse Small | ≥1.0x | Unknown |
| Parse Medium | ≥1.0x | Unknown |
| Parse Large | ≥0.8x | Unknown |
| Traversal | ≥1.2x | Should be faster (direct pointers) |
| Attribute Access | ≥1.2x | Should be faster |
| Serialization | ≥1.0x | Unknown |

---

## Success Criteria

| Metric | Target |
|--------|--------|
| Test Pass Rate | 100% (56/56) |
| XPath W3C Conformance | 100% (438/438) |
| Parse vs pugixml | ≥1.0x (small/medium) |
| Traversal vs pugixml | ≥1.2x |
| Memory vs current | ≤50% |
| Zero SEGFAULTs | ✅ |
| Zero memory leaks | ✅ |

---

## Next Immediate Steps

1. **Fix evaluator_types.c** - Update `XPATH_NODE_TYPE` macro for ptr_node
2. **Fix evaluator_axes.c** - Use ptr_node iteration correctly
3. **Run tests** - Verify no more crashes
4. **Fix remaining failures** - Address specific test cases
5. **Clean up legacy code** - Remove old structures
6. **Benchmark** - Verify performance targets

---

## Files Modified This Session

| File | Changes |
|------|---------|
| `src/taurus/parse/ptr_parser.c` | Text pool allocation, attr_count fix, sibling linking fix |
| `src/taurus/xpath/evaluator_axes.c` | Debug logging (to be removed) |
| `src/taurus/xpath/evaluator_path.c` | Debug logging (to be removed) |
| `CONTINUATION_PLAN.md` | Status updates |
| `TODO.taurus-benchmark-iteration.md` | Status updates |

---

## Key Learnings

1. **Pool allocation for text** - Don't modify XML buffer in-place for text content
2. **Type checking before casting** - Always check `type` field before casting between ptr_element and ptr_text
3. **attr_count must be set** - Parser links attributes but must also count them
4. **Direct pointers are faster** - ptr_element should be faster than offset-based compact_element
5. **Architecture correctness first** - Tests will be fixed to match correct architecture
