/* lib/src/xpath/bytecode.h — XPath bytecode format (TODO 120 Phase A)
 *
 * Compile-once, eval-many: the AST is compiled to a flat instruction
 * sequence and stored in a TaurusXPathBytecode.  A VM interpreter
 * runs the sequence against a document.
 *
 * Why bytecode over AST walking:
 *   - AST walking does an indirect call per node + a switch on type.
 *     Bytecode dispatch is a single switch + program-counter increment.
 *   - Bytecode can be cached: parse the XPath string once, compile to
 *     bytecode, reuse across many documents/contexts.
 *
 * Phase A scope (this file): a minimal instruction set covering the
 * common XPath subset.  The compiler walks the AST and emits opcodes;
 * the VM runs them.  Unsupported AST nodes fall back to the AST
 * evaluator via OP_FALLBACK_EVAL -- the VM pushes the AST node from
 * the constant pool and the VM's main loop calls evaluate_expr.
 *
 * Future phases (B+C in TODO.fix/120-xpath-bytecode-vm.md):
 *   - Full predicate support (sub-frames).
 *   - Function-call dispatch via the existing XPathFunction table.
 *   - Replace taurus_xpath_eval with compile + VM by default.
 */
#ifndef TAURUS_XPATH_BYTECODE_H
#define TAURUS_XPATH_BYTECODE_H

#include "../taurus_internal.h"

/* Opcode enum.  Adding a new opcode = append here, no switch to edit
 * outside the VM dispatch (open/closed).  Prefixed XPATH_BC_ to
 * avoid collision with the XPATH_OP_* AST-operator enum in
 * taurus_internal.h. */
typedef enum {
    XPATH_BC_NOP = 0,
    XPATH_BC_LITERAL_NUMBER,   /* operand: const-pool index (double) */
    XPATH_BC_LITERAL_STRING,   /* operand: const-pool index (string) */
    XPATH_BC_LITERAL_BOOL,     /* operand: 0/1 */
    XPATH_BC_ROOT_CONTEXT,     /* push document root as nodeset */
    XPATH_BC_AXIS_STEP,        /* operand: axis_id; consumes test from
                                * next op */
    XPATH_BC_NODE_TEST_NAME,   /* operand: const-pool index (name) */
    XPATH_BC_NODE_TEST_ALL,
    XPATH_BC_NODE_TEST_TYPE,   /* operand: TAURUS_NODE_TYPE_* */
    XPATH_BC_FILTER,           /* consume top of stack as predicate
                                * expression, apply to nodeset below */
    XPATH_BC_BINARY_OP,        /* operand: XPathOperatorType */
    XPATH_BC_UNARY_MINUS,
    XPATH_BC_FUNC_CALL,        /* operand: name idx + arg count */
    XPATH_BC_UNION,
    XPATH_BC_FALLBACK_EVAL,    /* operand: AST-node index -- punt to
                                * AST evaluator for unsupported AST */
    XPATH_BC_RETURN
} XPathOpcode;

/* Constant pool entry -- tagged union over the things an opcode can
 * reference.  Phase A only uses NUMBER and STRING. */
typedef enum {
    XPATH_CONST_NUMBER,
    XPATH_CONST_STRING,
    XPATH_CONST_AST_NODE        /* Pointer to AST node for fallback. */
} XPathConstType;

typedef struct {
    XPathConstType type;
    union {
        double number;
        char* string;           /* heap-owned; freed with bytecode */
        XPathASTNode* ast;      /* non-owning; AST owns itself */
    } v;
} XPathConstant;

/* Bytecode object -- the compiled form of an XPath expression. */
typedef struct {
    unsigned char* code;        /* opcode + inline operands */
    size_t code_len;
    size_t code_cap;

    XPathConstant* constants;   /* constant pool */
    size_t const_count;
    size_t const_cap;
} TaurusXPathBytecode;

/* Compile an XPath AST into bytecode.  Returns NULL on failure. */
TaurusXPathBytecode* taurus_xpath_compile_ast(XPathASTNode* ast);

/* Free a compiled bytecode. */
void taurus_xpath_bytecode_free(TaurusXPathBytecode* bc);

#endif /* TAURUS_XPATH_BYTECODE_H */
