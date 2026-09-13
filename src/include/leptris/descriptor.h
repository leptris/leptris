/**
 * @file descriptor.h
 * @brief Tree-shaped schema-descriptor materialization (issue #1039)
 *
 * Hosts (lutaml-model via the bindings) compile a tree of plans once
 * and hand it to the engine; leptris_plan_walk materializes a whole
 * subtree against that descriptor in one native pass — no per-element
 * host calls. XML is not a hash tree: this descriptor models the two
 * value channels (attributes + elements), namespace forms, document
 * ordering, and mixed content explicitly.
 *
 * ABI discipline: this header is the contract. Enum values only ever
 * APPEND; struct fields never renumber; breaking changes bump
 * leptris_plan_abi_version() and hosts refuse mismatched pools at
 * load. The spec structs are POD — the tree lives in caller-owned
 * arrays referenced by index.
 */

#ifndef LEPTRIS_DESCRIPTOR_H
#define LEPTRIS_DESCRIPTOR_H

#include <stddef.h>
#include <stdint.h>

#include "leptris/types.h"

/* Export macro — mirrors leptris.h (which defines it before including
 * anything); guarded so this header also parses standalone. */
#ifndef LEPTRIS_DESCRIPTOR_API_DEFINED
#define LEPTRIS_DESCRIPTOR_API_DEFINED
#ifdef LEPTRIS_FOR_BINDGEN
#define LEPTRIS_DESCRIPTOR_API
#else
#ifndef LEPTRIS_API
#  ifdef _WIN32
#    ifdef LEPTRIS_BUILD_SHARED
#      define LEPTRIS_API __declspec(dllexport)
#    elif defined(LEPTRIS_BUILDING_DLL)
#      define LEPTRIS_API __declspec(dllexport)
#    elif defined(LEPTRIS_USE_SHARED)
#      define LEPTRIS_API __declspec(dllimport)
#    else
#      define LEPTRIS_API
#    endif
#  else
#    define LEPTRIS_API __attribute__((visibility("default")))
#  endif
#endif
#define LEPTRIS_DESCRIPTOR_API LEPTRIS_API
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LEPTRIS_PLAN_ABI_VERSION 1u

/* Child / attribute plan kinds. Append-only. */
typedef enum {
    LEPTRIS_PLAN_KIND_SCALAR = 1,     /* text content / attribute value */
    LEPTRIS_PLAN_KIND_COLLECTION = 2, /* repeated matches -> list */
    LEPTRIS_PLAN_KIND_NESTED = 3,     /* recurse into plans[child_plan_index] */
    LEPTRIS_PLAN_KIND_RAW = 4,        /* verbatim serialized subtree */
    LEPTRIS_PLAN_KIND_CONTENT = 5,    /* mixed-content text runs, doc order */
    LEPTRIS_PLAN_KIND_CALLBACK = 6    /* escape hatch: raw value + position */
} LeptrisPlanKind;

/* Element plan flags. */
#define LEPTRIS_PLAN_FLAG_MIXED_CONTENT 0x1u /* text runs preserved via CONTENT rows */
#define LEPTRIS_PLAN_FLAG_ORDERED 0x2u       /* matches within a row keep document order (default) */
#define LEPTRIS_PLAN_FLAG_CDATA 0x4u         /* CDATA sections count as text runs */
#define LEPTRIS_PLAN_FLAG_NS_LENIENT 0x8u    /* #754: bind out-of-namespace children anyway */

/* Namespace matching form for binding an element. */
typedef enum {
    LEPTRIS_PLAN_NS_NONE = 0,  /* only elements with no namespace */
    LEPTRIS_PLAN_NS_EXACT = 1, /* resolved namespace URI equals ns_uri */
    LEPTRIS_PLAN_NS_ANY = 2
} LeptrisPlanNsForm;

typedef struct {
    const char* wire_name; /* XML attribute name as it appears on the wire */
    uint8_t kind;          /* LeptrisPlanKind: SCALAR | COLLECTION | CALLBACK */
    uint8_t type_tag;      /* host-defined; echoed back verbatim */
} leptris_attr_plan;

typedef struct {
    const char* wire_name; /* XML element name as it appears on the wire */
    uint8_t kind;          /* LeptrisPlanKind */
    uint8_t type_tag;      /* host-defined; echoed back verbatim */
    int32_t child_plan_index; /* NESTED: index into leptris_plan_spec.plans;
                               * -1 for non-nested kinds */
} leptris_child_plan;

typedef struct {
    const char* element_name; /* local name this plan binds; binding uses
                               * the CHILD row's wire_name — this field
                               * documents/validates the same name */
    uint8_t ns_form;          /* LeptrisPlanNsForm */
    uint8_t pad0;
    const char* ns_uri;       /* LEPTRIS_PLAN_NS_EXACT only */
    uint32_t attribute_count;
    uint32_t child_count;
    const leptris_attr_plan* attribute_plans;
    const leptris_child_plan* child_plans;
    uint16_t flags; /* LEPTRIS_PLAN_FLAG_* */
    uint16_t pad1;
} leptris_element_plan;

