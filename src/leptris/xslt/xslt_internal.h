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

#include "../dom/document_node.h"

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
    XSLT_INSTR_SEQUENCE,          /* xsl:sequence / xsl:perform-sort
                                     (select + optional sorts) */
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
    XSLT_INSTR_FUNC_RESULT,      /* EXSLT func:result — yields the
                                     user function's return value */
    XSLT_INSTR_ITERATE,          /* xsl:iterate (3.0 §12.5): sequential
                                     mapping with param chaining */
    XSLT_INSTR_NEXT_ITERATION,   /* xsl:next-iteration: rebind params,
                                     abandon the rest of the body */
    XSLT_INSTR_BREAK,            /* xsl:break: end the enclosing iterate */
    XSLT_INSTR_FOR_EACH_GROUP,   /* xsl:for-each-group (3.0 §14) */
    XSLT_INSTR_EVALUATE,         /* xsl:evaluate (3.0 §26): dynamic
                                     expression from a string */
    XSLT_INSTR_ANALYZE_STRING,   /* xsl:analyze-string (3.0 §18) */
    XSLT_INSTR_MATCHING_SUBSTRING,
    XSLT_INSTR_NONMATCHING_SUBSTRING,
    XSLT_INSTR_TRY,              /* xsl:try (3.0 §17): body until the
                                     first xsl:catch child */
    XSLT_INSTR_CATCH,            /* xsl:catch: runs on a dynamic error
                                     with $err:description bound */
    XSLT_INSTR_ON_NON_EMPTY,     /* 3.0 §26.4: Saxon-HE evaluates the
                                   content unconditionally (verified
                                   12.7 — the spec permits buffering;
                                   parity follows the oracle) */
    XSLT_INSTR_WHERE_POPULATED, /* 3.0 §26.2: drop wholly-empty
                                   content */
    XSLT_INSTR_NEXT_MATCH,       /* 3.0 §6.6: invoke the next-lower
                                    precedence matching rule */
    XSLT_INSTR_FORK,             /* 3.0 §14: sequential arms
                                    (non-streaming) */
    XSLT_INSTR_NAMESPACE,        /* 2.0 §11.7: namespace node */
    XSLT_INSTR_DOCUMENT,         /* 2.0 §11.8: document constructor */
    XSLT_INSTR_ON_COMPLETION,    /* 3.0 §12.5: post-iterate body */
    XSLT_INSTR_MERGE,            /* 3.0 §14.3: merge sources into groups */
    XSLT_INSTR_MERGE_SOURCE,     /* named source; children are merge-keys */
    XSLT_INSTR_MERGE_KEY,        /* key expression + sort order */
    XSLT_INSTR_MERGE_ACTION,     /* body run per merge group */
    XSLT_INSTR_ON_EMPTY,         /* xsl:on-empty (3.0 §26.4): consumed
                                     by the enclosing result element —
                                     content runs only when the
                                     element's content came back empty */
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
    long num_start_at;            /* §12.2 start-at (0 = default 1) */
    char num_group_sep;

    /* Sorts (FOR_EACH / APPLY_TEMPLATES). */
    XsltSort* sorts;

    /* FOR_EACH_GROUP (3.0 §14): group_by is an expression (string key
     * per item; group_adjacent only joins ADJACENT equal keys);
     * group_starting is a single-alternative match pattern (a match
     * OPENS a group); group_ending closes one (trailing non-matches
     * form the last group). At most one grouping control is set. */
    LeptrisXPathCompiled group_by;
    int group_adjacent;
    struct xslt_pattern* group_starting;
    struct xslt_pattern* group_ending;

    /* EVALUATE (3.0 §26): @xpath expression yields the STRING to
     * compile+run; context_item selects its context node (absent →
     * the document node anchors absolute paths). */
    LeptrisXPathCompiled context_item;

    /* ANALYZE_STRING (3.0 §18): regex/regex-flags raw strings (v1:
     * literal, no AVT); the exec regex-scans the selected string and
     * runs MATCHING/NONMATCHING_SUBSTRING children per segment. */
    const char* regex;
    const char* regex_flags;

    /* use-attribute-sets (RESULT_ELEM / ELEMENT / COPY): names
     * applied before the instruction's own attrs (§7.1.4). */
    char** attr_set_names;
    char** attr_set_uris;       /* parallel to attr_set_names */
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
    size_t ns_out_default_pos;  /* index among ns_out entries where
                                   the default was declared ((size_t)-1
                                   = after the last entry) */

    /* CHOOSE arms are WHEN/OTHERWISE children with test set. */
    int terminate;                  /* MESSAGE terminate */
    int tunnel;                     /* with-param/param: tunnel="yes" */
    int is_param;                   /* VARIABLE came from xsl:param —
                                       template-parameter semantics
                                       (§11.6): default applies only when
                                       no with-param binds the name. */

    /* TEXT (3.0 §10.4.2): the text carries {expr} value templates —
     * expand at execution (sheet-level expand-text="yes"). */
    int tvt;
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

