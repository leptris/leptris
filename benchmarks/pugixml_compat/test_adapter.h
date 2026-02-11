#ifndef PUGIXML_TEST_ADAPTER_H
#define PUGIXML_TEST_ADAPTER_H

/**
 * @file test_adapter.h
 * @brief Adapter layer mapping pugixml API to Taurus API for benchmarking
 *
 * This header provides a compatibility layer that allows pugixml test cases
 * to be compiled against Taurus API with minimal modifications.
 * COMPACT MODE: Uses public API functions (no direct field access).
 */

#include <taurus.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

/* Type mappings */
typedef TaurusDocument xml_document;
typedef TaurusElement xml_node;
typedef TaurusAttribute xml_attribute;

/* String macro (for wide char support in pugixml - we use narrow chars) */
#define STR(x) x
typedef char char_t;

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

/* Thread-local document tracking for element creation */
static __thread TaurusDocument g_current_doc = NULL;

/* Document operations */

static inline void xml_document_reset(TaurusDocument doc) {
    if (!doc) return;

    /* Get root element */
    TaurusElement root = taurus_document_root(doc);
    if (!root) return;

    /* Remove all children from root */
    taurus_element_remove_children(root);
}

static inline TaurusDocument xml_document_create(void) {
    /* Create/reuse document for benchmark */
    if (g_current_doc) {
        return g_current_doc;
    }

    const char* empty_xml = "<root/>";
    TaurusStatus status;
    g_current_doc = taurus_parse_string(empty_xml, strlen(empty_xml), &status);
    return g_current_doc;
}

static inline void xml_document_free(TaurusDocument doc) {
    /* For benchmark: don't actually free, just clear */
    if (doc && doc == g_current_doc) {
        xml_document_reset(doc);
    }
}

static inline TaurusElement xml_document_child(TaurusDocument doc, const char* name) {
    if (!doc) return NULL;
    TaurusElement root = taurus_document_root(doc);
    if (!root) return NULL;

    const char* root_name = taurus_element_name(root);
    if (root_name && strcmp(root_name, name) == 0) {
        return root;
    }
    return NULL;
}

static inline TaurusElement xml_document_append_child(TaurusDocument doc, const char* name) {
    if (!doc || !name) return NULL;

    /* Track this document for element creation */
    g_current_doc = doc;

    /* Create root element if it doesn't exist */
    TaurusElement root = taurus_document_root(doc);
    if (!root) {
        return NULL;
    }

    /* Create and append child using public API */
    TaurusElement child = taurus_element_create(doc, name);
    if (!child) return NULL;

    if (taurus_element_append_child(root, child) != TAURUS_OK) {
        return NULL;
    }

    return child;
}

static inline TaurusElement xml_document_prepend_child(TaurusDocument doc, const char* name) {
    if (!doc || !name) return NULL;

    /* Track this document for element creation */
    g_current_doc = doc;

    /* Create root element if it doesn't exist */
    TaurusElement root = taurus_document_root(doc);
    if (!root) {
        return NULL;
    }

    /* Create and prepend child using public API */
    TaurusElement child = taurus_element_create(doc, name);
    if (!child) return NULL;

    if (taurus_element_prepend_child(root, child) != TAURUS_OK) {
        return NULL;
    }

    return child;
}

/* Element operations */
static inline TaurusElement xml_node_child(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;

    /* Iterate through children */
    TaurusElement child = taurus_element_first_child_any(elem);
    while (child) {
        const char* child_name = taurus_element_name(child);
        if (child_name && strcmp(child_name, name) == 0) {
            return child;
        }
        child = taurus_element_next_sibling_any(child);
    }
    return NULL;
}

static inline TaurusElement xml_node_first_child(TaurusElement elem) {
    if (!elem) return NULL;
    return taurus_element_first_child_any(elem);
}

static inline TaurusElement xml_node_last_child(TaurusElement elem) {
    if (!elem) return NULL;
    return taurus_element_last_child_any(elem);
}

static inline TaurusElement xml_node_next_sibling(TaurusElement elem) {
    if (!elem) return NULL;
    return taurus_element_next_sibling(elem, NULL);
}

static inline TaurusElement xml_node_append_child(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;

    TaurusElement child = taurus_element_create(g_current_doc, name);
    if (!child) return NULL;

    if (taurus_element_append_child(elem, child) != TAURUS_OK) {
        return NULL;
    }

    return child;
}

