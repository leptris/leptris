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

/* LEPTRIS_API comes from leptris.h (the house export macro — the
 * export-surface gate greps for it). Guarded fallback so this header
 * also parses standalone. */
#ifndef LEPTRIS_API
#ifdef _WIN32
#define LEPTRIS_API
#else
/* Mirrors leptris.h (GCC LTO externalization, #1204). */
#if defined(__GNUC__) && !defined(__clang__)
#define LEPTRIS_API __attribute__((visibility("default"), used, \
                                    externally_visible))
#else
#define LEPTRIS_API __attribute__((visibility("default"), used))
#endif
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
/* #1273: when set, the walk ALSO emits unmatched sibling non-element
 * nodes (text runs / comments / PIs) as SCALAR values with
 * `position` and `node_kind` populated, so ordered/mixed-content
 * hosts can rebuild `element_order` without re-parsing the source.
 * Document-order identity already covers element children; the
 * spine closes the gap for runs between matched elements. */
#define LEPTRIS_PLAN_FLAG_EMIT_ORDER_SPINE 0x10u

/* Namespace matching form for binding an element. */
typedef enum {
    LEPTRIS_PLAN_NS_NONE = 0,  /* only elements with no namespace */
    LEPTRIS_PLAN_NS_EXACT = 1, /* resolved namespace URI equals ns_uri */
    LEPTRIS_PLAN_NS_ANY = 2
} LeptrisPlanNsForm;

typedef struct {
    const char* wire_name; /* XML attribute name as it appears on the wire */
    const char* expected_value; /* string-equal match (non-nil); host-owned */
} leptris_attr_predicate;

typedef struct {
    const char* wire_name; /* XML attribute name as it appears on the wire */
    uint8_t kind;          /* LeptrisPlanKind: SCALAR | COLLECTION | CALLBACK */
    uint8_t type_tag;      /* host-defined; echoed back verbatim */
    /* #1272: filter rows to require every (attr_name, expected_value)
     * pair (AND across pairs). Multiple rows with the same wire_name
     * and different predicates partition the match space
     * (exclusive — first matching row wins per occurrence, no
     * double-capture). Trailing additive field; `predicate_count = 0`
     * preserves historical behavior. Strings deep-copied at build. */
    uint16_t predicate_count;
    uint16_t pad_pred;
    const leptris_attr_predicate* predicates;
} leptris_attr_plan;

