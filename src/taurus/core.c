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
