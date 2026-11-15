/* common/cpu.c — runtime CPU detection (TODO 175).
 *
 * x86: __builtin_cpu_supports dispatch (GCC/Clang). MSVC exposes
 *      CPUID via __cpuid — treated as SSE42 baseline for simplicity
 *      (every x86_64 CPU shipping since 2008 has SSE4.2).
 * arm64: NEON is architectural baseline — constant.
 *
 * #2039: CPUID can claim AVX2 that the execution environment cannot
 * run — Rosetta 2 reports AVX2 in CPUID and SIGILLs on the first
 * vector op; QEMU user-mode carries the same gap class. A claimed
 * AVX2 level is therefore verified with an execute-canary (one
 * vpaddb under a SIGILL guard) before it is blessed; the verdict is
 * computed once per process and rides the same cache as the level.
 */
#include "cpu.h"

#if defined(LEPTRIS_ARCH_X86) && (defined(__GNUC__) || defined(__clang__))
#  include <immintrin.h>
#  define LEPTRIS_X86_GCC_BUILTINS 1
#elif defined(_MSC_VER)
#  include <intrin.h>
#endif

#if defined(LEPTRIS_X86_GCC_BUILTINS)
#  include <setjmp.h>
#  include <signal.h>

static sigjmp_buf leptris_canary_env;
static volatile sig_atomic_t leptris_canary_fired = 0;

static void leptris_canary_sigill(int sig) {
    (void)sig;
    leptris_canary_fired = 1;
    siglongjmp(leptris_canary_env, 1);
}

/* 1 = AVX2 instructions actually execute; 0 = CPUID lied. A probe
 * setup failure trusts CPUID (signal() unavailable is not a case any
 * supported platform hits, and the previous behavior was unconditional
 * trust). */
static int leptris_avx2_executable(void) {
    void (*old)(int) = signal(SIGILL, leptris_canary_sigill);
    if (old == SIG_ERR) {
        return 1;
    }
    int ok = 1;
    leptris_canary_fired = 0;
    if (sigsetjmp(leptris_canary_env, 1) == 0) {
        /* Reads/writes only xmm0 (caller-saved, clobber-declared): no
         * memory operand, no state change — a pure "can the pipeline
         * retire this opcode" probe. */
        __asm__ volatile("vpaddb %%xmm0, %%xmm0, %%xmm0" : : : "xmm0");
    }
    if (leptris_canary_fired) {
        ok = 0;
    }
    signal(SIGILL, old);
    return ok;
}
#endif

leptris_cpu_level leptris_cpu_detect(void) {
#if defined(LEPTRIS_ARCH_ARM)
    return LEPTRIS_CPU_NEON;
#elif defined(LEPTRIS_X86_GCC_BUILTINS)
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx2") && leptris_avx2_executable()) {
        return LEPTRIS_CPU_AVX2;
    }
    if (__builtin_cpu_supports("sse4.2")) return LEPTRIS_CPU_SSE42;
    return LEPTRIS_CPU_SCALAR;
#elif defined(_MSC_VER) && defined(_M_X64)
    /* SSE2 is guaranteed on x64 MSVC targets; conservatively report SSE42. */
    return LEPTRIS_CPU_SSE42;
#else
    return LEPTRIS_CPU_SCALAR;
#endif
}

const char* leptris_cpu_level_name(leptris_cpu_level level) {
    switch (level) {
        case LEPTRIS_CPU_AVX2:  return "avx2";
        case LEPTRIS_CPU_SSE42: return "sse4.2";
        case LEPTRIS_CPU_NEON:  return "neon";
        default:               return "scalar";
    }
}
