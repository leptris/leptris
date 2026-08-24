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
    XSLT_INSTR_ATTR_SET_REF       /* use-attribute-sets expansion */
} XsltInstrKind;

struct xslt_styles;  /* fwd */

/* One sort key (xsl:sort child of for-each/apply-templates). */
typedef struct xslt_sort {
    LeptrisXPathCompiled select;   /* NULL → string-value of node */
    int numeric;                    /* data-type="number" */
    int descending;
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

    /* CHOOSE arms are WHEN/OTHERWISE children with test set. */
    int terminate;                  /* MESSAGE terminate */
} XsltInstr;

typedef struct xslt_lattr {
    const char* name;
    const char* value;      /* raw, may contain {xpath} templates */
    struct xslt_lattr* next;
} XsltLAttr;

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
    int out_indent;
    int out_omit_decl;
    const char* out_encoding;
    const char* out_version;
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
    char* top_text;                /* text emitted with no parent yet */
    size_t top_text_len, top_text_cap;
} XsltExec;

/* xslt_exec.c — public transform entry. */
XsltExec* xslt_transform(const XsltStylesheet* sheet,
                         LeptrisDocument source);
void xslt_exec_free(XsltExec* ex);

/* xslt_parse.c — compilation. */
XsltStylesheet* xslt_stylesheet_parse(LeptrisDocument doc);
void xslt_stylesheet_free(XsltStylesheet* sheet);

/* xslt_pattern.c — pattern matching (§5.2). */
int xslt_pattern_matches(const XsltPattern* p, LeptrisElement node,
                         LeptrisDocument doc);

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

#endif /* LEPTRIS_XSLT_INTERNAL_H */
