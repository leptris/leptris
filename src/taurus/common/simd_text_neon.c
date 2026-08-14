/* common/simd_text_neon.c — NEON implementations (TODO 175).
 *
 * On aarch64 NEON is architectural baseline — no runtime detection
 * needed and no special compile flags. Compiled unconditionally on
 * arm64 targets.
 *
 * Safety: processes full 16-byte chunks only while 16+ bytes remain,
 * then a scalar tail. Never reads past s+len.
 */
#include "simd_text.h"
#include "cpu.h"

#if defined(TAURUS_ARCH_ARM) && defined(__aarch64__)

#include <arm_neon.h>

int taurus_text_contains_neon(const char* s, size_t len, char c) {
    const char* p = s;
    const char* end = s + len;
    const uint8x16_t needle = vdupq_n_u8((uint8_t)c);

    while (end - p >= 16) {
        uint8x16_t v = vld1q_u8((const uint8_t*)p);
        uint8x16_t eq = vceqq_u8(v, needle);
        if (vmaxvq_u8(eq) != 0) return 1;
        p += 16;
    }
    while (p < end) {
        if (*p == c) return 1;
        p++;
    }
    return 0;
}

#endif /* TAURUS_ARCH_ARM && __aarch64__ */
