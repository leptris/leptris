# TODO 32: CLI command specs

**Priority**: P2 (coverage)
**Status**: Planned
**Effort**: M

## Problem

The CLI (`src/taurus/cli/`) has 4 commands — `parse`, `xpath`,
`format`, `version` — but zero automated specs.  The original
validation pass found regressions by hand-testing; there's no
regression net for future changes.

## Root cause

The CLI was developed as a thin wrapper on libtaurus; nobody wrote
specs because "it's just argument parsing and calling the library."
But argument parsing is itself bug-prone (option ordering, stdin
handling, error messages), and the library's behavior under CLI
invocation can differ from direct API use (e.g., file I/O layer).

## Fix

### Test strategy: subprocess invocation

The CLI is a separate binary; specs invoke it via `fork`/`exec` and
assert on exit code, stdout, stderr.  This catches end-to-end
behavior including file I/O.

```cpp
// test/cli/test_cli.cpp
#include <gtest/gtest.h>
#include < subprocess helpers >

TEST(CliVersion, PrintsVersion) {
    auto [exit, out, err] = run({"build/cli/taurus", "version"});
    EXPECT_EQ(exit, 0);
    EXPECT_NE(out.find("taurus"), std::string::npos);
}

TEST(CliParse, ReadsFromStdin) {
    auto [exit, out, err] = run({"build/cli/taurus", "parse", "-"},
                                 "<root>hi</root>");
    EXPECT_EQ(exit, 0);
    EXPECT_NE(out.find("<root>hi</root>"), std::string::npos);
}

TEST(CliParse, ReadsFile) { /* ... */ }

TEST(CliParse, RejectsMalformedInput) {
    auto [exit, out, err] = run({"build/cli/taurus", "parse"},
                                 "<a><b></a></b>");
    EXPECT_NE(exit, 0);
    EXPECT_NE(err.find("error"), std::string::npos);
}

TEST(CliXpath, EvaluatesQuery) {
    // ... write fixtures/<x>.xml, invoke xpath, assert on result
}

TEST(CliFormat, AppliesIndent) {
    // ... verify pretty-print output
}

TEST(CliFormat, JsonOutputIsParseable) {
    // format --format json; verify output is valid JSON via a tiny parser.
}
```

### Test fixture directory

`test/cli/fixtures/` holds small XML files used by multiple specs.

### CMake registration

Add to `test/CMakeLists.txt`:

```cmake
taurus_add_test(test_cli  cli/test_cli.cpp)
```

The subprocess helper handles `fork`/`exec`/pipe capture.  About 50
lines of C++; not worth a dependency.

## Tests

The specs above are the deliverable.  Each CLI command gets at least
3 specs: happy path, malformed input, option handling.

## Architecture notes

CLI specs guard against two real risks:

1. **Argument-parsing regressions** — a future refactor of
   `cli/options.c` could silently break option precedence.
2. **Output-format drift** — `format --format json` changing its
   schema would break downstream consumers.

The subprocess approach is slower than in-process but matches how
users actually invoke the CLI.  In-process would require linking
the CLI code into the test binary, which couples the two.

## Verification

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R Cli
# All CLI specs pass.
```
