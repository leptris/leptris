/* common/cpu.h — CPU ISA detection for AOT SIMD dispatch (TODO 175).
 *
 * simdjson pattern: compile multiple implementations ahead-of-time,
 * pick the best at runtime via __builtin_cpu_supports (x86) or
 * baseline guarantee (arm64 NEON). No JIT, no runtime codegen.
 *
 * Compile-time macros describe what the CURRENT translation unit was
 * compiled with. Runtime detection (cpu.c) describes what the CPU
 * actually supports — use it to choose between separately-compiled
 * implementation objects.
 */
#ifndef TAURUS_COMMON_CPU_H
#define TAURUS_COMMON_CPU_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Architectures ---------------------------------------------------- */

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#  define TAURUS_ARCH_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON)
#  define TAURUS_ARCH_ARM 1
#  define TAURUS_HAS_NEON 1  /* NEON is baseline on aarch64 */
#elif defined(__wasm__) || defined(__wasm32__) || defined(__EMSCRIPTEN__)
#  define TAURUS_ARCH_WASM 1
#  define TAURUS_HAS_WASM_SIMD 1
#endif

/* ---- Compile-time instruction-set availability (this TU) --------------- */

#if defined(TAURUS_ARCH_X86)
#  if defined(__AVX2__) || defined(_MSC_VER)
#    define TAURUS_COMPILE_AVX2 1
#  endif
#  if defined(__SSE4_2__) || defined(_MSC_VER) || defined(__SSE2__)
#    define TAURUS_COMPILE_SSE 1
#  endif
#endif

/* ---- Runtime detection ------------------------------------------------ */

typedef enum taurus_cpu_level {
    TAURUS_CPU_SCALAR = 0,
    TAURUS_CPU_SSE42  = 1,
    TAURUS_CPU_AVX2   = 2,
    TAURUS_CPU_NEON   = 3
} taurus_cpu_level;

/* Detect the best available level at runtime. Cheap; call once and
 * cache, or call freely — it's a few branches on x86 (guarded by
 * __builtin_cpu_supports) and a constant on arm64. */
taurus_cpu_level taurus_cpu_detect(void);

/* Human-readable name for diagnostics. */
const char* taurus_cpu_level_name(taurus_cpu_level level);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_COMMON_CPU_H */
