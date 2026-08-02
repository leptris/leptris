# Taurus Validation Fix Plan

Findings from the validation passes on 2026-08-02. Each `NN-*.md` file is a
self-contained, separately mergeable unit of work.

## Final summary

| Area | Before | After |
|------|--------|-------|
| Build | Broken (missing template, missing test dir, link fail) | Clean compile, all features + ASAN + fuzzing + CI |
| Tests | 0 (no `test/` dir) | **70/70 passing** across 10 modules |
| **Compile warnings** | 50+ | **0** |
| basic.xml leaks | 43 leaks / 944 B | **0 leaks / 0 bytes** |
| bigattr.xml leaks | 2 leaks / 16 912 B | **0 leaks / 0 bytes** |
| small.xml leaks | 39 leaks / 896 B | **0 leaks / 0 bytes** |
| full.xml leaks | (would crash) | **0 leaks / 0 bytes** |
| 50k-deep nesting | Segfault (exit 139) | Rejected with parse error (exit 1) |
| attrs.xml perf | 3.4× slower than libxml2 | **0.75× — taurus 1.3× faster** |
| Active source files | 93 (incl. 12 dead) | 83 (clean) + 13 archived |
| Dispatch | 40-line switch | Vtable registry; new node types are purely additive |
| Memory safety | Manual leak hunting | ASAN + libFuzzer + CI workflows built-in |
| Strict mode | Process-global | Per-document, inheritable from thread-default |

## Status legend

- **DONE** — implemented and verified.
- **DESIGN** — design complete; execution deferred (documented in file).

## Implementation status (TODOs 01-53)

### Build (TODO 01-04) — DONE
- `cmake/taurus-config.cmake.in` added.
- `test/` recreated with Google Test.
- utf8proc + iconv link deps propagate via PUBLIC on OBJECT lib.
- Finder chain: native `utf8proc` → vcpkg `unofficial-utf8proc` → module-mode.

### Correctness — leaks (TODO 05, 15, 16, 25, 33, 34, 44) — DONE
- Every node-type create routes through pool (TODO 05/18).
- DTD subsystem uses document pool (TODO 16).
- Parser-side `taurus_sv_to_cstr` uses `p->pool` directly (TODO 25).
- `dtd_parse_quoted_string` pool-routed (TODO 33) — last full.xml leak killed.
- XPath `lang()` function's `taurus_sv_to_cstr` sites pool-routed (TODO 34).
- XML declaration attrs verified leak-free + regression spec (TODO 44).
- **Result: every test fixture reports 0 leaks / 0 bytes.**

### Correctness — robustness (TODO 07, 08) — DONE
- Depth guard (256 levels) on parser recursion (TODO 07).
- `buffer_ensure_capacity` overflow-safe + `alloc_failed` flag (TODO 08).

### Memory pool (TODO 06, 09, 10, 17, 36) — DONE
- Oversized allocations tracked on side list (TODO 06).
- ASCII-text-in-pointer heuristic removed (TODO 09).
- `taurus_pool_total_size` correct + new `taurus_pool_used_size` (TODO 10).
- Dead `taurus_node_create` removed; `taurus_node_free` is a no-op (TODO 17).
- Hash table grows dynamically past 75% load factor (TODO 36).

### Architecture (TODO 14, 23, 29, 30) — DONE
- Vtable struct + global registry indexed by `TaurusNodeTypeEnum` (TODO 29).
- `serialize_node_internal` dispatches via registry — no switch (TODO 30).
- Adding a new node type is purely additive.

### Performance (TODO 11, 22) — DONE
- Bypassed interning for attribute values; attrs.xml 3.4× slower → 1.3× faster.
- All other benchmarks beat or tie libxml2.

### Hygiene (TODO 12, 13, 18, 19, 20, 21, 26, 28, 43) — DONE
- Typedef-redefinition and forward-declaration-visibility resolved (TODO 12).
- Magic numbers → enum constants (TODO 20).
- 12 disabled/legacy files moved to `archive/` (TODO 21).
- Every node type has a single pool-routed create (TODO 18/26).
- **All pointer-type warnings eliminated: 27 → 0** (TODO 19/28/43).
- Dead `taurus_element_add_namespace` static removed (TODO 43).
- SerializeBuffer struct tagged to match forward declaration (TODO 43).

### Thread safety (TODO 27, 38) — DONE
- `g_taurus_strict_mode` and allocator hooks are `__thread` (TODO 27 phase 1).
- New public API: `taurus_document_set_strict(doc, int)`,
  `taurus_document_get_strict(doc)`, `taurus_get_strict_mode()`.
- Two documents in the same thread can have different strict modes (TODO 38).

### Test coverage (TODO 31, 32, 37, 39) — DONE
- SAX parser: 8 specs (TODO 31).
- CLI: 12 specs (TODO 32).
- Encoding: 4 specs covering UTF-16 BOM, malformed UTF-8, ASCII (TODO 37).
- Compact allocator: 3 specs covering multi-document lifecycle (TODO 39).

### Memory safety infrastructure (TODO 35, 40, 46) — DONE
- `TAURUS_ENABLE_ASAN` CMake option (TODO 35).
- libFuzzer harness in `test/fuzz/fuzz_parse.c` (TODO 40).
- **CI workflows added** (TODO 46):
  - `.github/workflows/asan.yml` — ASAN + leak check on every PR.
  - `.github/workflows/fuzz-nightly.yml` — libFuzzer nightly.

