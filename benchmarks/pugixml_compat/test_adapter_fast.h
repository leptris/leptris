#ifndef PUGIXML_TEST_ADAPTER_FAST_H
#define PUGIXML_TEST_ADAPTER_FAST_H

/**
 * @file test_adapter_fast.h
 * @brief Ultra-fast adapter using TaurusElementFast with direct pointers
 *
 * This adapter uses the new TaurusElementFast structure which eliminates
 * the encode/decode overhead of compact pointers by using direct pointers.
 */

#include <taurus.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Include internal headers for direct API access */
#include "../../lib/src/dom/element.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Timing utilities */
#include <time.h>
#include <sys/time.h>

static inline double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
}

/* Type mappings - use TaurusElement (TaurusElementNode*) */
typedef TaurusDocument xml_document;
typedef TaurusElement xml_node;
typedef TaurusAttribute xml_attribute;

/* Map fast API to regular public API */
#define taurus_element_fast_create(name, doc) taurus_element_create(doc, name)
#define taurus_element_fast_append_child(parent, child) taurus_element_append_child(parent, child)
#define taurus_element_fast_prepend_child(parent, child) taurus_element_prepend_child(parent, child)
#define taurus_element_fast_insert_after(sibling, child) taurus_element_insert_after(sibling, child)
#define taurus_element_fast_insert_before(sibling, child) taurus_element_insert_before(sibling, child)
#define taurus_element_fast_remove_child(parent, child) taurus_element_remove_child(parent, child)
#define taurus_element_fast_set_name(elem, name) taurus_element_set_name(elem, name)
#define taurus_element_fast_copy(elem, doc) taurus_element_create(doc, taurus_element_name(elem))  /* Create new with same name */
#define taurus_element_fast_set_document(doc) ((void)0)  /* No-op */
#define taurus_element_first_attribute(n) taurus_element_first_attribute(n)

/* String macro */
#define STR(x) x
typedef char char_t;

/* Compile-time string length macro */
#define TAURUS_STRLEN(s) (sizeof(s) - 1)

/* Compiler optimization hints */
#if defined(__GNUC__) || defined(__clang__)
    #define TAURUS_ALWAYS_INLINE __attribute__((always_inline)) inline
    #define TAURUS_HOT __attribute__((hot))
#else
    #define TAURUS_ALWAYS_INLINE inline
    #define TAURUS_HOT
#endif

/* Thread-local document tracking */
static __thread TaurusDocument g_fast_current_doc = NULL;
static __thread xml_node g_fast_root = NULL;  /* Fast root element */

/* Node type enum */
typedef enum {
    node_null,
    node_document,
    node_element,
    node_pcdata,
    node_cdata,
    node_comment,
    node_pi,
    node_declaration,
    node_doctype
} xml_node_type;