/* Named attribute set (xsl:attribute-set, §7.1.4).
 *
 * Sets with the same expanded name UNION (§12.1.4): declarations
 * apply in document order, later ones overriding values in place
 * (positions follow first insertion — libxslt bug-189/217). The
 * attrsets list is prepend-ordered, so the walker reverses it.
 * use-attribute-sets chains expand at USE time (recursively, cycle
 * -guarded), never snapshotted at parse time. Names are QNames:
 * name_uri holds the expanded-name URI for prefixed sets (the
 * declaration and references may spell different prefixes). */
typedef struct xslt_attrset {
    const char* name;
    const char* name_uri;       /* expanded-name URI (prefixed sets) */
    int import_rank;            /* precedence level (lower = wins) */
    XsltLAttr* attrs;           /* evaluated AVTs applied at use sites */
    char** use_names;           /* use-attribute-sets (comma-split) */
    char** use_uris;            /* parallel to use_names (NULL rows
                                   for unprefixed references) */
    size_t use_count;
    struct xslt_attrset* next;
} XsltAttrSet;

/* Decimal format (xsl:decimal-format, §12.3). The sheet always has
 * an unnamed default entry first. */
typedef struct xslt_decformat {
    const char* name;           /* NULL = default */
    const char* uri;            /* resolved namespace (name qname) */
    const char* local;          /* local part of the name qname */
    const char* decimal_sep;      /* full string — any UTF-8 char */
    const char* grouping_sep;
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
    /* Compiled step ladder (pattern-compiler fast path): the
     * alternative is a child-axis name/kind ladder with an
     * optional predicate on the LAST step. Matching then costs
     * one predicate eval plus a parent-chain walk — never the
     * ancestor-rung downward scans of the general ladder. Any
     * other shape (prefixed tests, //, ::, function patterns,
     * non-final predicates) leaves steps_valid = 0 and keeps the
     * general matcher. */
    struct xslt_pat_step* steps;
    int n_steps;
    int steps_valid;
    int steps_absolute;   /* leading '/' — chain anchors at the
                             document node's child axis */
    struct xslt_pattern* next;
} XsltPattern;

/* One step of the compiled pattern ladder. kind: 0 = name test
 * (name NULL = *), 1 = node(), 2 = text(), 3 = comment(),
 * 4 = processing-instruction(). */
typedef struct xslt_pat_step {
    char* name;
    int is_attr;
    int kind;
    LeptrisXPathCompiled pred;   /* optional [pred] on this step */
} XsltPatStep;

typedef struct xslt_template {
    XsltPattern* matches;           /* NULL for named-only */
    const char* name;               /* NULL for match-only */
    const char* mode;               /* NULL = default mode */
    XsltInstr* body;
    int import_rank;                /* 0 = this sheet, 1+ = imported */
    LeptrisXPathNsSet ns;           /* §5.3 in-scope bindings of the
                                       template element — prefixed
                                       name tests in its patterns
                                       resolve through here */
    struct xslt_template* next;     /* sheet order (imports first) */
} XsltTemplate;