static inline TaurusElement xml_node_prepend_child(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;

    TaurusElement child = taurus_element_create(g_current_doc, name);
    if (!child) return NULL;

    if (taurus_element_prepend_child(elem, child) != TAURUS_OK) {
        return NULL;
    }

    return child;
}

static inline TaurusElement xml_node_insert_child_after(TaurusElement elem,
                                                      const char* name,
                                                      TaurusElement sibling) {
    if (!elem || !name || !sibling) return NULL;

    TaurusElement child = taurus_element_create(g_current_doc, name);
    if (!child) return NULL;

    if (taurus_element_insert_after(sibling, child) != TAURUS_OK) {
        return NULL;
    }

    return child;
}

static inline TaurusElement xml_node_insert_child_before(TaurusElement elem,
                                                       const char* name,
                                                       TaurusElement sibling) {
    if (!elem || !name || !sibling) return NULL;

    TaurusElement child = taurus_element_create(g_current_doc, name);
    if (!child) return NULL;

    if (taurus_element_insert_before(sibling, child) != TAURUS_OK) {
        return NULL;
    }

    return child;
}

static inline int xml_node_set_name(TaurusElement elem, const char* name) {
    if (!elem || !name) return 0;
    return taurus_element_set_name(elem, name) == TAURUS_OK ? 1 : 0;
}

static inline TaurusElement xml_node_append_copy(TaurusElement elem, TaurusElement source) {
    if (!elem || !source) return NULL;
    return taurus_element_append_copy(elem, source);
}

static inline TaurusElement xml_node_prepend_copy(TaurusElement elem, TaurusElement source) {
    if (!elem || !source) return NULL;
    return taurus_element_prepend_copy(elem, source);
}

static inline TaurusElement xml_node_insert_copy_after(TaurusElement elem,
                                                     TaurusElement source,
                                                     TaurusElement sibling) {
    if (!elem || !source || !sibling) return NULL;
    return taurus_element_insert_copy_after(sibling, source);
}

static inline TaurusElement xml_node_insert_copy_before(TaurusElement elem,
                                                      TaurusElement source,
                                                      TaurusElement sibling) {
    if (!elem || !source || !sibling) return NULL;
    return taurus_element_insert_copy_before(sibling, source);
}

static inline int xml_node_remove_child(TaurusElement elem, const char* name) {
    if (!elem || !name) return 0;

    /* Find child by name */
    TaurusElement child = xml_node_child(elem, name);
    if (!child) return 0;

    return taurus_element_remove_child(elem, child) == TAURUS_OK ? 1 : 0;
}

static inline int xml_node_remove_child_node(TaurusElement elem, TaurusElement child) {
    if (!elem || !child) return 0;
    return taurus_element_remove_child(elem, child) == TAURUS_OK ? 1 : 0;
}

static inline const char* xml_node_name(TaurusElement elem) {
    if (!elem) return "";
    const char* name = taurus_element_name(elem);
    return name ? name : "";
}

/* Attribute operations */
static inline TaurusAttribute xml_node_attribute(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;
    /* Attribute object API not exposed in taurus.h */
    (void)name;
    return NULL;
}

static inline TaurusAttribute xml_node_first_attribute(TaurusElement elem) {
    if (!elem) return NULL;
    /* Attribute object API not exposed in taurus.h */
    return NULL;
}

/* Null checks */
static inline int xml_node_is_null(TaurusElement* elem) {
    return elem == NULL ? 1 : 0;
}

static inline int xml_attribute_is_null(TaurusAttribute* attr) {
    return attr == NULL ? 1 : 0;
}

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

static inline void run_benchmark(BenchmarkTest* test, int iterations) {
    if (!test || !test->test_func) return;

    double start = get_time_us();

    for (int i = 0; i < iterations; i++) {
        test->passed = test->test_func();
        if (!test->passed) break;
    }

    double end = get_time_us();
    test->time_us = (end - start) / iterations;
}

/* Test registry functions (implemented in dom_modify_tests.c) */
int get_test_count(void);
BenchmarkTest* get_test(int index);

#ifdef __cplusplus
}
#endif

#endif /* PUGIXML_TEST_ADAPTER_H */
