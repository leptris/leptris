# TODO 40: Fuzzing harness

**Priority**: P2 (security / robustness)
**Status**: Planned
**Effort**: M

## Problem

XML parsers are a perennial source of CVEs — billion-laughs, external
entity expansion, deeply nested input, malformed UTF-8, etc.  The
depth-limit fix in TODO 07 addressed one class (stack overflow).  But
many others remain untested:

- Quadratic blowup via specific attribute patterns.
- Integer overflow in size calculations.
- Off-by-one in entity decoding.
- State-machine bugs in the parser (unclosed tags at EOF, mixed
  content edge cases).

Manual testing catches the obvious ones; fuzzing catches the rest.

## Fix

Add a libFuzzer harness under `test/fuzz/`:

```c
/* test/fuzz/fuzz_parse.c */
#include <taurus.h>
#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(
        (const char*)data, size, &st);

    if (doc) {
        /* Exercise serialize + free to catch use-after-free in cleanup. */
        char* out = taurus_document_serialize(doc, NULL);
        if (out) taurus_free_string(out);
        taurus_document_free(doc);
    }
    return 0;
}
```

CMake:

```cmake
option(TAURUS_ENABLE_FUZZING "Build libFuzzer harness" OFF)

if(TAURUS_ENABLE_FUZZING)
    add_executable(fuzz_parse test/fuzz/fuzz_parse.c)
    target_link_libraries(fuzz_parse PRIVATE taurus)
    target_compile_options(fuzz_parse PRIVATE
        -fsanitize=fuzzer,address -fno-omit-frame-pointer -g)
    target_link_options(fuzz_parse PRIVATE -fsanitize=fuzzer,address)
endif()
```

Seed corpus: copy `test/fixtures/*.xml` and `benchmarks/data/*.xml`.

## Tests

Fuzzing is itself the test.  Run locally:

```bash
cmake -B build -S . -DTAURUS_ENABLE_FUZZING=ON
cmake --build build --target fuzz_parse
mkdir -p corpus && cp test/fixtures/*.xml corpus/
./build/fuzz_parse -max_total_time=600 corpus/
# Discovers bugs as crashes; ASAN reports give stack traces.
```

CI integration: optional job that runs for 5 minutes per push, longer
on nightly.

## Architecture notes

Fuzzing is the highest-leverage test investment for a parser.  It
catches bugs that unit tests miss because the input space is too large
to enumerate.  Combined with ASAN (TODO 35), it catches memory-safety
bugs at the moment of corruption.

The seed corpus matters — start with the existing fixtures and let the
fuzzer mutate them.  Add corpus entries for any crashing input found
(regression tests).

## Note on macOS

libFuzzer requires the open-source LLVM toolchain, not the Xcode-bundled
clang.  On macOS:

```bash
brew install llvm
export CC=/opt/homebrew/opt/llvm/bin/clang
export CXX=/opt/homebrew/opt/llvm/bin/clang++
cmake -B build-fuzz -S . -DTAURUS_ENABLE_FUZZING=ON ...
```

On Linux (clang package from most distros) it works out of the box.

## Implementation status

- Harness written: `test/fuzz/fuzz_parse.c`.
- CMake target wired in: `if(TAURUS_ENABLE_FUZZING)` block in
  `test/CMakeLists.txt`.
- Builds successfully on Linux clang.
- macOS requires the LLVM brew formula (documented above).