/* EXSLT func:function (http://exslt.org/functions): a stylesheet-
 * defined extension function callable from XPath. Registered in the
 * per-eval bridge registry under its raw qname; the body runs with
 * the CALLER's context node and globals-only variable scope;
 * func:result yields the return value. */
typedef struct xslt_userfunc {
    const char* name;               /* raw qname attr (e.g. "myf:dup") */
    XsltInstr* body;
    struct xslt_userfunc* next;
} XsltUserFunc;

/* Key definition (xsl:key). */
typedef struct xslt_keydef {
    const char* name;
    XsltPattern pat;            /* compiled @match (expr + step
                                   ladder) — the index build
                                   matches every document node with
                                   it. */
    LeptrisXPathCompiled use;
    struct xslt_keydef* next;
} XsltKeyDef;

/* xsl:accumulator (3.0 §18.2): a per-tree state machine folded over
 * the document-order event stream (a start and an end event for
 * every non-attribute, non-namespace node). Per event only the LAST
 * rule in declaration order whose @match matches and whose @phase
 * equals the event phase fires; no matching rule leaves the value
 * unchanged. accumulator-before(N) folds through N's own start
 * event, accumulator-after(N) through N's end event. */
typedef struct xslt_acc_rule {
    LeptrisXPathCompiled match;
    LeptrisXPathCompiled select;    /* NULL = empty sequence */
    int phase_end;                  /* 0 = "start" (default), 1 = "end" */
    struct xslt_acc_rule* next;
} XsltAccRule;

typedef struct xslt_accumulator {
    const char* name;
    LeptrisXPathCompiled initial;   /* NULL = empty sequence */
    XsltAccRule* rules;
    struct xslt_accumulator* next;
} XsltAccumulator;

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

    /* xsl:accumulator declarations (3.0 §18.2). */
    XsltAccumulator* accs;

    /* xsl:mode use-accumulators of the UNNAMED mode (3.0 §18.2.2
     * applicability gate): accumulator names applicable to the
     * principal source document, or #all. Empty = none applicable —
     * every accumulator-* call on the source doc is XTDE3362.
     * Named modes are not captured yet (mode dispatch is
     * string-based; on-no-match etc. land with full xsl:mode). */
    char** mode_acc_names;
    size_t mode_acc_count;
    int mode_acc_all;
    /* §6.7 on-no-match of the unnamed mode: 1 unspecified (runtime
     * resolves by version), 2 deep-copy, 3 shallow-copy,
     * 4 shallow-skip, 5 deep-skip, 6 text-only-copy, 7 fail. */
    int mode_on_no_match;

    /* Global variables (executed once per transform, before the
     * body: XSLT §11.4 top-level xsl:variable). */
    XsltInstr* globals;

    /* Output settings (xsl:output; defaults method=xml). */
    int out_method_text;
    int out_method_html;   /* §16.2 */
    int out_method_set;    /* xsl:output named a method — disables
                              the §16.1 html-root sniff */
    int out_indent;        /* -1 unspecified (html default yes,
                              xml no), 0 no, 1 yes */
    char** out_cdata;         /* §16.1 cdata-section-elements QNames */
    size_t out_cdata_count;
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

    /* EXSLT func:function definitions (see XsltUserFunc). */
    XsltUserFunc* funcs;

    /* §3.4 whitespace handling on the SOURCE tree: preserve list
     * from xsl:preserve-space, strip list from xsl:strip-space
     * (strip wins on conflict — last declaration). Default (both
     * NULL): whitespace-only text nodes are stripped per §3.4. */
    char** ws_preserve;
    /* Combined 3.4 rules in DECLARATION order — the LAST matching
     * rule for a name decides (libxslt bug-82). */
    char** ws_rules;
    unsigned char* ws_rule_preserve;   /* 1 = preserve rule */
    size_t ws_rule_count;
    char** ws_strip;

    /* §7.1.1 xsl:namespace-alias table. */
    XsltNsAlias* ns_alias;

    /* §2.5 forwards-compatible processing (xsl:stylesheet version
     * != 1.0): unknown top-level elements are ignored instead of
     * erroring; unknown instructions fall back per §15. */
    int forwards_compat;

    /* xsl:stylesheet/@version major (default 1). 2.0+ changes
     * versioned behaviors — e.g. xsl:value-of selects a SEQUENCE and
     * prints every item, not just the first node. */
    int version_major;

    /* 3.0 §10.4.2 expand-text="yes": text value templates {expr}
     * expand in literal text (xsl:text content included). */
    int expand_text;

    /* Any non-xsl namespace declared on the stylesheet root — gates
     * per-instruction ns-context building (§4 prefixed tests). */
    int sheet_has_ns;

    /* §7.1.1 exclude-result-prefixes (+ extension-element-prefixes,
     * §14.1: extension prefixes never reach the result). */
    char** exclude_pfx;
    size_t exclude_count;

    /* §15 extension-element-prefixes ONLY (elements in these
     * namespaces are unknown extension elements — xsl:fallback
     * semantics). */
    char** ext_pfx;
    size_t ext_count;

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

