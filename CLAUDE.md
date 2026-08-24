# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**libleptris** — pure C99 XML 1.0 parser, XPath 1.0 engine (W3C-conformant), and SAX parser. Ships as a static/shared library plus a `leptris` CLI. Zero required runtime deps; `utf8proc` (Unicode) and `iconv` (encoding conversion) are optional.

## Build & Test

CMake-based (CMake ≥ 3.20). The default build is a static lib + CLI + tests:

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

CI uses vcpkg for `utf8proc`, `iconv`, and `gtest` — pass `-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake` to use the same set.

### CMake options worth knowing
- `BUILD_TESTING` (ON) — builds the `test/` subdirectory with Google Test.
- `LEPTRIS_BUILD_CLI` (ON) — builds the `leptris` executable.
- `LEPTRIS_BUILD_BENCHMARKS` (OFF) — enables libxml2/pugixml comparison benchmarks under `benchmarks/`.
- `LEPTRIS_BUILD_MAN_PAGES` (OFF) — generates man pages from `.adoc` via `cmake/modules/AdocMan.cmake`.
- `LEPTRIS_ENABLE_UTF8PROC` / `LEPTRIS_ENABLE_ICONV` (ON/ON) — toggle Unicode/encoding features. Disabling compiles those `.c` files out via `LEPTRIS_HAS_*` macros.
- `LEPTRIS_BUILD_STATIC` (ON) / `LEPTRIS_BUILD_SHARED` (OFF).

### Useful targets
- `leptris-cli` (output name `leptris`) — the CLI binary under `build/cli/`.
- `leptris_static` / `leptris_shared` / alias `leptris::leptris`.
- `benchmarks/dom_benchmark`, `benchmarks/bench_dom_pugixml` — when benchmarks are enabled.

### Run a single test
Tests are Google Test binaries under `build/test/`. Use `--gtest_filter`:
```bash
./build/test/c/test_dom --gtest_filter=TestName.TestCase
./build/test/xpath/test_xpath --gtest_filter=Axes.*
```

### Full validation script
`./scripts/validate.sh` does clean build → tests → CLI → DOM → XPath → benchmarks → memory leak check → element-size check (target ~96 bytes). Use this as a sanity check before claiming work is complete.

### Memory leak checks
- macOS: `leaks --atExit -- ./build/test/c/test_dom`
- Linux: `valgrind --leak-check=full --error-exitcode=1 ./build/test/c/test_dom`

## Architecture

Three strict layers (top depends only on the layer below — never the reverse):

```
CLI (cli/)            →  argument parsing, output formatting, error reporting
Public API (include/) →  leptris_parse_*, leptris_xpath_eval, leptris_document_*, ...
Core (src/leptris/)    →  DOM, parser, XPath engine, SAX, DTD, encoding, memory
```

**The CLI never touches XML structures directly — it always goes through the public API in `src/include/leptris/`.** See `cli/CLI_ARCHITECTURE.md` for the design contract (MECE options, Open/Closed command registration, no `#ifdef` code guards — solve platform differences architecturally).

### Core subsystems (`src/leptris/`)
- `dom/` — node tree: `node`, `element`, `text`, `comment`, `cdata`, `pi`, `doctype`. **Compact architecture**: elements are ~96 bytes via compressed pointers; all node types begin with `LeptrisNode` so they're safely castable.
- `parse/` — `parser_new.c` is the active parser; `compact_parser.c` is the compact-mode path. (Legacy `leptris_parse.c` / `parse_*.c` files at the subsystem root are older implementations — confirm which is wired in via `src/CMakeLists.txt` before editing.)
- `xpath/` — split into `lexer`, `parser`, `evaluator` (with `_axes`, `_operators`, `_path`, `_types` companions), `functions`, and `xpath_variables`. The evaluator is the largest and most subtle piece; XPath 1.0 conformance is 438/438 against the W3C suite — don't regress it.
- `sax/parser.c` — event-driven, no DOM construction.
- `dtd/` — `parser`, `model`, `resolver`, `content_check`, `validator`. Phases 1–7 plus `#FIXED` are shipped: EMPTY/ANY/Mixed/Element content models, `#REQUIRED`/`#IMPLIED`/`#FIXED` attribute checking, attribute type validation. Phase 8 (ENTITY/ENTITIES attribute resolution, parameter entities, choice-model backtracking) is TODO 91.
- `encoding/` — UTF-16 always compiled; `encoding.c` (iconv) only when `LEPTRIS_ENABLE_ICONV` is on.
- `unicode/unicode.c` — only compiled when `LEPTRIS_ENABLE_UTF8PROC` is on.
- `memory/` — `pool.c` (O(1) arena allocator used pervasively) and `compact_allocator.c`.
- `xinclude/` — built and shipping. `parse="text"` with `xi:fallback` works end-to-end. `parse="xml"` and xpointer are TODO 92.

