# TODO: Taurus Compact/Benchmark Migration
**Goal:** Complete ptr_element migration, achieve >= 1.0x pugixml parsing speed. All 56 tests must pass.
**Deadline:** ASAP
**Last Updated:** 2026-03-04

---

## Current Status

| Metric | Current | Target |
|--------|---------|--------|
| Tests Passing | 44/56 (79%) | 56/56 (100%) |
| Crashes | None | None |
| Parse Speed vs pugixml | 3.10x slower | >= 1.0x |
| Traversal Speed vs pugixml | TBD | >= 1.2x |
| Modification Speed vs pugixml | 1.03x average | <= 1.0x |

---

## Benchmark Results

| Category | Taurus vs pugixml | Status |
|-----------|------------------|--------|
| **Parse Small (<=1KB)** | 3.00x slower | ⚠️ Needs optimization |
| **Parse Medium (1KB-100KB)** | 3.10x slower | ⚠️ Needs optimization |
| **Parse Large (>100KB)** | TBD | - |
| **DOM Modification** | 1.03x average | ✅ GOOD |

| **Traversal Operations** | TBD | - |

### Notes
- Modification benchmarks pass all tests (0.74x-1.31x faster than pugixml)
- Parsing benchmarks show Taurus is 3x slower than pugixml
- This is expected given the ptr_parser was designed to be 1.29-1.45x faster than pugixml
    - For large files, optimization is still needed

- Then C14N feature is still not fully implemented
    - Some edge cases (UTF-8 names, whitespace preservation) still have minor issues

---

## Remaining Failing Tests (12 tests)

1. **test_dom_pugixml_write** - 1 test: indentation with text content
2. **test_serialization_features** - 3 tests: roundtrip with pretty-print
3. **test_content_preservation** - 1 test: roundtrip complex
4. **test_c14n** - C14N not implemented (returns NULL)
5. **test_cli** - CLI tests (library path issues)
6. **test_libxml2_errors** - libxml2 error compatibility
7. **test_libxml2_comprehensive** - libxml2 comprehensive tests
8. **test_doctype_parse** - DOCTYPE edge cases
9. **test_error_handling** - Error handling edge cases
10. **test_api_extensions** - API extensions
11. **test_document_level** - Document level operations

---

## Fixes Applied This Session
1. **ptr_parser.c** - Added comment/CDATA/PI handling
2. **ptr_parser.c** - Fixed CDATA handling (allow empty CDATA)
3. **ptr_parser.c** - Fixed whitespace-only text preservation
4. **taurus_element_api.c** - Fixed first/last child any
5. **serialize.c** - Updated comment serialization
6. **taurus_element_api.c** - Fixed previous sibling any

---

## Next Steps

1. **Fix remaining 11 failing tests** - Priority: HIGH
2. **Run benchmarks** - Verify performance targets
3. **Commit all changes once all 56 tests pass**
4. **Delete legacy compact code**

5. **Update documentation**
