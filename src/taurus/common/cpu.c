/* common/cpu.c — runtime CPU detection (TODO 175).
 *
 * x86: __builtin_cpu_supports dispatch (GCC/Clang). MSVC exposes
 *      CPUID via __cpuid — treated as SSE42 baseline for simplicity
 *      (every x86_64 CPU shipping since 2008 has SSE4.2).
 * arm64: NEON is architectural baseline — constant.
 */
#include "cpu.h"

#if defined(TAURUS_ARCH_X86) && (defined(__GNUC__) || defined(__clang__))
#  include <immintrin.h>
#  define TAURUS_X86_GCC_BUILTINS 1
#elif defined(_MSC_VER)
#  include <intrin.h>
#endif

taurus_cpu_level taurus_cpu_detect(void) {
#if defined(TAURUS_ARCH_ARM)
    return TAURUS_CPU_NEON;
#elif defined(TAURUS_X86_GCC_BUILTINS)
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx2")) return TAURUS_CPU_AVX2;
    if (__builtin_cpu_supports("sse4.2")) return TAURUS_CPU_SSE42;
    return TAURUS_CPU_SCALAR;
#elif defined(_MSC_VER) && defined(_M_X64)
    /* SSE2 is guaranteed on x64 MSVC targets; conservatively report SSE42. */
    return TAURUS_CPU_SSE42;
#else
    return TAURUS_CPU_SCALAR;
#endif
}

const char* taurus_cpu_level_name(taurus_cpu_level level) {
    switch (level) {
        case TAURUS_CPU_AVX2:  return "avx2";
        case TAURUS_CPU_SSE42: return "sse4.2";
        case TAURUS_CPU_NEON:  return "neon";
        default:               return "scalar";
    }
}