### Carry-over design (TODO 24, 41, 42, 45, 47-53) — DESIGN

- TODO 24/42/45: design for splitting `taurus.c` (2900+ lines) into
  focused modules.  Phase 1 (encoding wrapper) sketched; deferred —
  cross-module dependency handling needs a focused refactor session.
- TODO 41: unify string ownership model (three lifetimes → one).
- TODO 47: XPath evaluator deep audit (lang() fixed; rest deferred).
- TODO 48: per-document allocator hooks (currently thread-default).
- TODO 49: pool-route serializer buffer.
- TODO 50: C14N canonicalization specs.
- TODO 51: Doxygen documentation pass.
- TODO 52: remove unused compact_allocator code.
- TODO 53: README accuracy pass.

## Verification snapshot

```
$ cmake -B build -S . -DBUILD_TESTING=ON -DTAURUS_BUILD_CLI=ON \
        -DTAURUS_ENABLE_UTF8PROC=ON -DTAURUS_ENABLE_ICONV=ON
$ cmake --build build
# Zero warnings.
$ ctest --test-dir build
100% tests passed out of 70

$ for f in basic.xml bigattr.xml small.xml full.xml; do
    leaks --atExit -- build/cli/taurus parse $f
  done
basic.xml     0 leaks for 0 bytes    # was 43/944
bigattr.xml   0 leaks for 0 bytes    # was 2/16912
small.xml     0 leaks for 0 bytes    # was 39/896
full.xml      0 leaks for 0 bytes    # was unreachable (crash) → 131KB

# ASAN build:
$ cmake -B build-asan -S . -DTAURUS_ENABLE_ASAN=ON
$ cmake --build build-asan
$ ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan
# 100% pass, zero ASAN errors.

# libFuzzer:
$ cmake -B build-fuzz -S . -DTAURUS_ENABLE_FUZZING=ON
$ cmake --build build-fuzz --target fuzz_parse
$ ./build-fuzz/fuzz_parse -max_total_time=600 corpus/

# CI:
$ # .github/workflows/asan.yml runs on every PR (Linux ASAN + macOS leaks)
$ # .github/workflows/fuzz-nightly.yml runs nightly
```

## Architecture: what landed

### Pool ownership invariant (TODOs 05, 06, 15, 16, 25, 33, 34, 44)

**Every byte the parser allocates that ends up referenced by a document
comes from the document's pool.**  Enforced — every test fixture
reports 0 leaks / 0 bytes.

### Vtable dispatch (TODOs 23, 29, 30, 43)

A global array `g_node_vtables[TAURUS_NODE_TYPE_COUNT]` holds per-type
vtables.  Dispatch: `taurus_node_vtable_for(node->type)->serialize(node, buf)`.
No switch.  Adding a node type is purely additive.

### Adaptive attribute storage (TODO 22)

Attribute names interned; values bypass.  attrs.xml 3.4× slower → 1.3×
faster than libxml2.

### Document-scoped state (TODO 27, 38)

Process-global → `__thread` → per-document.  Two documents in the same
thread can have different strict modes.

### Memory safety infrastructure (TODO 35, 40, 46)

ASAN + libFuzzer + CI workflows.  Regressions caught automatically.

### Zero-warning compile (TODO 43)

`cmake --build build` emits zero warnings.  Achieved by:
- Single source of truth for typedefs (`types_internal.h`).
- Tagged struct for forward-decl compatibility (`struct SerializeBuffer`).
- Explicit casts at every TaurusElement ↔ TaurusNode* boundary.
- Dead static code removed.

## Cross-cutting principles (applied throughout)

1. **Module boundaries are sacred.** Public header for external
   consumers; internal header for sibling modules. Never `#include` a
   `.c` file.
2. **No `extern` of `static` functions** (C analog of "no `send` to
   private methods").
3. **No type-erased `void*` where a typed pointer works** (C analog of
   "no `respond_to?`").
4. **No `require_relative` equivalent** — compile each `.c` separately.
5. **OCP / MECE / DRY.**
6. **Model-driven naming.**
7. **Measure before optimizing.**

## Test policy

70 specs across 10 modules.  Real model instances only — no doubles.
Each spec asserts on observable behavior.  CI runs under
`leaks --atExit --` (macOS), `valgrind --error-exitcode=1` (Linux), and
ASAN (both).

## Definition of done per TODO

- Implementation matches the fix plan.
- New/extended specs pass under `ctest --output-on-failure`.
- `cmake --build build` is warning-clean for touched files.
- `leaks --atExit -- build/cli/taurus parse <file>` reports no
  regressions.

## Remaining work (carry-over)

- **TODO 24/42/45 execution**: split `taurus.c` per the 5-phase plan.
  Phase 1 sketched; deferred (cross-module dependency handling).
- **TODO 41 execution**: unify string ownership model.
- **TODO 47**: XPath evaluator deep audit (lang() fixed; rest deferred).
- **TODO 48**: per-document allocator hooks (currently thread-default).
- **TODO 49**: pool-route serializer buffer.
- **TODO 50**: C14N canonicalization specs.
- **TODO 51**: Doxygen documentation pass.
- **TODO 52**: remove unused compact_allocator code.
- **TODO 53**: README accuracy pass.
