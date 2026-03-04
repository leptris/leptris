# Taurus ptr_element Migration - Continuation Plan

**Deadline: ASAP**
**Current Status: 11/56 tests passing (19.6%)**
**Blocker: XPath/DOM crashes due to type mismatches**

---

## Session Goal

Fix the XPath evaluator to work correctly with ptr_element/ptr_node types and achieve 100% test pass rate.

---

## Root Cause Analysis

### Type System Mismatch

The XPath evaluator uses `TaurusNode` (legacy 20-byte struct) but the parser now creates `ptr_element` (72 bytes) and `ptr_text` (24 bytes).

**Legacy TaurusNode layout:**
```
offset 0:  TaurusNodeTypeEnum type (4 bytes)
offset 4:  unsigned frozen:1, version:31 (4 bytes as bit fields)
offset 8:  next_sibling pointer
offset 16: prev_sibling pointer
Total: 20 bytes
```

**ptr_element layout:**
```
offset 0:  uint32_t type (4 bytes)
offset 4:  uint32_t frozen_version (4 bytes)
offset 8:  next_sibling pointer
offset 16: prev_sibling pointer
offset 24: first_child pointer
... (72 bytes total)
```

**ptr_text layout:**
```
offset 0:  uint32_t type (4 bytes)
offset 4:  uint32_t frozen_version (4 bytes)
offset 8:  next_sibling pointer
offset 16: prev_sibling pointer
offset 20: text pointer
Total: 24 bytes
```

The first 20 bytes are compatible, so reading type/next_sibling/prev_sibling works. But casting `ptr_text*` to `TaurusNode*` loses the text content pointer.

---

## Implementation Tasks

### Task 1: Fix Type Detection Macros

**File:** `src/taurus/taurus_internal.h`

**Change:**
```c
// OLD:
#define XPATH_NODE_TYPE(node) (*(TaurusNodeType*)(node))

// NEW:
#define XPATH_NODE_TYPE(node) (*(uint32_t*)(node))
```

**Verification:**
- Build and run `test_xpath_axes`
- No more Bus errors in type checking

---

### Task 2: Fix axis_child Function

**File:** `src/taurus/xpath/evaluator_axes.c`

**Current Issue:** Casting to `TaurusNode` then accessing `type` field.

**Fix:**
```c
// OLD:
TaurusElement child_elem = taurus_element_get_first_child(node);
while (child_elem) {
    TaurusNode* child_node = (TaurusNode*)child_elem;
    if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
        // ...
    }
    child_elem = taurus_element_get_next_sibling(child_elem);
}

// NEW (same logic, but with correct type):
TaurusElement child_elem = taurus_element_get_first_child(node);
while (child_elem) {
    // taurus_element_get_first_child already skips text nodes
    // So child_elem is always an element
    if (matches_node_test(ctx, child_elem, test)) {
        xpath_nodeset_add(result, child_elem);
    }
    child_elem = taurus_element_get_next_sibling(child_elem);
}
```

**Note:** `taurus_element_get_first_child` and `taurus_element_get_next_sibling` already skip text nodes (see `ptr_accessor.c`). The current code's type check is redundant.

---

### Task 3: Fix Node Test Matching

**File:** `src/taurus/xpath/evaluator_path.c`

**Function:** `matches_node_test`

**Current Issue:** Uses `taurus_element_get_name()` which checks legacy type field.

**Fix in element.c:**
```c
// In taurus_element_get_name:
const char* taurus_element_get_name(TaurusElement elem) {
    if (!elem) return NULL;
    // OLD: if (elem->type != TAURUS_NODE_TYPE_ELEMENT) return NULL;
    // NEW: if (elem->type != PTR_NODE_TYPE_ELEMENT) return NULL;
    return elem->name;
}
```

---

### Task 4: Fix Descendant Axis Recursion

**File:** `src/taurus/xpath/evaluator_axes.c`

**Function:** `collect_descendants_impl`

**Current Issue:** Type field comparison uses wrong constant.

**Fix:**
```c
// OLD:
if (child_node->type == TAURUS_NODE_TYPE_ELEMENT)

// NEW:
if (child_node->type == PTR_NODE_TYPE_ELEMENT)
```

