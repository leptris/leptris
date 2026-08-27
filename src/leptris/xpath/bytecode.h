/* lib/src/xpath/bytecode.h — XPath bytecode format (TODO 120)
 *
 * Compile-once, eval-many: the AST is compiled to a flat instruction
 * sequence and stored in a LeptrisXPathBytecode.  A VM interpreter
 * runs the sequence against a document.
 *
 * Why bytecode over AST walking:
 *   - The bytecode is cached alongside the AST in the expression
 *     cache (TODO 120 Phase F). Compile cost is paid once per unique
 *     expression; subsequent evals skip both parse and compile.
 *   - Bytecode dispatch is a single switch + program-counter
 *     increment; AST walking is a switch + indirect recursive call
 *     per node.
 *
 * Opcode model:
 *   - Leaf values → BC_LITERAL_* (number, string, bool).
 *   - Path expressions → BC_PATH_ABSOLUTE / BC_PATH_RELATIVE
 *     (push starting nodeset) followed by a sequence of
 *     BC_AXIS_STEP (each pops the input nodeset, evaluates one
 *     step, pushes the result).
 *   - Operators → operands emitted first, then BC_BINARY_OP.
 *   - Function calls → BC_FUNC_CALL referencing the FUNCTION_CALL
 *     AST in the constant pool. The VM calls evaluate_function_call
 *     directly, skipping the evaluate_expr AST-type switch.
 *   - Anything else (variable references, uncommon node tests)
 *     stays on BC_FALLBACK_EVAL.
 *
 * Open/closed: adding a new opcode = append to the enum + add a
 * case to the VM switch + add a compiler case.  No existing case
 * changes.  Prefixed XPATH_BC_ to avoid colliding with the
 * XPATH_OP_* AST-operator enum.
 */
#ifndef LEPTRIS_XPATH_BYTECODE_H
#define LEPTRIS_XPATH_BYTECODE_H

#include "../leptris_internal.h"

