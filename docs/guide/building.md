# Building Taurus

## Prerequisites

- **CMake** >= 3.20
- **C99 compiler** (GCC, Clang, or MSVC)
- C++11 compiler (only for tests and benchmarks)
- Optional: **utf8proc** (Unicode validation), **iconv** (encoding conversion), **Doxygen** (API docs)

### Platform packages

```bash
# Debian/Ubuntu
sudo apt-get install cmake ninja-build libutf8proc-dev

# macOS (Homebrew)
brew install cmake ninja utf8proc

# Windows (vcpkg)
vcpkg install utf8proc iconv
```

## Quick Start

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The default build produces the static library (`libtaurus.a`), the `taurus` CLI binary, and the full Google Test suite.

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTING` | `ON` | Build Google Test suite |
| `TAURUS_BUILD_CLI` | `ON` | Build the `taurus` CLI tool |
| `TAURUS_BUILD_STATIC` | `ON` | Build `libtaurus.a` |
| `TAURUS_BUILD_SHARED` | `OFF` | Build `libtaurus.{so,dylib,dll}` |
| `TAURUS_BUILD_BENCHMARKS` | `OFF` | Performance comparison targets |
| `TAURUS_BUILD_MAN_PAGES` | `OFF` | Generate man pages from AsciiDoc |
| `TAURUS_BUILD_DOCS` | `OFF` | Generate Doxygen HTML docs |
| `TAURUS_ENABLE_UTF8PROC` | `ON` | UTF-8 validation via utf8proc |
| `TAURUS_ENABLE_ICONV` | `ON` | Encoding conversion via iconv |
| `TAURUS_ENABLE_ASAN` | `OFF` | Build with AddressSanitizer |
| `TAURUS_ENABLE_FUZZING` | `OFF` | Build libFuzzer harness |
| `TAURUS_ENABLE_PGO` | `OFF` | Profile-guided optimization phase: `OFF`, `GENERATE`, or `USE`. See [PGO](#pgo-build) below. |

## PGO build

Profile-guided optimization lets the compiler specialise the
hot paths in the bytecode VM dispatch loop, the parser's tight
scan loops, and the XPath predicate filter loops. In our tests
on macOS arm64 + clang, PGO shaves ~8% off `bench_xpath_taurus`
total time vs the LTO-only baseline. Works on clang, GCC, and
MSVC — no GCC-isms.

The build is a three-step process:

```bash
# 1. Build with instrumentation.
cmake -B build-pgo -S . -DCMAKE_BUILD_TYPE=Release \
                     -DTAURUS_ENABLE_PGO=GENERATE \
                     -DTAURUS_BUILD_BENCHMARKS=ON
cmake --build build-pgo -j

# 2. Run a representative workload against the instrumented
#    binary. The test suite + benchmark suite is a good default;
#    if you ship taurus inside another app, use *its* test suite
#    instead so the profile matches real traffic.
ctest --test-dir build-pgo
build-pgo/benchmarks/bench_xpath_taurus > /dev/null
build-pgo/benchmarks/bench_xpath_pugixml > /dev/null

# 3a. clang only: merge the .profraw into a single .profdata.
#    Skip on GCC/MSVC — the profile is already usable.
xcrun llvm-profdata merge \
    build-pgo/pgo-data/default.profraw \
    -o build-pgo/pgo-data/default.profdata

# 3b. Rebuild with the profile applied.
cmake -B build-pgo -S . -DCMAKE_BUILD_TYPE=Release \
                     -DTAURUS_ENABLE_PGO=USE \
                     -DTAURUS_BUILD_BENCHMARKS=ON
cmake --build build-pgo -j
```

PGO defaults to OFF because the three-step workflow is heavier
than most users need. Distributions and packagers who want the
last ~10% on XPath dispatch should enable it.

## Static vs Shared

Default is static. To build a shared library instead:

```bash
cmake -B build -S . -DTAURUS_BUILD_STATIC=OFF -DTAURUS_BUILD_SHARED=ON \
                -DCMAKE_BUILD_TYPE=Release
```

The shared library carries an SOVERSION matching the major version (`libtaurus.so.0`).

## ASAN Build

```bash
cmake -B build-asan -S . -DTAURUS_ENABLE_ASAN=ON -DBUILD_TESTING=ON \
                     -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest --test-dir build-asan
```

On macOS, ASAN does not provide leak detection. Use the system `leaks` tool instead:

```bash
leaks --atExit -- ./build/test/test_dom
```

## Fuzzer Build (requires LLVM clang)

```bash
brew install llvm   # macOS
export CC=/opt/homebrew/opt/llvm/bin/clang
cmake -B build-fuzz -S . -DTAURUS_ENABLE_FUZZING=ON
cmake --build build-fuzz --target fuzz_parse
./build-fuzz/test/fuzz_parse test/fixtures/*.xml
```

## Doxygen API Docs

```bash
brew install doxygen     # or: sudo apt-get install doxygen
cmake -B build -S . -DTAURUS_BUILD_DOCS=ON
cmake --build build --target docs
# Open build/docs/api-generated/html/index.html
```

## Installation

```bash
cmake --install build --prefix /usr/local
```

Installs headers under `include/taurus/`, the static library under `lib/`, and the CLI under `bin/`. pkg-config and CMake config files are installed for downstream consumers.

### Downstream CMake

```cmake
find_package(taurus CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE taurus::taurus)
```

### Downstream pkg-config

```bash
gcc app.c $(pkg-config --cflags --libs taurus)
```

## Using vcpkg

Taurus ships a vcpkg overlay port under `ports/taurus/`. Use it directly:

```bash
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_SOURCE_PATH=$PWD
```

Or copy `ports/taurus/` into your vcpkg overlay ports directory.

## Runtime Configuration

Taurus exposes thread-default configuration that is inherited by every document parsed on that thread:

- `taurus_set_max_depth(int)` — overrides the 256-element nesting cap (DoS protection).
- `taurus_set_strict_mode(int)` — enables strict XML well-formedness checks (rejects unknown entities, missing semicolons, etc.).
- `taurus_document_set_strict(doc, int)` — overrides strict mode per-document.
- `taurus_document_set_allocators(doc, alloc, dealloc)` — routes document-scoped allocations through custom hooks.

See `src/include/taurus.h` for the complete public API.

## Running Tests

Tests are Google Test binaries under `build/test/`. Run a single suite:

```bash
./build/test/test_dom --gtest_filter=TestName.TestCase
./build/test/test_xpath --gtest_filter=Axes.*
```

Or run all tests via ctest:

```bash
ctest --test-dir build --output-on-failure
```

## Validation Script

`./scripts/validate.sh` does clean build → tests → CLI smoke → DOM → XPath → benchmarks → memory leak check → element-size check. Use this as a sanity check before tagging a release.
