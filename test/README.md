# Leptris Test Suite

C-based tests using [Google Test](https://github.com/google/googletest),
registered with CTest via `gtest_discover_tests`.

## Layout

```
test/
├── CMakeLists.txt           — Google Test integration + test registration
├── fixtures/                — Static XML inputs
├── smoke/test_smoke.cpp     — Build-and-link sanity
├── parser/test_parser.cpp   — Malformed input, encoding, BOM, declaration
├── dom/test_dom.cpp         — Node creation, traversal, modification
├── memory/test_pool.cpp     — Pool lifecycle, oversized allocs, stats
├── xpath/test_xpath.cpp     — Axes, functions, predicates
└── serializer/test_serialize.cpp — Round-trip, escaping, realloc growth
```

Each subdirectory maps to one module under `src/leptris/` (MECE).

## Policy

- **Real model instances only.** No `Mock*`, no test doubles. If a model
  is hard to construct in a test, build a fixture factory — not a stub.
- **Assert on behavior**, not on internal call counts or flag state.
- One `TEST` per behavior; group related behaviors in `TEST_F` fixtures.
- Each test file is its own executable; failures are isolated.

## Running

```bash
cmake -B build -S . -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure

# Filter to a single test
ctest --test-dir build -R ParserDepthLimit --output-on-failure

# Run a specific executable directly with gtest filtering
./build/test/parser/test_parser --gtest_filter='ParserDepthLimit.*'
```

## Memory-leak CI

On macOS, every test executable is also run under `leaks`:

```bash
leaks --atExit -- ./build/test/smoke/test_smoke
```

On Linux, under valgrind:

```bash
valgrind --leak-check=full --error-exitcode=1 ./build/test/smoke/test_smoke
```

A test that introduces a regression in `leptris_document_free` will surface
as a nonzero exit code from these tools.