typedef struct {
    uint32_t abi_version; /* must equal leptris_plan_abi_version() */
    uint32_t plan_count;  /* plans[0] is the root plan applied to ctx */
    const leptris_element_plan* plans;
} leptris_plan_spec;

/* Opaque handles: the compiled descriptor and a whole-subtree result. */
typedef struct leptris_plan* LeptrisPlan;
typedef struct leptris_plan_result* LeptrisPlanResult;

/* Result node kinds (the value tree the walk returns). */
typedef enum {
    LEPTRIS_PLAN_VALUE_ELEMENT = 0,     /* element binding: attrs + child values */
    LEPTRIS_PLAN_VALUE_SCALAR = 1,      /* string value */
    LEPTRIS_PLAN_VALUE_COLLECTION = 2,  /* ordered list of values */
    LEPTRIS_PLAN_VALUE_RAW = 3,         /* serialized subtree string */
    LEPTRIS_PLAN_VALUE_CALLBACK = 4     /* raw value + byte position + echo */
} LeptrisPlanValueKind;

/* Descriptor ABI version — versioned lockstep with the structs above. */
LEPTRIS_DESCRIPTOR_API uint32_t leptris_plan_abi_version(void);

/* Compile a descriptor: deep-copies every plan, string, and array into
 * an engine-owned pool — the spec (and its strings) may be freed by the
 * caller immediately after a successful build.
 * Memory: the plan owns everything; release with leptris_plan_free. */
LEPTRIS_DESCRIPTOR_API LeptrisPlan leptris_plan_build(const leptris_plan_spec* spec,
                                           LeptrisStatus* status);
LEPTRIS_DESCRIPTOR_API void leptris_plan_free(LeptrisPlan plan);

/* Walk the subtree rooted at ctx against plans[0] (the root plan
 * applies to ctx itself: attribute rows read ctx's attributes, child
 * rows bind ctx's children). One native pass; no host callbacks.
 * Children not described by the plan are skipped. Child values are
 * emitted in PLAN-ROW order; matches within a row keep document
 * order. Non-NESTED rows bind only no-namespace elements; NESTED
 * rows use the target plan's ns_form; NS_LENIENT on the PARENT plan
 * relaxes both to any namespace (#754 adoption semantics).
 * Memory: whole-subtree result owned by the caller; free with
 * leptris_plan_result_free. The result is standalone (strings copied)
 * and outlives the document. */
LEPTRIS_DESCRIPTOR_API LeptrisPlanResult leptris_plan_walk(LeptrisDocument doc,
                                                LeptrisElement ctx,
                                                LeptrisPlan plan,
                                                LeptrisStatus* status);
LEPTRIS_DESCRIPTOR_API void leptris_plan_result_free(LeptrisPlanResult result);

/* ---- Result accessors (NULL-tolerant: NULL in, 0/NULL out) ------- */

LEPTRIS_DESCRIPTOR_API LeptrisPlanValueKind leptris_plan_value_kind(const LeptrisPlanResult v);
/* wire_name of the plan row that produced this value. */
LEPTRIS_DESCRIPTOR_API const char* leptris_plan_value_name(const LeptrisPlanResult v);
/* Host type_tag echoed verbatim (0 when the row had none). */
LEPTRIS_DESCRIPTOR_API uint8_t leptris_plan_value_type_tag(const LeptrisPlanResult v);
/* SCALAR / RAW / CALLBACK string value. NUL-terminated, result-owned;
 * length excludes the terminator. */
LEPTRIS_DESCRIPTOR_API const char* leptris_plan_value_string(const LeptrisPlanResult v);
LEPTRIS_DESCRIPTOR_API size_t leptris_plan_value_length(const LeptrisPlanResult v);
/* CALLBACK: document byte offset of the source node (0 unknown). */
LEPTRIS_DESCRIPTOR_API size_t leptris_plan_value_position(const LeptrisPlanResult v);
/* ELEMENT: child-value count. COLLECTION: item count. */
LEPTRIS_DESCRIPTOR_API size_t leptris_plan_value_count(const LeptrisPlanResult v);
/* ELEMENT child value / COLLECTION item at i; NULL out of range. */
LEPTRIS_DESCRIPTOR_API LeptrisPlanResult leptris_plan_value_at(const LeptrisPlanResult v,
                                                    size_t i);
/* ELEMENT: attribute value by wire_name; NULL when absent. */
LEPTRIS_DESCRIPTOR_API const char* leptris_plan_value_attribute(const LeptrisPlanResult v,
                                                     const char* wire_name);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_DESCRIPTOR_H */
