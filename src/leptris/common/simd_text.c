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
static ptrdiff_t (*g_find3_fn)(const char*, size_t, char, char, char) = NULL;
static size_t (*g_count_fn)(const char*, size_t, char) = NULL;
static void (*g_count3_fn)(const char*, size_t, char, char, char,
                           size_t*, size_t*, size_t*) = NULL;
static void (*g_copy3_fn)(char*, const char*, size_t, char, char, char,
                          size_t*, size_t*, size_t*) = NULL;

static size_t count_scalar(const char* s, size_t len, char c) {
    size_t n = 0;
    const char* p = s;
    const char* end = s + len;
    while (p < end) {
        const char* hit = (const char*)memchr(p, c, (size_t)(end - p));
        if (!hit) break;
        n++;
        p = hit + 1;
    }
    return n;
}

/* TODO.concurrency/08: resolved at library load (constructors are
 * supported by ELF, Mach-O and MSVC) — the old lazy "if (!fn)
 * dispatch_init()" pattern raced when two threads hit the same
 * entry point on first call. */
static void dispatch_init(void);

#if defined(_MSC_VER)
#  pragma section(".CRT$XCU", read)
static void leptris_simd_dispatch_ctor(void);
__declspec(allocate(".CRT$XCU")) void (*leptris_simd_dispatch_ctor_ptr)(void) = leptris_simd_dispatch_ctor;
static void leptris_simd_dispatch_ctor(void)
#else
__attribute__((constructor))
static void leptris_simd_dispatch_ctor(void)
#endif
{
    dispatch_init();
}

static void dispatch_init(void) {
    leptris_cpu_level lvl = leptris_cpu_detect();
#if defined(LEPTRIS_ARCH_X86) && defined(LEPTRIS_HAS_AVX2_BUILD)
    if (lvl >= LEPTRIS_CPU_AVX2) {
        extern int leptris_text_contains_avx2(const char*, size_t, char);
        extern ptrdiff_t leptris_text_find3_avx2(const char*, size_t, char, char, char);
        extern size_t leptris_text_count_char_avx2(const char*, size_t, char);
        g_contains_fn = leptris_text_contains_avx2;
        g_find3_fn = leptris_text_find3_avx2;
        g_count_fn = leptris_text_count_char_avx2;
        return;
    }
#endif
#if defined(LEPTRIS_ARCH_ARM) && defined(__aarch64__)
    if (lvl >= LEPTRIS_CPU_NEON) {
        extern int leptris_text_contains_neon(const char*, size_t, char);
        extern ptrdiff_t leptris_text_find3_neon(const char*, size_t, char, char, char);
        extern size_t leptris_text_count_char_neon(const char*, size_t, char);
        extern void leptris_text_count3_neon(const char*, size_t, char, char, char,
                                            size_t*, size_t*, size_t*);
        extern void leptris_copy_count3_neon(char*, const char*, size_t, char, char, char,
                                            size_t*, size_t*, size_t*);
        g_contains_fn = leptris_text_contains_neon;
        g_find3_fn = leptris_text_find3_neon;
        g_count_fn = leptris_text_count_char_neon;
        g_count3_fn = leptris_text_count3_neon;
        g_copy3_fn = leptris_copy_count3_neon;
        return;
    }
#endif
    (void)lvl;
    g_contains_fn = contains_scalar;
    g_find3_fn = find3_scalar;
    g_count_fn = count_scalar;
}

int leptris_text_contains(const char* s, size_t len, char c) {
    if (!g_contains_fn) dispatch_init();
    return g_contains_fn(s, len, c);
}

ptrdiff_t leptris_text_find(const char* s, size_t len, char c) {
    return find_scalar(s, len, c);
}

ptrdiff_t leptris_text_find3(const char* s, size_t len,
                             char c0, char c1, char c2) {
    if (!g_find3_fn) dispatch_init();
    return g_find3_fn(s, len, c0, c1, c2);
}

