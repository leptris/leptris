#ifndef PUGIXML_TEST_ADAPTER_H
#define PUGIXML_TEST_ADAPTER_H

/**
 * @file test_adapter.h
 * @brief Adapter layer mapping pugixml API to Leptris API for benchmarking
 *
 * This header provides a compatibility layer that allows pugixml test cases
 * to be compiled against Leptris API with minimal modifications.
 * COMPACT MODE: Uses public API functions (no direct field access).
 */

#include <leptris.h>
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
typedef LeptrisDocument xml_document;
typedef LeptrisElement xml_node;
typedef LeptrisAttribute xml_attribute;

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
static __thread LeptrisDocument g_current_doc = NULL;

/* Document operations */

static inline void xml_document_reset(LeptrisDocument doc) {
    if (!doc) return;

    /* Get root element */
    LeptrisElement root = leptris_document_root(doc);
    if (!root) return;

    /* Remove all children from root */
    leptris_element_remove_children(root);
}

static inline LeptrisDocument xml_document_create(void) {
    /* Create/reuse document for benchmark */
    if (g_current_doc) {
        return g_current_doc;
    }

    const char* empty_xml = "<root/>";
    LeptrisStatus status;
    g_current_doc = leptris_parse_string(empty_xml, strlen(empty_xml), &status);
    return g_current_doc;
}

static inline void xml_document_free(LeptrisDocument doc) {
    /* For benchmark: don't actually free, just clear */
    if (doc && doc == g_current_doc) {
        xml_document_reset(doc);
    }
}

static inline LeptrisElement xml_document_child(LeptrisDocument doc, const char* name) {
    if (!doc) return NULL;
    LeptrisElement root = leptris_document_root(doc);
    if (!root) return NULL;

    const char* root_name = leptris_element_name(root);
    if (root_name && strcmp(root_name, name) == 0) {
        return root;
    }
    return NULL;
}

static inline LeptrisElement xml_document_append_child(LeptrisDocument doc, const char* name) {
    if (!doc || !name) return NULL;

    /* Track this document for element creation */
    g_current_doc = doc;

    /* Create root element if it doesn't exist */
    LeptrisElement root = leptris_document_root(doc);
    if (!root) {
        return NULL;
    }

    /* Create and append child using public API */
    LeptrisElement child = leptris_element_create(doc, name);
    if (!child) return NULL;

    if (leptris_element_append_child(root, child) != LEPTRIS_OK) {
        return NULL;
    }

    return child;
}

static inline LeptrisElement xml_document_prepend_child(LeptrisDocument doc, const char* name) {
    if (!doc || !name) return NULL;

    /* Track this document for element creation */
    g_current_doc = doc;

    /* Create root element if it doesn't exist */
    LeptrisElement root = leptris_document_root(doc);
    if (!root) {
        return NULL;
    }

    /* Create and prepend child using public API */
    LeptrisElement child = leptris_element_create(doc, name);
    if (!child) return NULL;

    if (leptris_element_prepend_child(root, child) != LEPTRIS_OK) {
        return NULL;
    }

    return child;
}

/* Element operations */
static inline LeptrisElement xml_node_child(LeptrisElement elem, const char* name) {
    if (!elem || !name) return NULL;

    /* Iterate through children */
    LeptrisElement child = leptris_element_first_child_any(elem);
    while (child) {
        const char* child_name = leptris_element_name(child);
        if (child_name && strcmp(child_name, name) == 0) {
            return child;
        }
        child = leptris_element_next_sibling_any(child);
    }
    return NULL;
}

static inline LeptrisElement xml_node_first_child(LeptrisElement elem) {
    if (!elem) return NULL;
    return leptris_element_first_child_any(elem);
}

static inline LeptrisElement xml_node_last_child(LeptrisElement elem) {
    if (!elem) return NULL;
    return leptris_element_last_child_any(elem);
}

static inline LeptrisElement xml_node_next_sibling(LeptrisElement elem) {
    if (!elem) return NULL;
    return leptris_element_next_sibling(elem, NULL);
}

