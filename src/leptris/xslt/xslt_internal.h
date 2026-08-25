/* xslt/xslt_internal.h — XSLT 1.0 engine internals (TODO.transform).
 *
 * ARCHITECTURE (the board's design contract):
 *   COMPILE ONCE, EXECUTE MANY — stylesheets parse to an instruction
 *   forest; every select/test/name expression is compiled once via
 *   leptris_xpath_compile (the pinned-AST cache). Instruction
 *   execution dispatches through a function-pointer table (the
 *   node_vtable pattern): adding an instruction = writing a handler
 *   and registering it — no engine edits (OCP).
 *
 *   The XPath layer is SSOT: patterns and selects are XPath ASTs;
 *   the pattern matcher (xslt_pattern.c) is the only XSLT-specific
 *   semantics over them (ancestor-chain matching, §5.2).
 *
 *   Output is a DOM built through the public mutation API into a
 *   result document and serialized by the shared serializer (DRY).
 *   Result-tree-fragments are scratch documents whose TOP-LEVEL
 *   nodes are exposed as a nodeset (no fake document node needed). */
#ifndef LEPTRIS_XSLT_INTERNAL_H
#define LEPTRIS_XSLT_INTERNAL_H

#include "../leptris_internal.h"
#include "../xpath/xpath_internal.h"
#include "../xpath/parser.h"
#include "../xpath/evaluator_internal.h"
#include "../xpath/functions.h"
#include "../xpath/xpath_variables.h"
#include "../dom/element.h"
#include "../../include/leptris.h"
#include <string.h>

/* ---- Instruction kinds (extend by appending — never renumber) ---- */
typedef enum {
    XSLT_INSTR_RESULT_ELEM = 0,   /* literal result element */
    XSLT_INSTR_TEXT,              /* literal text / xsl:text */
    XSLT_INSTR_VALUE_OF,          /* xsl:value-of select */
    XSLT_INSTR_FOR_EACH,          /* xsl:for-each + sorts */
    XSLT_INSTR_IF,
    XSLT_INSTR_CHOOSE,            /* children: WHEN (test) ... ELSE */
    XSLT_INSTR_WHEN,
    XSLT_INSTR_OTHERWISE,
    XSLT_INSTR_VARIABLE,          /* declare + body */
    XSLT_INSTR_WITH_PARAM,        /* captured on call/apply */
    XSLT_INSTR_CALL_TEMPLATE,
    XSLT_INSTR_APPLY_TEMPLATES,
    XSLT_INSTR_COPY_OF,
    XSLT_INSTR_COPY,
    XSLT_INSTR_ELEMENT,
    XSLT_INSTR_ATTRIBUTE,
    XSLT_INSTR_COMMENT,
    XSLT_INSTR_PI,
    XSLT_INSTR_MESSAGE,
    XSLT_INSTR_NUMBER,
    XSLT_INSTR_ATTR_SET_REF,      /* use-attribute-sets expansion */
    XSLT_INSTR_APPLY_IMPORTS,     /* xsl:apply-imports (§5.6) */
    XSLT_INSTR_UNKNOWN_XSL        /* forwards-compat container: executes
                                      its xsl:fallback children (§15) */
} XsltInstrKind;

struct xslt_styles;  /* fwd */

/* One sort key (xsl:sort child of for-each/apply-templates). */
typedef struct xslt_sort {
    LeptrisXPathCompiled select;   /* NULL → string-value of node */
    int numeric;                    /* data-type="number" */
    int descending;
    int case_upper_first;           /* case-order="upper-first" (default);
                                       -1 = not given, 0 = lower-first */
    struct xslt_sort* next;
} XsltSort;

/* One compiled instruction. POD tree: siblings via next, children
 * via child. Payload union-ish fields by kind (kept flat + explicit
 * rather than a union so the tree stays trivially walkable). */