/* One xsl:merge-source's slice of the current merge group (§14.3):
 * name is borrowed from the merge-source instruction; items are
 * borrowed pointers into op_merge's sorted entry list. */
typedef struct xslt_merge_side {
    const char* name;
    LeptrisNodeRef* items;
    size_t n, cap;
} XsltMergeSide;

typedef struct xslt_exec {
    const XsltStylesheet* sheet;
    LeptrisDocument sheet_doc;      /* stylesheet document —
                                       document('') (§12.1) resolves
                                       to this tree. */
    LeptrisDocument source;
    LeptrisDocument result;        /* output tree (owned) */
    LeptrisDocument scratch;       /* RTF fragments live here (owned) */
    XsltVar* vars;                 /* frame chain */
    XsltVar* global_vars;          /* chain head AFTER globals ran —
                                      xsl:call-template resets the
                                      chain here (§11: callees see
                                      globals + own locals, never the
                                      caller's locals) */
    int vars_dirty;                /* a frame changed since the last
                                      varset materialization — the
                                      next xslt_eval rebuilds the
                                      scratch set */
    XPathVariableSet* varset;      /* scratch set for evaluation */
    struct leptris_xpath_result* pending; /* with-params of the
                                             in-flight call */
    int terminated;                /* xsl:message terminate */
    int eval_error;                /* expression evaluation failed —
                                    * abort (issue 627: unknown
                                    * functions raise like plain XPath,
                                    * never silently empty) */
    char error[192];
    char* message;                 /* collected message text */
    LeptrisElement pending_parent;/* current output insertion point */
    void* frag_nodes;             /* pre-root fragment nodes */
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

    /* position() (§12.4): the 1-based position of current_node in
     * the node-list being processed (for-each/apply-templates
     * iteration). Set by the iterating ops; 1 outside them (the
     * XPath default). */
    size_t current_pos;
    size_t current_size;   /* last() (issue 628: pairs with pos) */

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
    /* xslt_functions.c: per-(accumulator, tree) computed value maps
     * (3.0 §18.2) — see XsltAccMap. */
    void* accs;
    void* docs;

    /* EXSLT func:function in flight: func:result stores the return
     * value here and flags the walker to unwind. */
    struct leptris_xpath_result* fn_result;
    int fn_yield;

    /* xsl:iterate (3.0 §12.5): the signal unwinds nested instruction
     * sequences up to the innermost enclosing op_iterate — 1 = next
     * (with ex->iter_params rebindings), 2 = break. iter_params is
     * owned by the producing op_next_iteration and consumed (or
     * freed) by the enclosing op_iterate each pass. */
    int iterate_signal;
    int iterate_depth;
    XsltVar* iter_params;

    /* §11.7 tunnel parameters: xsl:with-param tunnel="yes" pushes
     * here (never popped by the call — flows to every template the
     * subtree processing reaches); xsl:param tunnel="yes" binds
     * from this chain into the regular frame for the body. */
    XsltVar* tunnel_vars;

    /* xsl:for-each-group (3.0 §14): the group in flight — an OWNED
     * nodeset of borrowed member pointers plus the string grouping
     * key, served by current-group() / current-grouping-key() through
     * the bridge. NULL outside a for-each-group body. */
    XPathNodeSet* cur_group;
    char* cur_group_key;

    /* xsl:merge (3.0 §14.3): the group in flight — per-source item
     * arrays (borrowed pointers; owned by op_merge's entry list) and
     * the composite key string, served by current-merge-group() /
     * current-merge-key() through the bridge. NULL/0 outside a
     * merge-action body. */
    char* merge_key;
    XsltMergeSide* merge_sides;
    size_t merge_side_count;

    /* xsl:analyze-string (3.0 §18): the match in flight — the source
     * string (owned for the duration of the matching-substring body)
     * and POSIX regmatch captures into it; regex-group(n) reads these.
     * NULL outside analyze-string. Captures are relative to
     * as_src + as_pos (regexec runs on the unscanned tail). */
    char* as_src;
    void* as_pmatch;             /* regmatch_t[as_nmatch] */
    size_t as_nmatch;
    size_t as_pos;

    /* xslt_functions.c: exec-owned func:function registry bindings
     * (see XsltUfnBinding). */
    void* ufn;

    /* xslt_functions.c: generate-id() sequential numbering map
     * (bug-224) — node identity → id, assigned in first-request
     * order (libxslt's deterministic per-transform counter). */
    void* gids;

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

/* xslt_pattern.c — pattern matching (§5.2). ns carries the owning
 * template's in-scope bindings so prefixed name tests resolve by
 * namespace URI, not prefix spelling (§5.3). */
int xslt_pattern_matches(const XsltPattern* p, LeptrisElement node,
                         LeptrisDocument doc, LeptrisXPathNsSet ns);

/* Pattern-compiler fast path (xslt_pattern.c): compile a
 * child-axis ladder alternative into per-step name/kind tests
 * with an optional last-step predicate; free with
 * xslt_pattern_steps_free at stylesheet teardown. */
void xslt_pattern_compile_steps(XsltPattern* p, const char* src);
void xslt_pattern_steps_free(XsltPattern* p);

/* Eval hook: lets the caller supply a richer evaluation route than
 * the plain ns-aware one — xsl:number count/from patterns evaluate
 * with the current variable frame (bug-214). NULL = default route. */
typedef struct leptris_xpath_result* (*XsltPatternEvalFn)(
    void* ud, LeptrisXPathCompiled expr, LeptrisDocument doc,
    LeptrisElement ctx);

int xslt_pattern_matches_ex(const XsltPattern* p, LeptrisElement node,
                            LeptrisDocument doc, LeptrisXPathNsSet ns,
                            XsltPatternEvalFn hook, void* ud);

/* xslt_exec.c — pattern + execution helpers shared with
 * xslt_functions.c (the document-order walker reused for key-index
 * construction and the nodeset result identifier). */
LeptrisElement xslt_next_doc_order(LeptrisElement e);

/* Fragment chain (xslt_exec.c): the ordered pre-root result nodes.
 * apply_string walks this then the root sibling chain. */
typedef struct xslt_frag_node {
    LeptrisNodeRef node;
    struct xslt_frag_node* next;
} XsltFragNode;

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
void xslt_pop_vars_to(XsltExec* ex, XsltVar* mark);
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
void xslt_accs_free(XsltExec* ex);
void xslt_docs_free(XsltExec* ex);
void xslt_ufn_free(XsltExec* ex);
void xslt_gids_free(XsltExec* ex);

/* format-number(§12.3) — shared by the bridge; returns an OWNED
 * string. df_name NULL/"" selects the default decimal-format. */
struct leptris_xpath_ns_map;
char* xslt_format_number(const XsltStylesheet* sheet, double value,
                         const char* pattern, const char* df_name,
                         const struct leptris_xpath_ns_map* ns);

/* Attribute-set application (xslt_exec.c): evaluates the set's AVT
 * attrs onto `target`; existing attributes are NOT overwritten
 * (§7.1.4 — later sets and explicit attrs win). */
void xslt_apply_attr_sets(XsltExec* ex, const XsltInstr* in,
                          LeptrisElement target, LeptrisElement node);

#endif /* LEPTRIS_XSLT_INTERNAL_H */
