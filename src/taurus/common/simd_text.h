/* common/simd_text.h — SIMD-accelerated text scan primitives (TODO 175).
 *
 * First consumers of the AOT SIMD framework. Each function has a
 * scalar implementation (always compiled) plus ISA-specialized
 * implementations compiled in separate TUs with the appropriate
 * -m flags (see src/CMakeLists.txt). Dispatch picks the best at
 * runtime via taurus_cpu_detect().
 *
 * Contract: all functions may read at most len bytes. Implementations
 * must not fault on reads beyond len (use length-guarded chunking,
 * not full-width loads past the end).
 */
#ifndef TAURUS_COMMON_SIMD_TEXT_H
#define TAURUS_COMMON_SIMD_TEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns nonzero if [s, s+len) contains the byte c, else 0.
 * len == 0 returns 0. */
int taurus_text_contains(const char* s, size_t len, char c);

/* Returns the offset of the first occurrence of c in [s, s+len),
 * or -1 if not found. */
ptrdiff_t taurus_text_find(const char* s, size_t len, char c);

/* Returns the offset of the first occurrence of the 3-byte sequence
 * c0c1c2 in [s, s+len), or -1 if not found. len < 3 returns -1. */
ptrdiff_t taurus_text_find3(const char* s, size_t len,
                             char c0, char c1, char c2);

/* Count occurrences of c in [s, s+len). SIMD-accelerated where
 * available (32/16 bytes per chunk via movemask popcount / vaddvq);
 * scalar memchr-hop fallback. Used for content-derived arena sizing
 * (TODO 183 Phase 3) where occurrence counts can reach ~10^5 —
 * memchr-hopping per hit costs ~10ns each there. */
size_t taurus_text_count_char(const char* s, size_t len, char c);

/* Count occurrences of c0, c1, and c2 in [s, s+len) — one memory
 * pass where a SIMD path is compiled (falls back to three
 * count_char calls otherwise). The parser's sizing pre-scan walks
 * the document once instead of three times. */
void taurus_text_count3(const char* s, size_t len,
                        char c0, char c1, char c2,
                        size_t* n0, size_t* n1, size_t* n2);

/* Copy [src, src+len) to dst while counting c0/c1/c2 occurrences —
 * one fused memory pass (falls back to memcpy + count3 where no
 * SIMD path is compiled). The parser's owns-copy path gets its
 * buffer copy and its arena-sizing counters from a single
 * traversal of the input instead of two. TODO 188. */
void taurus_copy_count3(char* dst, const char* src, size_t len,
                        char c0, char c1, char c2,
                        size_t* n0, size_t* n1, size_t* n2);

/* ---- Structural span scan (TODO 193 Phase 1: parser v3) ----
 *
 * Records every byte of the XML structural set in [buf, buf+len)
 * as an event. Pass 2 of the two-pass parser walks ONLY these
 * events (typically ~1 per 8-15 bytes in real XML), replacing the
 * byte-at-a-time char+table classification that dominates the
 * single-pass parser (89% of K=50 parse time) with one vector
 * traversal plus a per-hit class lookup. */

#define DPSCAN_WS       1u  /* c <= ' ' (whitespace/controls) */
#define DPSCAN_LT       2u  /* '<' */
#define DPSCAN_GT       3u  /* '>' */
#define DPSCAN_SLASH    4u  /* '/' */
#define DPSCAN_EQ       5u  /* '=' */
#define DPSCAN_QUOTE_S  6u  /* '\'' */
#define DPSCAN_QUOTE_D  7u  /* '"' */
#define DPSCAN_AMP      8u  /* '&' */

typedef struct {
    uint32_t offset;   /* byte offset from buf */
    uint8_t  cls;      /* DPSCAN_* — never 0 */
} TaurusScanEvent;

/* Shared classification table: taurus_dp_class_table[byte] is 0 for
 * ordinary bytes, else the DPSCAN_* class. The <= ' ' class means
 * control bytes are reported as WS; pass 2 treats non-XML controls
 * as ordinary scan bytes, matching the single-pass parser. */
extern unsigned char taurus_dp_class_table[256];

/* Idempotent one-time fill of the table (call before first use or
 * rely on taurus_text_scan_events to call it). */
void taurus_dp_class_table_init(void);

/* Append events for every structural byte to out (capacity max).
 * Returns the number of events written; returns max unchanged as
 * an overflow signal (caller must detect return == max and treat
 * the scan as truncated). len == 0 returns 0. */
size_t taurus_text_scan_events(const char* buf, size_t len,
                               TaurusScanEvent* out, size_t max);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_COMMON_SIMD_TEXT_H */
