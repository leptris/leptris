# TODO 175 — AOT SIMD framework (simdjson pattern)

**Priority**: P1 (parallel workstream to compact pointers 178–182)
**Status**: scoped

## Goal

Adopt the simdjson model: hand-written SSE4.2 / AVX2 / NEON intrinsics
compiled ahead-of-time, runtime CPU dispatch via `__builtin_cpu_supports`,
no JIT. Parabix-level wins without LLVM dependency.

## Why

Parabix gets 30%+ on byte-classification via SIMD, but requires LLVM
JIT (50–100 MB dep, cold-start cost). simdjson proves the same wins
are achievable AOT:

- Hand-written intrinsics per ISA, compiled once.
- `__builtin_cpu_supports()` at first call, dispatch table populated.
- Zero runtime deps, ~2 MB binary, works on every taurus target.

taurus's inline amp check (TODO 174) is already this pattern in
microcosm. Formalize it.

## Scope — five phases, one PR each

### Phase 1 — Compile-time detection (`common/cpu.h`)

- `TAURUS_HAS_SSE42`, `TAURUS_HAS_AVX2`, `TAURUS_HAS_AVX512`,
  `TAURUS_HAS_NEON`, `TAURUS_HAS_WASM_SIMD` macros.
- Detect via `__builtin_cpu_supports` (GCC/Clang) /
  `IsProcessorFeaturePresent` (MSVC) at process startup.
- CMake `target_compile_definitions` per-ISA source file.

### Phase 2 — Dispatch table (`common/dispatch.h`)

- `TAURUS_DISPATCH(name, scalar_fn, sse42_fn, avx2_fn, neon_fn)` macro
  expands to a function-pointer resolved at first call.
- TLS-resolved cache (one indirect call per process, not per parse).
- Fallback chain: AVX2 → SSE4.2 → NEON → scalar.

### Phase 3 — Multi-ISA build wiring

- CMake: build each `*_sse42.c` / `*_avx2.c` / `*_neon.c` with the
  appropriate `-mavx2` / `-msse4.2` / `-mfpu=neon` flag.
- Link all into the same library; runtime picks.
- Confirm vcpkg + macOS arm64 + Linux x86_64 all build clean.

### Phase 4 — Migrate inline amp check (TODO 174) to framework

Validation: should be a no-op perf change (already vectorized by -O3)
but proves the framework works end-to-end.

### Phase 5 — Hot-path migrations (separate PRs)

- Tag dispatch (after `<`)
- Whitespace classification
- Name-char scanning
- Quote scanning in attr value

## Estimated impact

5–15% on attr-heavy parse once all migrations land. Compound with
compact pointers (178–182) for cumulative pugixml-parity.

## Risk

Low — framework is additive. Each migration is independently
revertable. Scalar fallback preserves correctness on unsupported CPUs.

## References

- Parabix ARM report: https://mdsz.ca/experience/parabix-arm-project-report/
- simdjson: https://github.com/simdjson/simdjson
- Related: [[176-swar-byte-classification]], [[177-multibyte-literal-matchers]]