typedef struct xslt_instr {
    XsltInstrKind kind;
    struct xslt_instr* next;        /* sibling */
    struct xslt_instr* child;       /* first child (body / content) */

    /* Expressions (compiled once). */
    LeptrisXPathCompiled test;     /* IF / WHEN */
    LeptrisXPathCompiled select;   /* VALUE_OF / FOR_EACH / COPY_OF /
                                       VARIABLE / WITH_PARAM */

    /* Literal result element attributes (name, raw value with
     * possible {expr} templates) — evaluated at execution. */
    struct xslt_lattr* attrs;

    /* Literal data. */
    const char* name;               /* RESULT_ELEM qname / ELEMENT
                                       value / VARIABLE / PARAM /
                                       WITH_PARAM / CALL_TEMPLATE /
                                       PI target / mode name */
    const char* ns_uri;             /* RESULT_ELEM / ELEMENT namespace */
    const char* text;               /* TEXT content (owned copy) */

    /* Numeric-control (NUMBER) */
    int num_level;                  /* 0 single, 1 multiple, 2 any */
    LeptrisXPathCompiled num_value;
    LeptrisXPathCompiled num_count;
    LeptrisXPathCompiled num_from;
    const char* num_format;         /* "1", "a", "A", "i", "I" prefix */
    int num_group_size;
    char num_group_sep;

    /* Sorts (FOR_EACH / APPLY_TEMPLATES). */
    XsltSort* sorts;

    /* use-attribute-sets (RESULT_ELEM / ELEMENT / COPY): names
     * applied before the instruction's own attrs (§7.1.4). */
    char** attr_set_names;
    size_t attr_set_count;

    /* §4/§5.3: in-scope namespace bindings of the stylesheet
     * element carrying this instruction — prefixed name tests in
     * its expressions resolve through here. NULL when the sheet
     * declares no namespaces (the common case; hot path skips). */
    LeptrisXPathNsSet ns;

    /* §7.1.1 LRE namespace copy: in-scope bindings (minus XSLT)
     * re-declared on the result element; default ns separate. */
    char** ns_out_pfx;
    char** ns_out_uri;
    size_t ns_out_count;
    char* ns_out_default;

    /* CHOOSE arms are WHEN/OTHERWISE children with test set. */
    int terminate;                  /* MESSAGE terminate */
    int is_param;                   /* VARIABLE came from xsl:param —
                                       template-parameter semantics
                                       (§11.6): default applies only when
                                       no with-param binds the name. */
    int doe;                        /* TEXT / VALUE_OF
                                       disable-output-escaping="yes" */
    const char* letter_value;      /* NUMBER: "alphabetic"|"traditional"
                                       (§7.7 disambiguator, stored raw) */
} XsltInstr;

/* One literal result-element attribute (name, raw value with
 * possible {expr} templates — evaluated at execution). Shared by
 * literal elements and xsl:attribute-set entries. */
typedef struct xslt_lattr {
    const char* name;
    const char* value;      /* raw, may contain {xpath} templates */
    struct xslt_lattr* next;
} XsltLAttr;

/* Named attribute set (xsl:attribute-set, §7.1.4). */
typedef struct xslt_attrset {
    const char* name;
    XsltLAttr* attrs;           /* evaluated AVTs applied at use sites */
    struct xslt_attrset* next;
} XsltAttrSet;

/* Decimal format (xsl:decimal-format, §12.3). The sheet always has
 * an unnamed default entry first. */
typedef struct xslt_decformat {
    const char* name;           /* NULL = default */
    char decimal_sep;
    char grouping_sep;
    char minus_sign;
    char percent;
    char per_mille;             /* stored as bytes; multi-byte chars v1 truncated */
    char zero_digit;
    const char* infinity;
    const char* nan;
    struct xslt_decformat* next;
} XsltDecimalFormat;

/* One match pattern alternative (a pattern "a|b|c" is three alts). */
typedef struct xslt_pattern {
    LeptrisXPathCompiled expr;     /* full alternative expression */
    double priority;                /* computed default (§5.5) */
    /* Root-element fast path: set when the alternative is a single
     * bare name test or "*" (see xslt_pattern.c). */
    char expr_name[64];
    int expr_name_only;
    struct xslt_pattern* next;
} XsltPattern;

typedef struct xslt_template {
    XsltPattern* matches;           /* NULL for named-only */
    const char* name;               /* NULL for match-only */
    const char* mode;               /* NULL = default mode */
    XsltInstr* body;
    int import_rank;                /* 0 = this sheet, 1+ = imported */
    struct xslt_template* next;     /* sheet order (imports first) */
} XsltTemplate;

/* Key definition (xsl:key). */
typedef struct xslt_keydef {
    const char* name;
    LeptrisXPathCompiled match;
    LeptrisXPathCompiled use;
    struct xslt_keydef* next;
} XsltKeyDef;

