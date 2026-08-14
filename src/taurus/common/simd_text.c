/* common/simd_text.c — scalar implementations + dispatch (TODO 175).
 *
 * The scalar path is always compiled and is the fallback. ISA-specific
 * implementations live in simd_text_avx2.c (compiled with -mavx2 on
 * x86) and simd_text_neon.c (compiled on aarch64 where NEON is
 * baseline). Dispatch happens once per call site chain via the
 * resolved function pointers below.
 */
#include "simd_text.h"
#include "cpu.h"

#include <string.h>

/* ---- Scalar implementations ------------------------------------------- */

static int contains_scalar(const char* s, size_t len, char c) {
    /* Short inputs: inline loop beats memchr's ~10ns call setup
     * (finding from TODO 174). Long inputs: libc memchr is
     * vectorized and unbeatable. */
    if (len <= 16) {
        const char* p = s;
        const char* end = s + len;
        while (p < end) {
            if (*p == c) return 1;
            p++;
        }
        return 0;
    }
    return memchr(s, c, len) != NULL;
}

static ptrdiff_t find_scalar(const char* s, size_t len, char c) {
    const char* hit = (const char*)memchr(s, c, len);
    return hit ? (hit - s) : -1;
}

static ptrdiff_t find3_scalar(const char* s, size_t len,
                               char c0, char c1, char c2) {
    if (len < 3) return -1;
    /* Anchor on first char via memchr, verify the pair. */
    size_t i = 0;
    while (i + 2 < len) {
        const char* hit = (const char*)memchr(s + i, c0, len - 2 - i);
        if (!hit) return -1;
        size_t pos = (size_t)(hit - s);
        if (s[pos + 1] == c1 && s[pos + 2] == c2) return (ptrdiff_t)pos;
        i = pos + 1;
    }
    return -1;
}

/* ---- Dispatch ---------------------------------------------------------- */

/* Resolved pointer — initialized on first use (thread-safe enough:
 * racing threads write identical values; the store is idempotent). */
static int (*g_contains_fn)(const char*, size_t, char) = NULL;

static void dispatch_init(void) {
    taurus_cpu_level lvl = taurus_cpu_detect();
#if defined(TAURUS_ARCH_X86)
    if (lvl >= TAURUS_CPU_AVX2) {
        extern int taurus_text_contains_avx2(const char*, size_t, char);
        g_contains_fn = taurus_text_contains_avx2;
        return;
    }
#endif
#if defined(TAURUS_ARCH_ARM)
    if (lvl >= TAURUS_CPU_NEON) {
        extern int taurus_text_contains_neon(const char*, size_t, char);
        g_contains_fn = taurus_text_contains_neon;
        return;
    }
#endif
    (void)lvl;
    g_contains_fn = contains_scalar;
}

int taurus_text_contains(const char* s, size_t len, char c) {
    if (!g_contains_fn) dispatch_init();
    return g_contains_fn(s, len, c);
}

ptrdiff_t taurus_text_find(const char* s, size_t len, char c) {
    return find_scalar(s, len, c);
}

ptrdiff_t taurus_text_find3(const char* s, size_t len,
                             char c0, char c1, char c2) {
    return find3_scalar(s, len, c0, c1, c2);
}