### Public API surface (`src/include/leptris/`)
The public contract. Don't break ABI without a major bump. Headers split by subsystem: `dom/{document,element,serialize}.h`, `xpath/xpath.h`, `sax/sax.h`, plus `types.h`, `error.h`, `dtd.h` at the top level. All handles are opaque typedefs (`LeptrisDocument`, `LeptrisElement`, `LeptrisNodeRef`, …); callers never see struct definitions.

### Memory model
- Pool allocator is the dominant allocation path — node creation should go through `leptris_node_create_pooled`.
- Strings are stored as `StringView` (`common/string_view.{h,c}`) for zero-copy slicing during parse.
- No reference counting; ownership is document-scoped — free via `leptris_document_free`.

## Conventions

- **C99, `-Wall -Wextra -Wno-unused-parameter`, warning-clean.** No GNU extensions (`C_EXTENSIONS OFF`). New warnings are bugs — fix them at the source, don't suppress.
- Public types live in `src/include/leptris/types.h` (single canonical source); `leptris.h` includes it and adds the function declarations. See TODO 99.
- Internal headers re-include `leptris/types.h` via `leptris_internal.h` → `memory/pool.h`; do not redeclare public typedefs locally.
- Benchmarks are C++11.
- Memory ownership in the public API is documented per-function in the headers (look for "Memory:" comments) — preserve these contracts. Strings returned as `const char*` from a `LeptrisDocument`-taking accessor are document-owned and live until `leptris_document_free`.
- Error reporting goes through `LeptrisStatus` codes (`src/leptris/error.c`); the CLI wraps these via `cli/error.{h,c}`. Public-API entry points must write only public `LeptrisStatus` constants to their `LeptrisStatus*` out-params — the internal `leptris_error_code` enum is for `error.c`'s thread-local channel only (TODO 98).
- Output formats in the CLI are MECE: XML / JSON / text — never mix. Adding a new command = new file in `cli/commands/` + registration in `main.c`. No changes to core infrastructure.
- Man pages live under `cli/man/*.1.adoc` and `src/man/libleptris.5.adoc` and are only generated when `LEPTRIS_BUILD_MAN_PAGES=ON`.

## Known repo state

- `git log` is intentionally shallow on `main`; rely on `git log <path>` rather than expecting deep project history.
- `test/` is a full Google Test tree (`dom`, `xpath`, `sax`, `dtd`, `xinclude`, `c14n`, `cli`, `abi`, `memory`, `parser`, `fuzz`). Add specs under the relevant subdirectory; CMake picks them up via `test/CMakeLists.txt`.

## Where to look for context

- `README.adoc` — exhaustive feature/API reference with examples (~2700 lines). The authoritative user-facing doc.
- `cli/CLI_ARCHITECTURE.md` — CLI design contract (MECE, layers, extension points).
- `cli/ARCHITECTURE_REVIEW.md` — review notes on the CLI design.
- `docs/guide/building.md` — platform-specific build instructions.
- `VALIDATION.md` — exact commands to run individual test suites and benchmarks, plus expected results.
- `benchmarks/README.{md,adoc}` — benchmark methodology and reference comparisons.

## Releasing — ALWAYS use the automated workflow

**NEVER create git tags or GitHub releases manually.** No `git tag`, no
`git push --tags`, no `gh release create`. These operations must go
through the automated release workflow.

### How to bump version and release

1. Trigger the release workflow from the CLI:

```bash
# For a patch release (0.4.2 → 0.4.3):
gh workflow run release.yml -f next_version=patch

# For a minor release (0.4.2 → 0.5.0):
gh workflow run release.yml -f next_version=minor

# For a specific version:
gh workflow run release.yml -f next_version=0.5.0
```

2. The workflow creates a `release/vX.Y.Z` branch with the version bump
   (CMakeLists.txt, vcpkg.json, CHANGELOG.md) and opens a PR labeled
   `release` + `automated`.

3. Review the PR. Edit the CHANGELOG.md entry to add real release notes.
   Merge the PR.

4. When the PR is merged, the workflow automatically:
   - Creates the git tag `vX.Y.Z`
   - Publishes the GitHub Release with notes extracted from CHANGELOG.md

### What the bump-version.sh script does

`.github/scripts/bump-version.sh` reads the current version from git tags,
calculates the next version, and updates three files:
- `CMakeLists.txt` — `project(leptris VERSION ...)`
- `vcpkg.json` — `"version"` field
- `CHANGELOG.md` — new entry with template

### Files that must stay in sync on version

- `CMakeLists.txt` (project VERSION)
- `vcpkg.json` (version field)
- `CHANGELOG.md` (version header)

The bump-version.sh script handles all three in one shot.