Or simpler: since `taurus_element_get_first_child` already returns only elements:
```c
// Remove redundant type check entirely
TaurusElement child_elem = taurus_element_get_first_child(node);
while (child_elem) {
    if (matches_node_test(ctx, child_elem, test)) {
        xpath_nodeset_add(result, child_elem);
    }
    collect_descendants_impl(ctx, child_elem, result, node_test, depth + 1);
    child_elem = taurus_element_get_next_sibling(child_elem);
}
```

---

### Task 5: Update Type Constants

**File:** `src/taurus/dom/ptr_element.h`

**Verify constants match legacy values:**
```c
#define PTR_NODE_TYPE_ELEMENT   0  // Must match TAURUS_NODE_ELEMENT
#define PTR_NODE_TYPE_TEXT      1  // Must match TAURUS_NODE_TEXT
#define PTR_NODE_TYPE_COMMENT   2  // etc.
#define PTR_NODE_TYPE_CDATA     3
#define PTR_NODE_TYPE_PI        4
#define PTR_NODE_TYPE_DOCTYPE   5
```

**Compare with taurus_internal.h:**
```c
typedef enum {
    TAURUS_NODE_ELEMENT = 0,
    TAURUS_NODE_ATTRIBUTE = 1,
    TAURUS_NODE_TEXT = 2,  // MISMATCH! PTR has TEXT=1
    // ...
} TaurusNodeType;
```

**CRITICAL:** The type values don't match! This is the root cause.

**Fix:** Update ptr_element.h to match:
```c
#define PTR_NODE_TYPE_ELEMENT   0  // TAURUS_NODE_ELEMENT
#define PTR_NODE_TYPE_TEXT      2  // TAURUS_NODE_TEXT (was 1, now 2)
#define PTR_NODE_TYPE_COMMENT   3  // etc.
#define PTR_NODE_TYPE_CDATA     3  // Same as text for our purposes
#define PTR_NODE_TYPE_PI        4
#define PTR_NODE_TYPE_DOCTYPE   5
```

---

### Task 6: Update Parser for Correct Type Values

**File:** `src/taurus/parse/ptr_parser.c`

**Search for:**
```c
text->type = PTR_NODE_TYPE_TEXT;
elem->type = PTR_NODE_TYPE_ELEMENT;
```

**Update if PTR_NODE_TYPE_TEXT changed from 1 to 2.**

---

### Task 7: Remove Debug Logging

**Files:**
- `src/taurus/xpath/evaluator_axes.c`
- `src/taurus/xpath/evaluator_path.c`

**Set:**
```c
#define XPATH_DEBUG 0
```

---

### Task 8: Rebuild and Test

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

**Target:** 56/56 tests passing

---

## Verification Checklist

- [ ] Type constants match between ptr_element.h and taurus_internal.h
- [ ] XPATH_NODE_TYPE macro uses uint32_t
- [ ] Axis functions don't use redundant type checks
- [ ] No debug output in release build
- [ ] All 56 tests pass
- [ ] No SEGFAULTs or Bus errors

---

## Success Criteria

| Metric | Target |
|--------|--------|
| Test Pass Rate | 100% (56/56) |
| No Crashes | All tests complete without SEGFAULT/Bus error |
| Parser Works | Text content correctly stored |
| Attributes Work | attr_count matches actual count |

---

## Files to Modify

| File | Change |
|------|--------|
| `src/taurus/dom/ptr_element.h` | Fix PTR_NODE_TYPE_* constants |
| `src/taurus/taurus_internal.h` | Fix XPATH_NODE_TYPE macro |
| `src/taurus/xpath/evaluator_axes.c` | Remove redundant type checks |
| `src/taurus/xpath/evaluator_path.c` | Set XPATH_DEBUG to 0 |
| `src/taurus/parse/ptr_parser.c` | Update type constants if changed |

---

## Next Session Prompt

Continue fixing XPath/DOM integration issues for ptr_element architecture:

1. Verify type constants match between ptr_element.h and taurus_internal.h
2. Fix XPATH_NODE_TYPE macro in taurus_internal.h
3. Remove redundant type checks in evaluator_axes.c
4. Rebuild and run tests
5. Address any remaining failures

Current blockers: Type mismatch between PTR_NODE_TYPE_* and TAURUS_NODE_* constants causing XPath evaluation crashes.