typedef struct {
    const char* wire_name; /* XML element name as it appears on the wire:
                            * the prefixed form ("w:b") binds that literal
                            * prefix+local pair; the bare local form ("b")
                            * binds by local name and lets ns_form do the
                            * namespace work */
    uint8_t kind;          /* LeptrisPlanKind */
    uint8_t type_tag;      /* host-defined; echoed back verbatim */
    int32_t child_plan_index; /* NESTED: index into leptris_plan_spec.plans;
                               * -1 for non-nested kinds */
    /* #1115 (additive to the frozen v1 ABI; trailing fields keep
     * existing aggregate initializers valid). Rule-level
     * namespace form: consulted at child-match time so siblings
     * can require different URIs under one parent. NONE (0, the
     * zero value) keeps the historical no-namespace+NS_LENIENT
     * behavior — the fields are consulted only when set. */
    uint8_t ns_form;       /* LeptrisPlanNsForm */
    uint8_t pad0;
    const char* ns_uri;    /* LEPTRIS_PLAN_NS_EXACT only */
    /* #1272: element-side predicates. Same semantics as attr_predicate
     * (AND across pairs, exclusive same-name rows). */
    uint16_t predicate_count;
    uint16_t pad_pred;
    const leptris_attr_predicate* predicates;
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
LEPTRIS_API uint32_t leptris_plan_abi_version(void);

/* Compile a descriptor: deep-copies every plan, string, and array into
 * an engine-owned pool — the spec (and its strings) may be freed by the
 * caller immediately after a successful build.
 * Memory: the plan owns everything; release with leptris_plan_free. */
LEPTRIS_API LeptrisPlan leptris_plan_build(const leptris_plan_spec* spec,
                                           LeptrisStatus* status);
LEPTRIS_API void leptris_plan_free(LeptrisPlan plan);

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
LEPTRIS_API LeptrisPlanResult leptris_plan_walk(LeptrisDocument doc,
                                                LeptrisElement ctx,
                                                LeptrisPlan plan,
                                                LeptrisStatus* status);
LEPTRIS_API void leptris_plan_result_free(LeptrisPlanResult result);

/* ---- Result accessors (NULL-tolerant: NULL in, 0/NULL out) ------- */

LEPTRIS_API LeptrisPlanValueKind leptris_plan_value_kind(const LeptrisPlanResult v);
/* wire_name of the plan row that produced this value. */
LEPTRIS_API const char* leptris_plan_value_name(const LeptrisPlanResult v);
/* Host type_tag echoed verbatim (0 when the row had none). */
LEPTRIS_API uint8_t leptris_plan_value_type_tag(const LeptrisPlanResult v);
/* SCALAR / RAW / CALLBACK string value. NUL-terminated, result-owned;
 * length excludes the terminator. */
LEPTRIS_API const char* leptris_plan_value_string(const LeptrisPlanResult v);
LEPTRIS_API size_t leptris_plan_value_length(const LeptrisPlanResult v);
/* CALLBACK: document byte offset of the source node (0 unknown). */
LEPTRIS_API size_t leptris_plan_value_position(const LeptrisPlanResult v);
/* #1273: LEPTRIS_NODE_TYPE_* of the value's source node, or 0 for
 * ELEMENT/COLLECTION wrappers and synthesized text. */
LEPTRIS_API uint8_t leptris_plan_value_node_kind(const LeptrisPlanResult v);
/* #1273: dense sibling rank inside the producing element (0 if
 * unranked). Useful when byte offsets are 0 (mutated docs) or
 * when hosts sort by document order without byte-offset helpers. */
LEPTRIS_API uint32_t leptris_plan_value_order_index(
    const LeptrisPlanResult v);
/* ELEMENT: child-value count. COLLECTION: item count. */
LEPTRIS_API size_t leptris_plan_value_count(const LeptrisPlanResult v);
/* ELEMENT child value / COLLECTION item at i; NULL out of range. */
LEPTRIS_API LeptrisPlanResult leptris_plan_value_at(const LeptrisPlanResult v,
                                                    size_t i);
/* ELEMENT: attribute value by wire_name; NULL when absent. */
LEPTRIS_API const char* leptris_plan_value_attribute(const LeptrisPlanResult v,
                                                     const char* wire_name);

/* #1269a in-pass type execution (#1269a): for SCALAR/RAW values
 * whose producing row carried type_tag in {1=int, 2=float, 3=bool},
 * the walk parses the string once and stores typed values. The str
 * field is always populated for backward compatibility. Accessors
 * return 0 on success (matches type) and non-zero on parse failure
 * (host may fall back to `string`). NULL in or non-numeric value is
 * a failure. */
LEPTRIS_API int leptris_plan_value_int(const LeptrisPlanResult v,
                                       int64_t* out);
LEPTRIS_API int leptris_plan_value_float(const LeptrisPlanResult v,
                                         double* out);
/* `true` / `1` (case-insensitive) -> 1, `false` / `0` -> 0, anything
 * else leaves *out untouched and returns non-zero. */
LEPTRIS_API int leptris_plan_value_bool(const LeptrisPlanResult v, int* out);

/* #1269b fused parse+walk entry. v1 = parse_string + walk(root) +
 * free(doc) — byte-parity with Descriptor#materialize. Returns the
 * same result type as leptris_plan_walk; status reports
 * LEPTRIS_ERROR_PARSE on syntax error. NULL doc / plan / status
 * handled. The caller still owns plan lifetime. */
LEPTRIS_API LeptrisPlanResult leptris_plan_materialize(
    const char* source, size_t source_len,
    LeptrisPlan plan, LeptrisStatus* status);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_DESCRIPTOR_H */
