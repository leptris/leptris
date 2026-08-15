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

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_COMMON_SIMD_TEXT_H */
