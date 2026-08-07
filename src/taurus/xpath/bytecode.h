/* lib/src/xpath/bytecode.h — XPath bytecode format (TODO 120)
 *
 * Compile-once, eval-many: the AST is compiled to a flat instruction
 * sequence and stored in a TaurusXPathBytecode.  A VM interpreter
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
#ifndef TAURUS_XPATH_BYTECODE_H
#define TAURUS_XPATH_BYTECODE_H

#include "../taurus_internal.h"

typedef enum {
    XPATH_BC_NOP = 0,
    XPATH_BC_LITERAL_NUMBER,   /* u16 operand: const-pool index (double) */
    XPATH_BC_LITERAL_STRING,   /* u16 operand: const-pool index (string) */
    XPATH_BC_LITERAL_BOOL,     /* u8 operand: 0/1 */
    XPATH_BC_PATH_ABSOLUTE,    /* push document root as single-node nodeset */
    XPATH_BC_PATH_RELATIVE,    /* push context node as single-node nodeset */
    XPATH_BC_AXIS_STEP,        /* u16 operand: const-pool index (STEP AST) */
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
typedef struct TaurusXPathBytecode {
    unsigned char* code;        /* opcode + inline operands */
    size_t code_len;
    size_t code_cap;

    XPathConstant* constants;   /* constant pool */
    size_t const_count;
    size_t const_cap;
} TaurusXPathBytecode;

/* Compile an XPath AST into bytecode.  Returns NULL on failure.
 * The bytecode references AST nodes from the constant pool but
 * does not own them — caller must keep the AST alive for the
 * bytecode's lifetime. */
TaurusXPathBytecode* taurus_xpath_compile_ast(XPathASTNode* ast);

/* Free a compiled bytecode. */
void taurus_xpath_bytecode_free(TaurusXPathBytecode* bc);

#endif /* TAURUS_XPATH_BYTECODE_H */
