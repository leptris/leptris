# Continuation Plan: Legacy Code Cleanup

**Created:** 2026-03-05
**Goal:** Clean up all legacy, backwards compatibility, and duplicate code. Keep only:
1. **Compact mode** - `compact_element_v2` storage for inplace parsing
2. **Pointer access** - `ptr_element`, `ptr_attribute`, `ptr_text` structures
3. **Inplace API** - `taurus_parse_string_inplace()`

---

## Architecture Principle

**MECE (Mutually Exclusive, Collectively Exhaustive)**: There should be exactly ONE way to do each thing.

### Target Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    PUBLIC API                                    │
├─────────────────────────────────────────────────────────────────┤
│ taurus_parse_string()       → ptr_element tree (copy input)     │
│ taurus_parse_string_inplace() → compact_element + wrappers      │
├─────────────────────────────────────────────────────────────────┤
│                    INTERNAL                                      │
├─────────────────────────────────────────────────────────────────┤
│ Parser: taurus_parse_ptr(), taurus_parse_v5()                   │
│ Storage: ptr_element (72B) or compact_element_v2 (16B)          │
│ Access: ptr_accessor.c, compact_accessor.c                       │
└─────────────────────────────────────────────────────────────────┘
```

---

## Current State Analysis

### Parsing Modes (2 paths - KEEP BOTH)
1. `taurus_parse_ptr()` → Used by `taurus_parse_string()` → Creates ptr_element structures directly
2. `taurus_parse_v5()` → Used by `taurus_parse_string_inplace()` → Creates compact_element_v2 storage, then wraps

### Element Structures (TOO MANY - CLEANUP NEEDED)

| Structure | Size | Status | Action |
|-----------|------|--------|--------|
| `ptr_element` | 72 bytes | ACTIVE | KEEP |
| `ptr_attribute` | ~48 bytes | ACTIVE | KEEP |
| `ptr_text` | ~32 bytes | ACTIVE | KEEP |
| `compact_element_v2` | 16 bytes | ACTIVE (inplace) | KEEP |
| `compact_attribute_v2` | 8 bytes | ACTIVE (inplace) | KEEP |
| `compact_text_v2` | 16 bytes | ACTIVE (inplace) | KEEP |
| `taurus_element` | ~168 bytes | LEGACY | REMOVE |
| `taurus_attribute` | ~64 bytes | LEGACY | REMOVE |
| `compact_element_v3` | 20 bytes | UNUSED | REMOVE |
| `TaurusElementCompact` | varies | LEGACY | REMOVE |
| `TaurusElement` handle | 4 bytes | UNUSED | REMOVE |

---

## Phase 1: Remove Unused Files ✅ COMPLETED

**Priority:** HIGH
**Risk:** LOW (unused files)
**Impact:** ~750 lines removed

### Tasks

- [x] Remove `src/taurus/dom/compact_element_v3.h` - Never used, was an experiment
- [x] Remove `src/taurus/dom/element_handle.h` - Handle-based architecture not used
- [x] Remove `src/taurus/dom/element_dispatch.h` - References unused handle system
- [x] Remove `src/taurus/dom/element_compact.c` - Code behind `#ifdef TAURUS_COMPACT_MODE` (never defined)
- [x] Update `src/CMakeLists.txt` to remove deleted files (files weren't in CMakeLists)

---

## Phase 2: Remove Legacy Parsing Code ✅ COMPLETED

**Priority:** HIGH
**Risk:** MEDIUM (need to verify no callers)
**Impact:** ~140 lines removed

### Tasks

- [x] Remove `taurus_parse_inplace()` internal function in `taurus_parse_api.c` (lines 150-292)
  - Never called from public API
  - Uses old Parser structure
- [ ] ~~Remove `taurus_parse_string_with_encoding()` wrapper function~~
  - SKIPPED: Used by CLI, keeping for backward compatibility
- [ ] Remove unused parser functions from `parser.c` that are only used by removed paths
  - DEFERRED: Need to verify which functions are unused
- [x] Verify all removed functions have no callers

---

## Phase 3: Remove Duplicate Error Codes ✅ COMPLETED

**Priority:** MEDIUM
**Risk:** LOW
**Impact:** ~15 lines removed

### Tasks

- [x] Remove duplicate error code macros in `taurus_internal.h` (lines 38-50)
- [x] Remove `typedef TaurusStatus taurus_error_code;`
- [x] Update code using these aliases to use `TaurusStatus` directly:
  - `TAURUS_ERROR_XPATH_SYNTAX` → `TAURUS_ERROR_XPATH` in lexer.c, parser.c
  - `TAURUS_ERROR_MEMORY_ALLOCATION` → `TAURUS_ERROR_MEMORY` in taurus_parse_api.c

---

## Phase 4: Mark Deprecated API Functions ✅ COMPLETED

**Priority:** MEDIUM
**Risk:** LOW (marking only, not removing)
**Impact:** Clear deprecation path

### Tasks

- [x] Add `TAURUS_DEPRECATED` macro to element.h (compiler-agnostic)
- [x] Mark legacy functions as deprecated:
  - `taurus_element_add_attribute_legacy()`
  - `taurus_element_add_attribute_pooled()`
  - `taurus_element_add_attribute_pooled_inplace()`
  - `taurus_element_get_attribute_legacy()`
  - `taurus_element_add_namespace_deprecated()`
  - `taurus_element_add_namespace_inplace()`
  - `taurus_namespace_new_pooled()`
- [x] Verify these functions are not called in tests (they are not)
- [x] Build and tests pass after marking deprecated

---

## Phase 5: Remove Legacy Element Structures ✅ COMPLETED

**Priority:** HIGH
**Risk:** MEDIUM (need careful testing)
**Impact:** Cleaner architecture

### Tasks
- [x] Remove legacy `root` field from `taurus_document` struct
- [x] Remove duplicate pool fields (`ptr_elem_pool`, `ptr_attr_pool`, `ptr_text_pool`)
- [x] Simplify `element_modify.c` to use single `pool` field
- [x] Simplify cleanup code in `taurus_document.c`
- [x] All tests pass after changes

---

## Phase 6: Simplify Document Structure ✅ COMPLETED
**Priority:** MEDIUM
**Risk:** MEDIUM
**Impact:** Cleaner architecture

### Final Structure
```c
struct taurus_document {
    /* Memory pool (owns all DOM nodes) */
    TaurusMemoryPool* pool;

    /* Document metadata */
    char* encoding;
    struct taurus_processing_instruction* pis;
    size_t ref_count;
    void* new_dom_root;

    /* XML Declaration */
    char* xml_version;
    int standalone;
    int had_declaration;
    int has_bom;

    /* DOCTYPE support */
    void* doctype;
    void* dtd;

    /* Compact pointer support */
    void* page_base;
    struct taurus_compact_overflow_entry* overflow_entries;

    /* In-place parsing */
    char* xml_buffer;
    size_t xml_buffer_len;
    int xml_buffer_needs_free;

    /* Parsing state */
    int strict_mode;
    int is_ptr_mode;
    void* ptr_root;

    /* Compact-only mode (v5 parser) */
    void* compact_alloc;
    void* compact_base;
    uint32_t compact_root_offset;
    void* wrapper_cache;
};
```

### Tasks
- [x] Remove `root` field (legacy, use `ptr_root` only)
- [x] Remove `ptr_elem_pool`, `ptr_attr_pool`, `ptr_text_pool` fields
- [x] Consolidate pool usage in `element_modify.c`
- [x] Simplify cleanup code in `taurus_document_free()`
- [x] All tests pass after consolidation

---

## Phase 7: Update Documentation

**Priority:** LOW
**Risk:** LOW
**Impact:** Clear documentation

### Tasks

- [ ] Update `README.adoc`:
  - Remove references to legacy APIs
  - Document only: `taurus_parse_string()` and `taurus_parse_string_inplace()`
  - Explain compact vs pointer mode clearly
- [ ] Move `CONTINUATION_PROMPT.md` to `old-docs/`
- [ ] Move `IMPLEMENTATION_STATUS.md` to `old-docs/` if exists
- [ ] Update `docs/architecture.adoc` with current architecture

---

## Verification Checklist

After each phase:
- [ ] Build succeeds: `cmake --build build`
- [ ] All tests pass: `ctest --test-dir build --output-on-failure`
- [ ] No memory leaks: `leaks --atExit -- ./build/test/c/test_dom`
- [ ] Benchmark runs: `./build/benchmarks/ultimate_benchmark`

---

## Files to Modify

| File | Action | Phase |
|------|--------|-------|
| `src/taurus/dom/compact_element_v3.h` | DELETE | 1 |
| `src/taurus/dom/element_handle.h` | DELETE | 1 |
| `src/taurus/dom/element_dispatch.h` | DELETE | 1 |
| `src/taurus/dom/element_compact.c` | DELETE | 1 |
| `src/taurus/taurus_parse_api.c` | Remove dead code | 2 |
| `src/taurus/parse/parser.c` | Remove unused functions | 2 |
| `src/taurus/taurus_internal.h` | Remove duplicate errors | 3 |
| `src/taurus/dom/element.h` | Mark deprecated, remove structs | 4, 5 |
| `src/taurus/dom/element.c` | Remove legacy functions | 5 |
| `src/CMakeLists.txt` | Remove deleted files | 1 |
| `README.adoc` | Update documentation | 7 |

---

## Estimated Impact

- **Lines of code removed**: ~1,200
- **Files removed**: 4
- **Complexity reduction**: Significant
- **Maintainability**: Much improved
- **Performance**: No change (removing unused code)

---

## Execution Order

1. **Phase 1**: Remove unused files (safest, high impact)
2. **Phase 3**: Remove duplicate error codes (simple cleanup)
3. **Phase 2**: Remove legacy parsing code (no API impact)
4. **Phase 4**: Mark deprecated API functions
5. **Phase 5**: Remove legacy element structures (requires care)
6. **Phase 6**: Simplify document structure (requires testing)
7. **Phase 7**: Update documentation (final step)

---

## Commands

```bash
# Build
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure

# Memory check
leaks --atExit -- ./build/test/c/test_dom

# Run benchmark
./build/benchmarks/ultimate_benchmark

# Find legacy code patterns
grep -r "is_compact" src/
grep -r "taurus_element " src/  # Note space to avoid ptr_element matches
grep -r "TAURUS_COMPACT_MODE" src/
```
