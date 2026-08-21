# TODO 167 — Build-system wins (ABI constraint removed)

## Status

Pending. With the ABI-stability constraint removed, several
build-system techniques that were off-limits become available.
Most of these are low-risk additive options — users opt in via
CMake flags.

## Why

We've been treating "the shipped binary must use the same flag
set as v0.x" as a hard constraint. The user has explicitly
removed that constraint. Build flags can be made aggressive
without preserving compatibility.

## Plan

### Phase A — `-O3` opt-in for Release

CMake's default `CMAKE_C_FLAGS_RELEASE` is `-O2 -DNDEBUG`. The
`-O3` flag enables more aggressive inlining, loop transforms,
and vectorization. Trade-off: larger binary, longer compile.

Add `LEPTRIS_OPT_LEVEL` cache variable:
- `default` (default): use CMake's `-O2` for Release
- `aggressive`: force `-O3` for Release/RelWithDebInfo

### Phase B — `-march=native` opt-in

The default build targets the lowest common denominator CPU.
`-march=native` lets the compiler use the host's full ISA —
AVX2, AVX-512, BMI2, etc. Binary won't run on older CPUs, so
kept opt-in.

Add `LEPTRIS_TARGET_ARCH` cache variable:
- `default`: baseline (current behavior)
- `native`: `-march=native` (gcc/clang) or `/arch:AVX2` (MSVC)

### Phase C — `-fno-semantic-interposition` for shared libs

GCC/Clang default to allowing symbol interposition in shared
libraries, which prevents inlining of `leptris_*` symbols across
the public/private boundary and prevents LLVM from speculating
devirtualization. `-fno-semantic-interposition` tells the
compiler no interposition will happen, enabling more aggressive
inlining. ~5% win on shared-library builds. Static builds
unaffected.

Apply automatically when `BUILD_SHARED_LIBS=ON` and compiler
supports it.

### Phase D — Add a "fast" preset

Add `cmake/presets/fast.json` that bundles:
- `-DCMAKE_BUILD_TYPE=Release`
- `-DLEPTRIS_OPT_LEVEL=aggressive`
- `-DLEPTRIS_TARGET_ARCH=native`
- `-DLEPTRIS_ENABLE_LTO=ON`
- `-DBUILD_SHARED_LIBS=OFF`

This is the recommended build for maximum single-machine
throughput. Documented as such; not the default.

## Risk

- All phases are opt-in via CMake variables — no default-behavior
  change for existing users.
- `-O3` occasionally exposes latent UB bugs; full test suite
  catches those.
- `-march=native` produces non-portable binaries; clearly
  documented and not default.
- `-fno-semantic-interposition` is a standard GCC/Clang flag,
  no portability concern.

## Expected impact

- `-O3` alone: ~3-5% on parse-heavy workloads.
- `-march=native`: ~5-10% when SIMD-friendly loops hit.
- `-fno-semantic-interposition`: ~5% on shared builds.
- Combined "fast" preset: ~10-15% on top of current Release.

## Status

Pending. One PR per [[feedback-one-pr-per-todo]].
