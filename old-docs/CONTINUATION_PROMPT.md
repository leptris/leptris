# Taurus ptr_element Migration - Continuation Prompt

**For next Claude Code session**

---

## Context

You are continuing work on the Taurus XML parser migration from offset-based `compact_element` to pointer-based `ptr_element`.

## Current State (2026-03-04)

- **Parser:** Working (ptr_parser.c)
- **Tests:** 17/56 passing (30%)
- **Main Issues:** XPath crashes (SEGFAULT), DOM crashes (SIGTRAP)

## Completed Fixes

1. **Type Constants Aligned** - All PTR_NODE_TYPE_* values match TaurusNodeTypeEnum
2. **Text Node Linking Fixed** - first_child pointer correctly points to text nodes
3. **Union Offset Issue Fixed** - Direct casting to ptr_text* instead of using ptr_node union
4. **Namespace Collection Fixed** - Uses ptr_attribute fields instead of taurus_attribute

## Critical Pattern: Union Offset Issue

The `ptr_node` union has `type` and `frozen_version` OUTSIDE the union:
```c
struct ptr_node {
    uint32_t type;           // Offset 0
    uint32_t frozen_version; // Offset 4
    union {
        struct ptr_element elem;
        struct ptr_text text;
    } u;
};
```

**WRONG:** `node->u.text.text` - accesses wrong memory offset
**CORRECT:** `((struct ptr_text*)child)->text` - direct cast

All node types share the same first 4 fields for navigation:
- type (4 bytes)
- frozen_version (4 bytes)
- next_sibling (8 bytes)
- prev_sibling (8 bytes)

## Remaining Crashes

### XPath (SEGFAULT)
- test_xpath_functions_boolean
- test_xpath_functions_nodeset
- test_xpath_axes
- test_xpath_operators
- test_custom_functions

### DOM (SIGTRAP/Abort)
- test_dom_pugixml_write
- test_dom_operations
- test_tree_operations
- test_memory_safety
- test_attribute_conversion

## Files to Focus On

| Priority | File | Issue |
|----------|------|-------|
| CRITICAL | `src/taurus/xpath/evaluator.c` | XPath evaluation crashes |
| CRITICAL | `src/taurus/xpath/evaluator_axes.c` | Axis implementations |
| CRITICAL | `src/taurus/dom/element.c` | DOM operations |
| HIGH | `src/taurus/taurus_element_api.c` | Element API |

## Architecture Principle

**Correctness over compatibility.** Breaking API changes are accepted. No legacy code should remain.

## Success Criteria

- 56/56 tests passing
- No SEGFAULTs or Bus errors
- XPath W3C conformance 100%
- Parsing speed >= 1.0x pugixml
- Traversal speed >= 1.2x pugixml

## Benchmark Suite Requirements

Create comprehensive benchmarks in `benchmarks/suite/`:

1. **Parsing Benchmarks**
   - Small files (< 1KB)
   - Medium files (1-100KB)
   - Large files (> 100KB)
   - Deep nesting (100+ levels)
   - Wide trees (1000+ siblings)

2. **Traversal Benchmarks**
   - First child access
   - Next sibling access
   - Deep tree walking
   - Attribute access

3. **XPath Benchmarks**
   - All 13 axes
   - Common patterns
   - Complex expressions

4. **Memory Benchmarks**
   - Peak memory usage
   - Allocation count

## Performance Targets

| Operation | Target vs pugixml | Target vs libxml2 |
|-----------|------------------|-------------------|
| Parse Small | >= 1.0x | >= 1.5x |
| Parse Medium | >= 0.8x | >= 1.2x |
| Parse Large | >= 0.5x | >= 1.0x |
| Traversal | >= 1.2x | N/A |
| XPath | N/A | >= 1.0x |
| Memory | <= 50% | <= 50% |

## Commands

```bash
# Build
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure

# Run specific test
./build/test/test_dom_traverse

# Run XPath test
./build/test/test_xpath_axes

# Memory check (macOS)
leaks --atExit -- ./build/test/c/test_dom
```