/* Namespace alias (§7.1.1 xsl:namespace-alias): stylesheet-prefix
 * → result-prefix; "#default" maps to/from the default namespace. */
typedef struct xslt_ns_alias {
    const char* stylesheet_prefix;   /* NULL = "#default" */
    const char* result_prefix;       /* NULL = "#default" */
    struct xslt_ns_alias* next;
} XsltNsAlias;

struct xslt_styles {
    /* Templates in source order; selection sorts by (import_rank,
     * priority, order) at apply time per §5.4/5.5. */
    XsltTemplate* templates;
    size_t template_count;

    /* Named template lookup: linear over the same array (stylesheets
     * rarely have many named templates; revisit at scale). */
    XsltKeyDef* keys;

    /* Global variables (executed once per transform, before the
     * body: XSLT §11.4 top-level xsl:variable). */
    XsltInstr* globals;

    /* Output settings (xsl:output; defaults method=xml). */
    int out_method_text;
    int out_method_html;   /* §16.2 */
    int out_indent;
    int out_omit_decl;
    const char* out_encoding;
    const char* out_version;
    const char* out_doctype_system;
    const char* out_doctype_public;
    char** out_cdata_elems;        /* NULL-terminated name list */

    /* xsl:decimal-format declarations; the head is the default. */
    XsltDecimalFormat* decformats;

    /* Named attribute sets (§7.1.4). */
    XsltAttrSet* attrsets;

    /* §3.4 whitespace handling on the SOURCE tree: preserve list
     * from xsl:preserve-space, strip list from xsl:strip-space
     * (strip wins on conflict — last declaration). Default (both
     * NULL): whitespace-only text nodes are stripped per §3.4. */
    char** ws_preserve;
    char** ws_strip;

    /* §7.1.1 xsl:namespace-alias table. */
    XsltNsAlias* ns_alias;

    /* §2.5 forwards-compatible processing (xsl:stylesheet version
     * != 1.0): unknown top-level elements are ignored instead of
     * erroring; unknown instructions fall back per §15. */
    int forwards_compat;

    /* Any non-xsl namespace declared on the stylesheet root — gates
     * per-instruction ns-context building (§4 prefixed tests). */
    int sheet_has_ns;

    /* §16.1 xsl:output standalone (-1 absent, 0 no, 1 yes) and
     * media-type (advisory). */
    int out_standalone;
    const char* out_media_type;
};

typedef struct xslt_styles XsltStylesheet;

/* ---- Execution state ---- */

/* Variable frame chain — innermost shadows outer; materialized
 * newest-first into an XPathVariableSet per evaluation so lookup
 * (first-match scan) sees the innermost binding. */
typedef struct xslt_var {
    const char* name;
    struct leptris_xpath_result* value;   /* owned */
    struct xslt_var* prev;
} XsltVar;

typedef struct xslt_exec {
    const XsltStylesheet* sheet;
    LeptrisDocument sheet_doc;      /* stylesheet document —
                                       document('') (§12.1) resolves
                                       to this tree. */
    LeptrisDocument source;
    LeptrisDocument result;        /* output tree (owned) */
    LeptrisDocument scratch;       /* RTF fragments live here (owned) */
    XsltVar* vars;                 /* frame chain */
    XPathVariableSet* varset;      /* scratch set for evaluation */
    struct leptris_xpath_result* pending; /* with-params of the
                                             in-flight call */
    int terminated;                /* xsl:message terminate */
    char* message;                 /* collected message text */
    LeptrisElement pending_parent;/* current output insertion point */
    char* top_text;                /* text emitted before any element */
    size_t top_text_len, top_text_cap;
    char* tail_text;               /* text emitted AFTER elements (root
                                    * siblings in fragment order) */
    size_t tail_text_len, tail_text_cap;
    char* rtf_text;                /* RTF capture buffer: a variable
                                    * body that produces only text has
                                    * no element to chain nodes from —
                                    * accumulate here instead of the
                                    * fragment buffers. */
    size_t rtf_text_len, rtf_text_cap;
    int rtf_capturing;

    /* current(): the node being processed by the template rule or
     * for-each in flight (§12.4) — distinct from the predicate
     * context, so save/restored around each xslt_eval. */
    LeptrisElement current_node;

    /* The template rule currently executing (§5.6 apply-imports
     * resolves candidates against THIS rule's import rank). */
    const XsltTemplate* current_template;

    /* §4: the in-scope ns bindings of the instruction in flight —
     * installed by the walker, consumed by xslt_eval. */
    LeptrisXPathNsSet current_ns;

    /* Frame-chain depth snapshot management for block scope: the
     * instruction walker saves/restores depth per sequence. */
    size_t var_depth;

    /* xslt_functions.c state (opaque here): the per-exec registry
     * carrying the XSLT function bridge, the lazy key indexes
     * (§12.2), and the document() cache. */
    void* bridge;
    void* keys;
    void* docs;

    /* RTF ownership chain: result-tree-fragment documents built by
     * <xsl:variable> bodies whose lifetime must outlive the
     * nodeset that references their nodes (§11.4 — the spec calls
     * these RTFs; their lifetime is the binding's). Nodesets
     * returned from op_variable carry heap-owned nodeset arrays
     * pointing into one of these documents. Freed by
     * xslt_exec_free. */
    void* rtf_chain;
} XsltExec;