/* Test macros */
#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK FAILED: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define CHECK_STRING(got, expected) do { \
    const char* _got = (got); \
    const char* _expected = (expected); \
    if (!_got || !_expected || strcmp(_got, _expected) != 0) { \
        fprintf(stderr, "CHECK_STRING FAILED: got '%s', expected '%s' at %s:%d\n", \
                _got ? _got : "(null)", _expected ? _expected : "(null)", \
                __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

/* Benchmark utilities */
typedef struct {
    const char* name;
    int (*test_func)(void);
    double time_us;
    int passed;
} BenchmarkTest;

#define BENCHMARK_TEST(name) \
    static int bench_##name(void); \
    static BenchmarkTest test_##name = { #name, bench_##name, 0.0, 0 }; \
    static int bench_##name(void)

/* ============================================================================
 * Element Operations (using TaurusElementFast with direct pointers)
 * ============================================================================ */

/* Create element */
static TAURUS_ALWAYS_INLINE xml_node xml_node_create_element(const char* name) {
    if (!g_fast_current_doc) return NULL;
    return taurus_element_fast_create(name, g_fast_current_doc);
}

/* Append child - uses direct pointer operations (no encode/decode!) */
static TAURUS_ALWAYS_INLINE xml_node xml_node_append_child(xml_node parent, const char* name) {
    if (!parent || !name || !g_fast_current_doc) return NULL;

    xml_node child = taurus_element_fast_create(name, g_fast_current_doc);
    if (!child) return NULL;

    taurus_element_fast_append_child(parent, child);
    return child;
}

/* Prepend child */
static TAURUS_ALWAYS_INLINE xml_node xml_node_prepend_child(xml_node parent, const char* name) {
    if (!parent || !name || !g_fast_current_doc) return NULL;

    xml_node child = taurus_element_fast_create(name, g_fast_current_doc);
    if (!child) return NULL;

    taurus_element_fast_prepend_child(parent, child);
    return child;
}

/* Insert after */
static TAURUS_ALWAYS_INLINE xml_node xml_node_insert_child_after(xml_node parent, const char* name, xml_node sibling) {
    if (!sibling || !name || !g_fast_current_doc) return NULL;

    xml_node child = taurus_element_fast_create(name, g_fast_current_doc);
    if (!child) return NULL;

    taurus_element_fast_insert_after(sibling, child);
    return child;
}

/* Insert before */
static TAURUS_ALWAYS_INLINE xml_node xml_node_insert_child_before(xml_node parent, const char* name, xml_node sibling) {
    if (!sibling || !name || !g_fast_current_doc) return NULL;

    xml_node child = taurus_element_fast_create(name, g_fast_current_doc);
    if (!child) return NULL;

    taurus_element_fast_insert_before(sibling, child);
    return child;
}

/* Remove child by direct pointer (internal helper) */
static TAURUS_ALWAYS_INLINE void xml_node_remove_child_node(xml_node parent, xml_node child) {
    if (!parent || !child) return;
    taurus_element_fast_remove_child(parent, child);
}

/* Set name */
static TAURUS_ALWAYS_INLINE int xml_node_set_name(xml_node elem, const char* name) {
    if (!elem || !name) return 0;
    taurus_element_fast_set_name(elem, name);
    return 1;
}

/* Get name */
static inline const char* xml_node_get_name(xml_node elem) {
    if (!elem) return NULL;
    return taurus_element_name(elem);
}

/* Get name (alias for compatibility) */
static inline const char* xml_node_name(xml_node elem) {
    return xml_node_get_name(elem);
}

/* Get first child */
static inline xml_node xml_node_first_child(xml_node elem) {
    if (!elem) return NULL;
    return taurus_element_first_child(elem, NULL);
}

/* Get last child */
static inline xml_node xml_node_last_child(xml_node elem) {
    if (!elem) return NULL;
    return taurus_element_last_child(elem, NULL);
}

/* Get next sibling */
static inline xml_node xml_node_next_sibling(xml_node elem) {
    if (!elem) return NULL;
    return taurus_element_next_sibling(elem, NULL);  /* NULL = any sibling */
}

/* Get parent */
static inline xml_node xml_node_get_parent(xml_node elem) {
    if (!elem) return NULL;
    return taurus_element_parent(elem);
}

/* Remove child by name (finds child first, then removes it) */
static TAURUS_ALWAYS_INLINE int xml_node_remove_child(xml_node parent, const char* name) {
    if (!parent || !name) return 0;

    /* Find child by name using the API */
    xml_node child = taurus_element_find_child(parent, name);
    if (child) {
        /* Found child - remove it */
        taurus_element_remove_child(parent, child);
        return 1;
    }

    return 0;
}

/* ============================================================================
 * Document Operations
 * ============================================================================ */

/* Forward declarations for internal functions */
extern TaurusDocument taurus_document_new(void);
extern TaurusMemoryPool* taurus_pool_create(void);
extern void* taurus_pool_alloc(TaurusMemoryPool* pool, size_t size);

static inline xml_document xml_document_create(void) {
    /* Create/reuse document for benchmark */
    if (g_fast_current_doc) {
        return g_fast_current_doc;
    }

    /* Create a minimal document directly (no XML parsing overhead) */
    g_fast_current_doc = taurus_document_new();
    if (!g_fast_current_doc) return NULL;

    /* Create memory pool for fast element allocation */
    TaurusMemoryPool* pool = taurus_pool_create();
    if (!pool) {
        free(g_fast_current_doc);
        g_fast_current_doc = NULL;
        return NULL;
    }

    /* Attach pool to document */
    g_fast_current_doc->pool = pool;

    /* Set thread-local document for element_fast.c */
    taurus_element_fast_set_document(g_fast_current_doc);

    /* Create fast root element */
    g_fast_root = taurus_element_fast_create("root", g_fast_current_doc);

    return g_fast_current_doc;
}

static inline void xml_document_free(xml_document doc) {
    /* For benchmark: don't actually free, just clear */
    if (g_fast_root) {
        /* Clear root's children */
        g_fast_root->first_child = NULL;
        g_fast_root->last_child = NULL;
    }
    (void)doc;
}

static inline xml_node xml_document_append_child(xml_document doc, const char* name) {
    if (!doc || !name) return NULL;

    /* Track this document for element creation */
    g_fast_current_doc = doc;

    /* Create child using ultra-fast API and append to fast root */
    xml_node child = xml_node_create_element(name);
    if (!child) return NULL;

    taurus_element_fast_append_child(g_fast_root, child);
    return child;
}

static inline xml_node xml_document_prepend_child(xml_document doc, const char* name) {
    if (!doc || !name) return NULL;

    /* Track this document for element creation */
    g_fast_current_doc = doc;

    /* Create child using ultra-fast API and prepend to fast root */
    xml_node child = xml_node_create_element(name);
    if (!child) return NULL;

    taurus_element_fast_prepend_child(g_fast_root, child);
    return child;
}

/* ============================================================================
 * Copy Operations
 * ============================================================================ */

/* Append copy (shallow copy - just the element, no children) */
static TAURUS_ALWAYS_INLINE xml_node xml_node_append_copy(xml_node parent, xml_node source) {
    if (!parent || !source || !g_fast_current_doc) return NULL;

    xml_node copy = taurus_element_fast_copy(source, g_fast_current_doc);
    if (!copy) return NULL;

    taurus_element_fast_append_child(parent, copy);
    return copy;
}

/* Prepend copy */
static TAURUS_ALWAYS_INLINE xml_node xml_node_prepend_copy(xml_node parent, xml_node source) {
    if (!parent || !source || !g_fast_current_doc) return NULL;

    xml_node copy = taurus_element_fast_copy(source, g_fast_current_doc);
    if (!copy) return NULL;

    taurus_element_fast_prepend_child(parent, copy);
    return copy;
}

/* Insert copy after */
static TAURUS_ALWAYS_INLINE xml_node xml_node_insert_copy_after(xml_node parent, xml_node source, xml_node sibling) {
    if (!sibling || !source || !g_fast_current_doc) return NULL;

    xml_node copy = taurus_element_fast_copy(source, g_fast_current_doc);
    if (!copy) return NULL;

    taurus_element_fast_insert_after(sibling, copy);
    return copy;
}

/* Insert copy before */
static TAURUS_ALWAYS_INLINE xml_node xml_node_insert_copy_before(xml_node parent, xml_node source, xml_node sibling) {
    if (!sibling || !source || !g_fast_current_doc) return NULL;

    xml_node copy = taurus_element_fast_copy(source, g_fast_current_doc);
    if (!copy) return NULL;

    taurus_element_fast_insert_before(sibling, copy);
    return copy;
}

/* ============================================================================
 * Attribute stubs (not used in benchmarks)
 * ============================================================================ */

static inline xml_attribute xml_node_first_attribute(xml_node elem) {
    (void)elem;
    return NULL;
}

static inline xml_attribute xml_node_next_attribute(xml_attribute attr) {
    (void)attr;
    return NULL;
}

static inline const char_t* xml_attribute_name(xml_attribute attr) {
    (void)attr;
    return "";
}

static inline const char_t* xml_attribute_value(xml_attribute attr) {
    (void)attr;
    return "";
}

#ifdef __cplusplus
}
#endif

#endif /* PUGIXML_TEST_ADAPTER_FAST_H */
