/* core.c — library entry points: version + memory management.
 *
 * Extracted from leptris.c as phase 2 of the file-split roadmap
 * (TODO 42 phase 2).  These functions have no dependencies on the
 * parser or DOM — they're pure library-level entry points.
 *
 * Phase 1 (encoding/wrapper.c) extracted the encoding-aware parse
 * wrapper.  Phase 3+ will extract document lifecycle, navigation,
 * and finalization into their own files.
 */

#include "../../include/leptris.h"
#include "../leptris_internal.h"
#include "common/port.h"

/* Library version.  Single source of truth; CMakeLists.txt should
 * keep this in sync with project(leptris VERSION ...). */
/* Single source: the CMake project version (leptris_VERSION_*)
 * reaches here via target_compile_definitions in src/CMakeLists.txt.
 * Fallback keeps hand-built compilers honest. */
#ifndef LEPTRIS_VERSION
#define LEPTRIS_VERSION "0.26.8"
#endif
#ifndef LEPTRIS_VERSION_MAJOR
#define LEPTRIS_VERSION_MAJOR 0
#endif
#ifndef LEPTRIS_VERSION_MINOR
#define LEPTRIS_VERSION_MINOR 26
#endif
#ifndef LEPTRIS_VERSION_PATCH
#define LEPTRIS_VERSION_PATCH 8
#endif

/* Thread-local allocator hooks (TODO 27/74).
 *
 * When non-NULL, these override the system malloc/free for all
 * leptris-side allocations.  Per-document overrides live on the
 * LeptrisDocument struct (alloc_hook / dealloc_hook fields). */
LEPTRIS_THREAD_LOCAL leptris_allocation_function  g_leptris_alloc_function   = NULL;
LEPTRIS_THREAD_LOCAL leptris_deallocation_function g_leptris_dealloc_function = NULL;

/* The alloc/free hooks that the rest of the library calls.  These
 * are the public face of the thread-local overrides.  Defined here
 * so they have access to the globals. */
void* leptris_alloc_hook(size_t size) {
    return g_leptris_alloc_function ? g_leptris_alloc_function(size) : malloc(size);
}

void leptris_free_hook(void* ptr) {
    if (g_leptris_dealloc_function) g_leptris_dealloc_function(ptr);
    else free(ptr);
}

/* Nonzero when a thread-local allocator override is installed.
 * The pool recycler must stand down so custom-allocator accounting
 * and failure injection keep seeing every malloc/free pair. */
int leptris_custom_allocator_active(void) {
    return (g_leptris_alloc_function || g_leptris_dealloc_function) ? 1 : 0;
}

/* ---- Version API ---- */

LEPTRIS_API const char* leptris_version(void) {
    return LEPTRIS_VERSION;
}

LEPTRIS_API void leptris_version_components(int* major, int* minor, int* patch) {
    if (major) *major = LEPTRIS_VERSION_MAJOR;
    if (minor) *minor = LEPTRIS_VERSION_MINOR;
    if (patch) *patch = LEPTRIS_VERSION_PATCH;
}

LEPTRIS_API void leptris_thread_cleanup(void) {
    extern void leptris_xpath_drain_thread_caches(void);
    extern void leptris_root_doc_drain_thread_caches(void);
    extern void leptris_pool_drain_recycler(void);
    extern void leptris_arena_retain_drain(void);
    leptris_xpath_drain_thread_caches();
    leptris_root_doc_drain_thread_caches();
    leptris_pool_drain_recycler();
    leptris_arena_retain_drain();
}

/* ---- Memory management API ---- */

LEPTRIS_API void leptris_set_memory_management_functions(
    leptris_allocation_function alloc_function,
    leptris_deallocation_function dealloc_function) {
    g_leptris_alloc_function   = alloc_function;
    g_leptris_dealloc_function = dealloc_function;
}

LEPTRIS_API leptris_allocation_function leptris_get_memory_allocation_function(void) {
    return g_leptris_alloc_function;
}

LEPTRIS_API leptris_deallocation_function leptris_get_memory_deallocation_function(void) {
    return g_leptris_dealloc_function;
}

/* #1219: engine-heap buffer for FFI callers whose buffers the
 * engine later frees (DTD PE loaders). The hook pair keeps the
 * custom-allocator configuration symmetric. */
LEPTRIS_API char* leptris_alloc_buffer(size_t len) {
    if (len == 0) return NULL;
    return (char*)leptris_alloc_hook(len);
}

LEPTRIS_API LeptrisStatus leptris_document_set_allocators(
    LeptrisDocument doc,
    leptris_allocation_function alloc,
    leptris_deallocation_function dealloc) {
    if (!doc) return LEPTRIS_ERROR_NULL_ARG;
    doc->alloc_hook   = alloc;
    doc->dealloc_hook = dealloc;
    return LEPTRIS_OK;
}