/* xslt_exec.c — public transform entry. */
XsltExec* xslt_transform_doc(const XsltStylesheet* sheet,
                             LeptrisDocument sheet_doc,
                             LeptrisDocument source);
#define xslt_transform(sheet, source) \
    xslt_transform_doc((sheet), NULL, (source))
void xslt_exec_free(XsltExec* ex);

/* xslt_parse.c — compilation. */
XsltStylesheet* xslt_stylesheet_parse(LeptrisDocument doc);
XsltStylesheet* xslt_stylesheet_parse_root(LeptrisDocument doc,
                                           LeptrisElement root);
void xslt_stylesheet_free(XsltStylesheet* sheet);

/* xslt_pattern.c — pattern matching (§5.2). */
int xslt_pattern_matches(const XsltPattern* p, LeptrisElement node,
                         LeptrisDocument doc);

/* xslt_exec.c — pattern + execution helpers shared with
 * xslt_functions.c (the document-order walker reused for key-index
 * construction and the nodeset result identifier). */
LeptrisElement xslt_next_doc_order(LeptrisElement e);

/* xslt_exec.c — instruction dispatch (registration table). */
typedef int (*XsltInstrFn)(XsltExec* ex, const XsltInstr* in,
                           LeptrisElement node);
void xslt_register_op(XsltInstrKind kind, XsltInstrFn fn);
int xslt_exec_instrs(XsltExec* ex, const XsltInstr* list,
                     LeptrisElement node);

/* Shared helpers (xslt_exec.c). */
struct leptris_xpath_result* xslt_eval(XsltExec* ex,
                                       LeptrisXPathCompiled c,
                                       LeptrisElement node);
void xslt_push_var(XsltExec* ex, const char* name,
                   struct leptris_xpath_result* v);
void xslt_pop_var(XsltExec* ex, const char* name);
struct leptris_xpath_result* xslt_copy_result(
    const struct leptris_xpath_result* r);

/* xslt_functions.c — the context-function bridge (phases 04/05):
 * XSLT functions (current, key, format-number, generate-id,
 * system-property, document) + EXSLT node-set/regexp/date, bound to
 * a per-exec registry so handlers reach this exec without touching
 * the (shared, read-only) source document. Built lazily; freed at
 * exec teardown. */
void xslt_register_bridge_handlers(XPathFunctionRegistry* r, void* exec);
XPathFunctionRegistry* xslt_bridge_registry(XsltExec* ex);
void xslt_bridge_free(XsltExec* ex);
void xslt_keys_free(XsltExec* ex);
void xslt_docs_free(XsltExec* ex);

/* format-number(§12.3) — shared by the bridge; returns an OWNED
 * string. df_name NULL/"" selects the default decimal-format. */
char* xslt_format_number(const XsltStylesheet* sheet, double value,
                         const char* pattern, const char* df_name);

/* Attribute-set application (xslt_exec.c): evaluates the set's AVT
 * attrs onto `target`; existing attributes are NOT overwritten
 * (§7.1.4 — later sets and explicit attrs win). */
void xslt_apply_attr_sets(XsltExec* ex, const XsltInstr* in,
                          LeptrisElement target, LeptrisElement node);

#endif /* LEPTRIS_XSLT_INTERNAL_H */
