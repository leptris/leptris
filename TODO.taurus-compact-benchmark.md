# TODO: Taurus Compact/Benchmark Migration
**Goal:** Complete ptr_element migration, achieve 1.0-1.2x pugixml speed, comprehensive benchmarks
**Deadline:** ASAP
**Last Updated:** 2026-03-04

---

## Current Status

| Metric | Current | Target |
|--------|---------|--------|
| Tests Passing | 36/56 (64%) | 56/56 (100%) |
| Crashes | None (all fixed!) | None |
| Parse Speed vs pugixml | Unknown | >= 1.0x |
| Traversal Speed vs pugixml | Unknown | >= 1.2x |

---

## Recent Progress (2026-03-04)

### Completed Fixes:
1. **Entity handling** - Added entity expansion for text content and attribute values
2. **Attribute separator check** - Added XML spec compliant whitespace requirement between attributes
3. **id() function** - Fixed to use ptr_root instead of new_dom_root
4. **name() function** - Fixed duplicate prefix issue
5. **taurus_element_name()** - Now returns local name only (pugixml compatible)
6. **taurus_element_get_name()** - Now returns local name only

### All XPath Functions Nodeset Tests Pass (55/55):
- id(), name(), local-name(), namespace-uri(), count(), etc.

### All DOM Operations Tests Pass (36/36):
- SetAttribute, RemoveAttribute, AppendChild, PrependChild, InsertAfter, etc.

### All Parse Tests Pass (64/64):
- Entity expansion, attribute parsing, etc.

---

## Remaining Failing Tests (20 tests)

1. **test_libxml2_errors** - Unit test
2. **test_cli** - CLI integration test
3. **test_dom_comprehensive** - Comprehensive DOM validation
4. **test_dom_pugixml*** - DOM pugixml compatibility tests
5. **test_libxml2_comprehensive** - libxml2 comprehensive test
6. **test_document** - Document operations test
7. **test_parse_pugixml_compat** - Parse compatibility
8. **test_parse_edge_cases** - Edge case parsing
9. **test_doctype_parse** - DOCTYPE parsing
10. **test_navigation_any** - Navigation tests
11. **test_text_handling** - Text handling tests
12. **test_serialization_features** - Serialization tests
13. **test_write** - Write tests
14. **test_c14n** - Canonical XML
15. **test_xinclude** - XInclude
16. **test_error_handling** - Error handling
17. **test_edge_cases_advanced** - Advanced edge cases
18. **test_content_preservation** - Content preservation
19. **test_api_extensions** - API extensions
20. **test_document_level** - Document level operations

---

## Next Steps

1. Run individual failing tests to identify root causes
2. Fix parsing edge cases
3. Fix DOM pugixml compatibility issues
4. Fix serialization issues
5. Run benchmarks

---

## Commands

```bash
# Build
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure

# Run specific test
./build/test/test_dom_traverse

# Run benchmarks
./build/benchmarks/suite/bench_parsing

# Memory check (macOS)
leaks --atExit -- ./build/test/c/test_dom
```

---

## Success Criteria
- [ ] All 56 tests pass
- [x] No SEGFAULT/Bus error/SIGTRAP crashes
- [ ] Parse speed >= 1.0x pugixml
- [ ] Traversal speed >= 1.2x pugixml
- [ ] All legacy compact code removed