/* ---- Parser configuration (TODO 38/62) ---- */

/* Thread-local defaults for strict mode and max depth.
 * Documents inherit these at creation; callers can override per-document. */
LEPTRIS_THREAD_LOCAL int g_leptris_strict_mode = 0;
LEPTRIS_THREAD_LOCAL int g_leptris_max_depth   = 0;

/* Internal accessor — called from parser_new.c to read the thread-default. */
int leptris_get_max_depth_default(void) {
    return g_leptris_max_depth;
}

LEPTRIS_API void leptris_set_max_depth(int max_depth) {
    g_leptris_max_depth = max_depth;
}

LEPTRIS_API int leptris_get_max_depth(void) {
    return g_leptris_max_depth > 0 ? g_leptris_max_depth : 256;
}

LEPTRIS_API void leptris_set_strict_mode(int strict) {
    g_leptris_strict_mode = (strict != 0);
}

LEPTRIS_API int leptris_get_strict_mode(void) {
    return g_leptris_strict_mode;
}

LEPTRIS_API LeptrisStatus leptris_document_set_strict(LeptrisDocument doc, int strict) {
    if (!doc) return LEPTRIS_ERROR_NULL_ARG;
    doc->strict_mode = (strict != 0);
    return LEPTRIS_OK;
}

LEPTRIS_API int leptris_document_get_strict(LeptrisDocument doc) {
    return doc ? doc->strict_mode : g_leptris_strict_mode;
}

LEPTRIS_API void leptris_explicit_cleanup(void) {
    extern void leptris_compact_cleanup(void);
    leptris_compact_cleanup();
}

LEPTRIS_API const char* leptris_document_encoding(struct leptris_document* doc) {
    return doc ? doc->encoding : NULL;
}

/* ---- #1094: XML declaration surface (read + mutate) ----
 * The declaration serializes in document position when present
 * (had_declaration); the setters create that presence. */

LEPTRIS_API const char* leptris_document_version(struct leptris_document* doc) {
    return doc ? doc->xml_version : NULL;
}

LEPTRIS_API int leptris_document_standalone(struct leptris_document* doc) {
    return doc ? doc->standalone : -1;
}

/* document_free releases xml_version/encoding with LEPTRIS_FREE,
 * so the setters malloc-copy (and release any prior set value). */
LEPTRIS_API LeptrisStatus leptris_document_set_version(
    struct leptris_document* doc, const char* version) {
    if (!doc) return LEPTRIS_ERROR_NULL_ARG;
    if (!version || !*version) return LEPTRIS_ERROR_INVALID_ARG;
    char* copy = leptris_strdup(version);
    if (!copy) return LEPTRIS_ERROR_MEMORY;
    if (doc->xml_version) leptris_free(doc->xml_version);
    doc->xml_version = copy;
    doc->had_declaration = 1;
    return LEPTRIS_OK;
}

LEPTRIS_API LeptrisStatus leptris_document_set_encoding(
    struct leptris_document* doc, const char* encoding) {
    if (!doc) return LEPTRIS_ERROR_NULL_ARG;
    if (!encoding || !*encoding) return LEPTRIS_ERROR_INVALID_ARG;
    char* copy = leptris_strdup(encoding);
    if (!copy) return LEPTRIS_ERROR_MEMORY;
    if (doc->encoding) leptris_free(doc->encoding);
    doc->encoding = copy;
    doc->had_declaration = 1;
    doc->decl_encoding_verbatim = 1;
    return LEPTRIS_OK;
}

LEPTRIS_API LeptrisStatus leptris_document_set_standalone(
    struct leptris_document* doc, int standalone) {
    if (!doc) return LEPTRIS_ERROR_NULL_ARG;
    doc->standalone = standalone ? 1 : 0;
    doc->had_declaration = 1;
    return LEPTRIS_OK;
}

/* #1229: the remove-half — back to as-if-the-input-had-none. The
 * setters' malloc'd strings are released here (document_free checks
 * for NULL), and the standalone marker returns to unset. */
LEPTRIS_API LeptrisStatus leptris_document_clear_declaration(
    struct leptris_document* doc) {
    if (!doc) return LEPTRIS_ERROR_NULL_ARG;
    if (doc->xml_version) {
        leptris_free(doc->xml_version);
        doc->xml_version = NULL;
    }
    if (doc->encoding) {
        leptris_free(doc->encoding);
        doc->encoding = NULL;
    }
    doc->standalone = -1;
    doc->decl_encoding_verbatim = 0;
    doc->had_declaration = 0;
    return LEPTRIS_OK;
}