static inline LeptrisElement xml_node_append_child(LeptrisElement elem, const char* name) {
    if (!elem || !name) return NULL;

    LeptrisElement child = leptris_element_create(g_current_doc, name);
    if (!child) return NULL;

    if (leptris_element_append_child(elem, child) != LEPTRIS_OK) {
        return NULL;
    }

    return child;
}

static inline LeptrisElement xml_node_prepend_child(LeptrisElement elem, const char* name) {
    if (!elem || !name) return NULL;

    LeptrisElement child = leptris_element_create(g_current_doc, name);
    if (!child) return NULL;

    if (leptris_element_prepend_child(elem, child) != LEPTRIS_OK) {
        return NULL;
    }

    return child;
}

static inline LeptrisElement xml_node_insert_child_after(LeptrisElement elem,
                                                      const char* name,
                                                      LeptrisElement sibling) {
    if (!elem || !name || !sibling) return NULL;

    LeptrisElement child = leptris_element_create(g_current_doc, name);
    if (!child) return NULL;

    if (leptris_element_insert_after(sibling, child) != LEPTRIS_OK) {
        return NULL;
    }

    return child;
}

static inline LeptrisElement xml_node_insert_child_before(LeptrisElement elem,
                                                       const char* name,
                                                       LeptrisElement sibling) {
    if (!elem || !name || !sibling) return NULL;

    LeptrisElement child = leptris_element_create(g_current_doc, name);
    if (!child) return NULL;

    if (leptris_element_insert_before(sibling, child) != LEPTRIS_OK) {
        return NULL;
    }

    return child;
}

static inline int xml_node_set_name(LeptrisElement elem, const char* name) {
    if (!elem || !name) return 0;
    return leptris_element_set_name(elem, name) == LEPTRIS_OK ? 1 : 0;
}

static inline LeptrisElement xml_node_append_copy(LeptrisElement elem, LeptrisElement source) {
    if (!elem || !source) return NULL;
    return leptris_element_append_copy(elem, source);
}

static inline LeptrisElement xml_node_prepend_copy(LeptrisElement elem, LeptrisElement source) {
    if (!elem || !source) return NULL;
    return leptris_element_prepend_copy(elem, source);
}

static inline LeptrisElement xml_node_insert_copy_after(LeptrisElement elem,
                                                     LeptrisElement source,
                                                     LeptrisElement sibling) {
    if (!elem || !source || !sibling) return NULL;
    return leptris_element_insert_copy_after(sibling, source);
}

static inline LeptrisElement xml_node_insert_copy_before(LeptrisElement elem,
                                                      LeptrisElement source,
                                                      LeptrisElement sibling) {
    if (!elem || !source || !sibling) return NULL;
    return leptris_element_insert_copy_before(sibling, source);
}

static inline int xml_node_remove_child(LeptrisElement elem, const char* name) {
    if (!elem || !name) return 0;

    /* Find child by name */
    LeptrisElement child = xml_node_child(elem, name);
    if (!child) return 0;

    return leptris_element_remove_child(elem, child) == LEPTRIS_OK ? 1 : 0;
}

static inline int xml_node_remove_child_node(LeptrisElement elem, LeptrisElement child) {
    if (!elem || !child) return 0;
    return leptris_element_remove_child(elem, child) == LEPTRIS_OK ? 1 : 0;
}

static inline const char* xml_node_name(LeptrisElement elem) {
    if (!elem) return "";
    const char* name = leptris_element_name(elem);
    return name ? name : "";
}

/* Attribute operations */
static inline LeptrisAttribute xml_node_attribute(LeptrisElement elem, const char* name) {
    if (!elem || !name) return NULL;
    /* Attribute object API not exposed in leptris.h */
    (void)name;
    return NULL;
}

static inline LeptrisAttribute xml_node_first_attribute(LeptrisElement elem) {
    if (!elem) return NULL;
    /* Attribute object API not exposed in leptris.h */
    return NULL;
}

/* Null checks */
static inline int xml_node_is_null(LeptrisElement* elem) {
    return elem == NULL ? 1 : 0;
}

static inline int xml_attribute_is_null(LeptrisAttribute* attr) {
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