typedef enum {
    XPATH_BC_NOP = 0,
    XPATH_BC_LITERAL_NUMBER,   /* u16 operand: const-pool index (double) */
    XPATH_BC_LITERAL_STRING,   /* u16 operand: const-pool index (string) */
    XPATH_BC_LITERAL_BOOL,     /* u8 operand: 0/1 */
    XPATH_BC_PATH_ABSOLUTE,    /* push document root as single-node nodeset */
    XPATH_BC_PATH_RELATIVE,    /* push context node as single-node nodeset */
    XPATH_BC_AXIS_STEP,        /* u16 operand: const-pool index (STEP AST) */

    /* Specialized axis opcodes (TODO 126). The compiler emits these
     * for the common shape: single name test or wildcard, no
     * namespace prefix, no predicates. The VM handler is a tight
     * loop that bypasses evaluate_step → apply_axis →
     * matches_node_test. */
    XPATH_BC_AXIS_CHILD_NAME,       /* u16 operand: const-pool string */
    XPATH_BC_AXIS_CHILD_WILD,       /* no operand */
    XPATH_BC_AXIS_ATTRIBUTE_NAME,   /* u16 operand: const-pool string */
    XPATH_BC_AXIS_ATTRIBUTE_WILD,   /* no operand */
    XPATH_BC_AXIS_SELF_NAME,        /* u16 operand: const-pool string */
    XPATH_BC_AXIS_SELF_WILD,        /* no operand */
    XPATH_BC_AXIS_PARENT_NAME,      /* u16 operand: const-pool string */
    XPATH_BC_AXIS_PARENT_WILD,      /* no operand */

    /* Descendant / descendant-or-self (TODO 127). Tight recursive
     * subtree walk inline. Skip dedup when input has 1 element
     * (the common case for these axes). */
    XPATH_BC_AXIS_DESCENDANT_NAME,        /* u16 operand: const-pool string */
    XPATH_BC_AXIS_DESCENDANT_WILD,        /* no operand */
    XPATH_BC_AXIS_DESCENDANT_OR_SELF_NAME,/* u16 operand: const-pool string */
    XPATH_BC_AXIS_DESCENDANT_OR_SELF_WILD,/* no operand */
    /* Fused relative descendant + attr-equals predicate (TODO 192c).
     * Three u16 operands: name idx, attr name idx, value idx. Served
     * from the element index: per context element, the subtree
     * interval windows the attr-VALUE bucket's preorder positions —
     * O(log B + hits) instead of walking the subtree and each
     * element's attribute list. Falls back to walk+filter without
     * the index. */
    XPATH_BC_AXIS_DESCENDANT_NAME_ATTREQ,
    /* Absolute //name[@attr='value'], same operands and index
     * service, but self-contained (no input nodeset): the whole
     * document IS the interval, and the value-bucket scan is
     * descendant-or-self-correct by construction (the root is in
     * the bucket iff it carries attr=value). TODO 192d. */
    XPATH_BC_ABSOLUTE_DESCENDANT_NAME_ATTREQ,
    /* u16 u16 u16: name, attr, VARIABLE name — RHS resolved at run
     * time from the context's variable set (issue #565). */
    XPATH_BC_ABSOLUTE_DESCENDANT_NAME_ATTREQ_VAR,

    /* Fused axis+predicate opcodes (TODO 134). Combines the axis
     * walk with the predicate filter into a single pass, and uses
     * the attribute index for O(K) lookup when input is doc_root.
     * The compiler emits these instead of <axis> + BC_PRED_ATTR_*
     * when the predicate is simple. */
    XPATH_BC_AXIS_DESCENDANT_WILD_PRED_ATTR_EXISTS,
        /* u16 operand: attr name */
    XPATH_BC_AXIS_DESCENDANT_WILD_PRED_ATTR_EQ_STRING,
        /* u16 u16 operands: attr name, attr value */
    XPATH_BC_AXIS_DESCENDANT_OR_SELF_WILD_PRED_ATTR_EXISTS,
        /* u16 operand: attr name */
    XPATH_BC_AXIS_DESCENDANT_OR_SELF_WILD_PRED_ATTR_EQ_STRING,
        /* u16 u16 operands: attr name, attr value */

    /* Absolute-path first-step opcodes (TODO 129). The first step
     * of an absolute path needs document-root semantics: `/foo`
     * matches the root element if its name is foo, NOT root's
     * children. `//foo` walks the entire tree from root. */
    XPATH_BC_ABSOLUTE_ROOT_MATCH_NAME,         /* u16: name */
    XPATH_BC_ABSOLUTE_ROOT_MATCH_WILD,         /* no operand */
    XPATH_BC_ABSOLUTE_DESCENDANT_NAME,         /* u16: name */
    XPATH_BC_ABSOLUTE_DESCENDANT_WILD,         /* no operand */
    XPATH_BC_ABSOLUTE_DESCENDANT_OR_SELF_NAME, /* u16: name */
    XPATH_BC_ABSOLUTE_DESCENDANT_OR_SELF_WILD, /* no operand */
    XPATH_BC_ABSOLUTE_DESCENDANT_TYPE, /* u16 u16: type name (node/text/
                                          comment/processing-instruction),
                                          PI target (0xFFFF = any) */

    /* Inline function opcodes (TODO 130). The compiler emits
     * <arg bytecode> + BC_FUNC_<NAME> for the common XPath functions
     * instead of BC_FUNC_CALL. The VM evaluates args via normal
     * dispatch (which uses the fast specialized axis opcodes from
     * TODO 126-129), then applies the function inline.
     *
     * Each opcode pops its args from the stack and pushes the result.
     * Arg counts:
     *   no-arg: position, last, true, false
     *   1-arg (optional, defaults to context node as nodeset):
     *     string, number, boolean, name, local-name, namespace-uri,
     *     normalize-space, string-length
     *   1-arg (required): count, sum, not, floor, ceiling, round, lang
     *   2-arg: contains, starts-with, substring-before, substring-after
     *   2-3 arg: substring
     *   3-arg: translate, concat (variadic; operand = arg count)
     *
     * For variadic / multi-arg functions, the operand is the arg count.
     */
    XPATH_BC_FUNC_COUNT,           /* 1 nodeset arg → number */
    XPATH_BC_FUNC_STRING,          /* 0/1 arg → string */
    XPATH_BC_FUNC_NUMBER,          /* 0/1 arg → number */
    XPATH_BC_FUNC_BOOLEAN,         /* 1 arg → boolean */
    XPATH_BC_FUNC_NAME,            /* 0/1 arg → string */
    XPATH_BC_FUNC_LOCAL_NAME,      /* 0/1 arg → string */
    XPATH_BC_FUNC_NAMESPACE_URI,   /* 0/1 arg → string */
    XPATH_BC_FUNC_SUM,             /* 1 nodeset arg → number */
    XPATH_BC_FUNC_POSITION,        /* no arg → number */
    XPATH_BC_FUNC_LAST,            /* no arg → number */
    XPATH_BC_FUNC_TRUE,            /* no arg → boolean */
    XPATH_BC_FUNC_FALSE,           /* no arg → boolean */
    XPATH_BC_FUNC_NOT,             /* 1 arg → boolean */

    /* Simple predicate opcodes (TODO 128). Each pops the input
     * nodeset from the stack, applies the filter inline, and pushes
     * the filtered result. The compiler emits these for the common
     * predicate shapes:
     *   - [@attr]              → BC_PRED_ATTR_EXISTS
     *   - [@attr = 'literal']  → BC_PRED_ATTR_EQ_STRING
     *   - [N]                  → BC_PRED_POSITION
     *   - [child::n OP num]    → BC_PRED_CHILD_NUM_CMP (TODO 159)
     * Anything else stays on the existing apply_predicates path. */
    XPATH_BC_PRED_ATTR_EXISTS,       /* u16 operand: const-pool attr name */
    XPATH_BC_PRED_ATTR_EQ_STRING,    /* u16 operand: const-pool (name, value) pair encoded as two consecutive const-pool indices */
    XPATH_BC_PRED_POSITION,          /* u8 operand: position (1-based) */
    /* Fused child-axis numeric comparison (TODO 159 Phase D).
     * Operands: u8 operator (XPathOperatorType), u16 child name idx,
     * double literal RHS. Filters input nodeset by walking each
     * element's child list (hash-pre-filtered), parsing the matching
     * child's text as a number, and applying OP against RHS. Inline
     * two-pointer filter; no AST re-evaluation. */
    XPATH_BC_PRED_CHILD_NUM_CMP,

    XPATH_BC_BINARY_OP,        /* u8 operand: XPathOperatorType */
    XPATH_BC_FUNC_CALL,        /* u16 operand: const-pool index (FUNC AST) */
    XPATH_BC_FALLBACK_EVAL,    /* u16 operand: const-pool index (AST) */
    XPATH_BC_RETURN
} XPathOpcode;

