/* core.c — library entry points: version + memory management.
 *
 * Extracted from taurus.c as phase 2 of the file-split roadmap
 * (TODO 42 phase 2).  These functions have no dependencies on the
 * parser or DOM — they're pure library-level entry points.
 *
 * Phase 1 (encoding/wrapper.c) extracted the encoding-aware parse
 * wrapper.  Phase 3+ will extract document lifecycle, navigation,
 * and finalization into their own files.
 */

#include "../../include/taurus.h"
#include "../taurus_internal.h"

/* Library version.  Single source of truth; CMakeLists.txt should
 * keep this in sync with project(taurus VERSION ...). */
#define TAURUS_VERSION "0.1.0"
#define TAURUS_VERSION_MAJOR 0
#define TAURUS_VERSION_MINOR 1
#define TAURUS_VERSION_PATCH 0

/* Thread-local allocator hooks (TODO 27/74).
 *
 * When non-NULL, these override the system malloc/free for all
 * taurus-side allocations.  Per-document overrides live on the
 * TaurusDocument struct (alloc_hook / dealloc_hook fields). */
__thread taurus_allocation_function  g_taurus_alloc_function   = NULL;
__thread taurus_deallocation_function g_taurus_dealloc_function = NULL;

/* The alloc/free hooks that the rest of the library calls.  These
 * are the public face of the thread-local overrides.  Defined here
 * so they have access to the globals. */
void* taurus_alloc_hook(size_t size) {
    return g_taurus_alloc_function ? g_taurus_alloc_function(size) : malloc(size);
}

void taurus_free_hook(void* ptr) {
    if (g_taurus_dealloc_function) g_taurus_dealloc_function(ptr);
    else free(ptr);
}

/* ---- Version API ---- */

TAURUS_API const char* taurus_version(void) {
    return TAURUS_VERSION;
}

TAURUS_API void taurus_version_components(int* major, int* minor, int* patch) {
    if (major) *major = TAURUS_VERSION_MAJOR;
    if (minor) *minor = TAURUS_VERSION_MINOR;
    if (patch) *patch = TAURUS_VERSION_PATCH;
}

/* ---- Memory management API ---- */

TAURUS_API void taurus_set_memory_management_functions(
    taurus_allocation_function alloc_function,
    taurus_deallocation_function dealloc_function) {
    g_taurus_alloc_function   = alloc_function;
    g_taurus_dealloc_function = dealloc_function;
}

TAURUS_API taurus_allocation_function taurus_get_memory_allocation_function(void) {
    return g_taurus_alloc_function;
}

TAURUS_API taurus_deallocation_function taurus_get_memory_deallocation_function(void) {
    return g_taurus_dealloc_function;
}

TAURUS_API TaurusStatus taurus_document_set_allocators(
    TaurusDocument doc,
    taurus_allocation_function alloc,
    taurus_deallocation_function dealloc) {
    if (!doc) return TAURUS_ERROR_NULL_ARG;
    doc->alloc_hook   = alloc;
    doc->dealloc_hook = dealloc;
    return TAURUS_OK;
}

/* ---- Parser configuration (TODO 38/62) ---- */

/* Thread-local defaults for strict mode and max depth.
 * Documents inherit these at creation; callers can override per-document. */
__thread int g_taurus_strict_mode = 0;
__thread int g_taurus_max_depth   = 0;

/* Internal accessor — called from parser_new.c to read the thread-default. */
int taurus_get_max_depth_default(void) {
    return g_taurus_max_depth;
}

TAURUS_API void taurus_set_max_depth(int max_depth) {
    g_taurus_max_depth = max_depth;
}

TAURUS_API int taurus_get_max_depth(void) {
    return g_taurus_max_depth > 0 ? g_taurus_max_depth : 256;
}

TAURUS_API void taurus_set_strict_mode(int strict) {
    g_taurus_strict_mode = (strict != 0);
}

TAURUS_API int taurus_get_strict_mode(void) {
    return g_taurus_strict_mode;
}

TAURUS_API TaurusStatus taurus_document_set_strict(TaurusDocument doc, int strict) {
    if (!doc) return TAURUS_ERROR_NULL_ARG;
    doc->strict_mode = (strict != 0);
    return TAURUS_OK;
}

TAURUS_API int taurus_document_get_strict(TaurusDocument doc) {
    return doc ? doc->strict_mode : g_taurus_strict_mode;
}

TAURUS_API void taurus_explicit_cleanup(void) {
    extern void taurus_compact_cleanup(void);
    taurus_compact_cleanup();
}

TAURUS_API const char* taurus_document_encoding(struct taurus_document* doc) {
    return doc ? doc->encoding : NULL;
}