size_t leptris_text_count_char(const char* s, size_t len, char c) {
    if (!g_count_fn) dispatch_init();
    return g_count_fn(s, len, c);
}

void leptris_text_count3(const char* s, size_t len,
                        char c0, char c1, char c2,
                        size_t* n0, size_t* n1, size_t* n2) {
    /* Single memory pass, three counters — the parser's sizing
     * pre-scan walks the document once instead of three times
     * (TODO 184: at 884 KB that is 1.7 MB of avoided L2/DRAM
     * traffic per parse). Falls back to three count_char calls
     * where no SIMD path is compiled. */
    if (g_count3_fn) {
        g_count3_fn(s, len, c0, c1, c2, n0, n1, n2);
        return;
    }
    *n0 = leptris_text_count_char(s, len, c0);
    *n1 = leptris_text_count_char(s, len, c1);
    *n2 = leptris_text_count_char(s, len, c2);
}

void leptris_copy_count3(char* dst, const char* src, size_t len,
                        char c0, char c1, char c2,
                        size_t* n0, size_t* n1, size_t* n2) {
    if (!g_copy3_fn) dispatch_init();
    if (g_copy3_fn) {
        g_copy3_fn(dst, src, len, c0, c1, c2, n0, n1, n2);
        return;
    }
    /* Fallback = exactly the pre-fusion behavior: memcpy + count. */
    memcpy(dst, src, len);
    leptris_text_count3(src, len, c0, c1, c2, n0, n1, n2);
}

/* ---- Structural span scan (TODO 193 Phase 1) ---- */

unsigned char leptris_dp_class_table[256];
/* One-time fill (no constructor attribute — MSVC rejects it):
 * explicit markers for the structural bytes, WS for the whole
 * c <= ' ' range (controls included — pass 2 treats non-whitespace
 * controls as ordinary scan bytes, matching the single-pass
 * parser), 0 elsewhere. Benign duplicate fills write identical
 * values; the flag keeps it one-pass in practice. */
void leptris_dp_class_table_init(void) {
    static volatile int done = 0;
    if (done) return;
    for (int i = 0; i <= ' '; i++) leptris_dp_class_table[i] = DPSCAN_WS;
    leptris_dp_class_table['<']  = DPSCAN_LT;
    leptris_dp_class_table['>']  = DPSCAN_GT;
    leptris_dp_class_table['/']  = DPSCAN_SLASH;
    leptris_dp_class_table['=']  = DPSCAN_EQ;
    leptris_dp_class_table['\''] = DPSCAN_QUOTE_S;
    leptris_dp_class_table['"']  = DPSCAN_QUOTE_D;
    leptris_dp_class_table['&']  = DPSCAN_AMP;
    done = 1;
}

static size_t scan_events_scalar(const char* buf, size_t len,
                                 LeptrisScanEvent* out, size_t max) {
    leptris_dp_class_table_init();
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char cls = leptris_dp_class_table[(unsigned char)buf[i]];
        if (cls) {
            if (n >= max) return max;
            out[n].offset = (uint32_t)i;
            out[n].cls = cls;
            n++;
        }
    }
    return n;
}

static size_t (*g_scan_fn)(const char*, size_t, LeptrisScanEvent*, size_t) = NULL;

size_t leptris_text_scan_events(const char* buf, size_t len,
                               LeptrisScanEvent* out, size_t max) {
    leptris_dp_class_table_init();
    if (!g_scan_fn) {
        g_scan_fn = scan_events_scalar;
#if defined(LEPTRIS_ARCH_ARM) && defined(__aarch64__)
        if (leptris_cpu_detect() >= LEPTRIS_CPU_NEON) {
            extern size_t leptris_text_scan_events_neon(const char*, size_t,
                                                       LeptrisScanEvent*, size_t);
            g_scan_fn = leptris_text_scan_events_neon;
        }
#endif
    }
    return g_scan_fn(buf, len, out, max);
}