/* Constant pool entry — tagged union over the things an opcode can
 * reference. */
typedef enum {
    XPATH_CONST_NUMBER,
    XPATH_CONST_STRING,
    XPATH_CONST_AST_NODE        /* Non-owning; AST owns itself. */
} XPathConstType;

typedef struct {
    XPathConstType type;
    union {
        double number;
        char* string;           /* heap-owned; freed with bytecode */
        XPathASTNode* ast;      /* non-owning; AST owns itself */
    } v;
} XPathConstant;

/* Bytecode object — the compiled form of an XPath expression. */
typedef struct LeptrisXPathBytecode {
    unsigned char* code;        /* opcode + inline operands */
    size_t code_len;
    size_t code_cap;

    XPathConstant* constants;   /* constant pool */
    size_t const_count;
    size_t const_cap;
} LeptrisXPathBytecode;

/* Compile an XPath AST into bytecode.  Returns NULL on failure.
 * The bytecode references AST nodes from the constant pool but
 * does not own them — caller must keep the AST alive for the
 * bytecode's lifetime. */
LeptrisXPathBytecode* leptris_xpath_compile_ast(XPathASTNode* ast);

/* Free a compiled bytecode. */
void leptris_xpath_bytecode_free(LeptrisXPathBytecode* bc);

#endif /* LEPTRIS_XPATH_BYTECODE_H */
