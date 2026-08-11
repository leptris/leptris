/* common/port.h — Portability shims for non-GCC/Clang compilers (MSVC).
 *
 * Centralizes the small set of compiler intrinsics and GCC-isms the
 * codebase uses so the rest of the source stays portable:
 *
 *   - TAURUS_CTZ(mask) — count trailing zeros (GCC: __builtin_ctz,
 *     MSVC: _BitScanForward).
 *   - strdup/strndup shims for MSVC's CRT (which only ships _strdup
 *     and lacks strndup entirely).
 *
 * Add to this file rather than scattering #ifdef _MSC_VER across the
 * codebase. */
#ifndef TAURUS_COMMON_PORT_H
#define TAURUS_COMMON_PORT_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#if defined(_MSC_VER)
#  include <intrin.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Count trailing zeros --------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
#  define TAURUS_CTZ(mask) __builtin_ctz(mask)
#elif defined(_MSC_VER)
static __inline int taurus_msvc_ctz(uint32_t mask) {
    unsigned long pos;
    if (_BitScanForward(&pos, mask)) return (int)pos;
    return 32;
}
#  define TAURUS_CTZ(mask) taurus_msvc_ctz((uint32_t)(mask))
#else
/* Fallback: O(n) loop. */
static __inline int taurus_port_ctz(uint32_t mask) {
    if (mask == 0) return 32;
    int n = 0;
    while ((mask & 1u) == 0) { mask >>= 1; n++; }
    return n;
}
#  define TAURUS_CTZ(mask) taurus_port_ctz((uint32_t)(mask))
#endif

/* ---- Library-load constructor ----------------------------------------- */
/* Runs a function once at DLL/EXE load. GCC/Clang use the constructor
 * attribute; MSVC uses the .CRT$XCU section with a function pointer.
 *
 * Usage at file scope:
 *
 *   static void my_init(void) { ... }
 *   TAURUS_CONSTRUCTOR(my_init)
 *
 * The macro emits either an __attribute__((constructor)) declaration
 * (GCC/Clang) or an MSVC CRT-initializer slot that points to my_init.
 */
#if defined(__GNUC__) || defined(__clang__)
#  define TAURUS_CONSTRUCTOR(fn) \
       __attribute__((constructor)) static void fn##_ctor(void) { fn(); }
#elif defined(_MSC_VER)
#  define TAURUS_CONSTRUCTOR(fn) \
       __pragma(section(".CRT$XCU", read)) \
       __declspec(allocate(".CRT$XCU")) \
       static void (*fn##_crt_init)(void) = (fn);
#else
/* Fallback: no-op. Caller must invoke init explicitly. */
#  define TAURUS_CONSTRUCTOR(fn) \
       static void fn##_unused(void) { (void)fn; }
#endif

/* ---- Thread-local storage --------------------------------------------- */
/* GCC/Clang accept __thread directly; MSVC's C compiler doesn't
 * recognize __thread (use __declspec(thread) instead). */
#if defined(_MSC_VER)
#  define TAURUS_THREAD_LOCAL __declspec(thread)
#else
#  define TAURUS_THREAD_LOCAL __thread
#endif

/* ---- POSIX string functions ------------------------------------------- */

#if defined(_MSC_VER)
/* MSVC's CRT ships _strdup (no strdup). Map strdup to _strdup.
 * strndup is missing entirely — provide a tiny shim. */
#  define strdup(s) _strdup(s)

#include <stddef.h>  /* size_t */
static __inline char* taurus_strndup(const char* s, size_t n) {
    size_t len = 0;
    while (len < n && s[len] != '\0') len++;
    char* p = (char*)malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}
#  define strndup(s, n) taurus_strndup((s), (n))

/* MSVC lacks POSIX strcasecmp/strncasecmp. Map to _stricmp/_strnicmp. */
#  define strcasecmp(s1, s2) _stricmp((s1), (s2))
#  define strncasecmp(s1, s2, n) _strnicmp((s1), (s2), (n))

/* MSVC's strtok_s has the same three-arg signature as POSIX strtok_r. */
#  define strtok_r(s, delim, ctx) strtok_s((s), (delim), (ctx))
#else
/* POSIX: strcasecmp/strncasecmp live in <strings.h>. */
#  include <strings.h>
#endif

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_COMMON_PORT_H */
