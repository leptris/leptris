# Building Taurus

## Prerequisites

- **CMake** >= 3.20
- **C99 compiler** (GCC, Clang, or MSVC)
- Optional: **utf8proc** (Unicode validation), **iconv** (encoding conversion), **Doxygen** (API docs)

## Quick Start

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTING` | `ON` | Build Google Test suite |
| `TAURUS_BUILD_CLI` | `ON` | Build the `taurus` CLI tool |
| `TAURUS_BUILD_BENCHMARKS` | `OFF` | Performance comparison targets |
| `TAURUS_BUILD_MAN_PAGES` | `OFF` | Generate man pages from AsciiDoc |
| `TAURUS_ENABLE_UTF8PROC` | `ON` | UTF-8 validation via utf8proc |
| `TAURUS_ENABLE_ICONV` | `ON` | Encoding conversion via iconv |
| `TAURUS_ENABLE_ASAN` | `OFF` | Build with AddressSanitizer |
| `TAURUS_ENABLE_FUZZING` | `OFF` | Build libFuzzer harness |
| `TAURUS_BUILD_DOCS` | `OFF` | Generate Doxygen HTML docs |

## ASAN Build

```bash
cmake -B build-asan -S . -DTAURUS_ENABLE_ASAN=ON -DBUILD_TESTING=ON
cmake --build build-asan
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan
```

## Fuzzer Build (requires LLVM clang)

```bash
brew install llvm   # macOS
export CC=/opt/homebrew/opt/llvm/bin/clang
cmake -B build-fuzz -S . -DTAURUS_ENABLE_FUZZING=ON
cmake --build build-fuzz --target fuzz_parse
```

## Doxygen API Docs

```bash
brew install doxygen
cmake -B build -S . -DTAURUS_BUILD_DOCS=ON
cmake --build build --target docs
# Open docs/api-generated/html/index.html
```

## Installation

```bash
cmake --install build --prefix /usr/local
```

Downstream projects use:

```cmake
find_package(taurus CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE taurus::taurus)
```

## Using vcpkg

```bash
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
```
