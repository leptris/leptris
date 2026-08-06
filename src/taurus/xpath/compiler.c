/* lib/src/xpath/compiler.c — XPath AST → bytecode (TODO 120)
 *
 * Walks the AST and emits bytecode + populates the constant pool.
 *
 * Phase A+B approach: decompose AST into fine-grained opcodes
 * (AXIS_STEP, NODE_TEST, BINARY_OP, FILTER, FUNC_CALL, etc.).
 *
 * Phase C-E approach (this version): emit LITERAL_* for leaf values
 * and BC_FALLBACK_EVAL for everything else.  The VM calls
 * evaluate_expr on the AST node for fallback cases.  This makes
 * the VM COMPLETE — it can evaluate any XPath expression — at the
 * cost of no perf win on complex expressions (they still go through
 * evaluate_expr).  Future work can incrementally replace specific
 * FALLBACK_EVAL opcodes with inline VM handlers.
 *
 * The decomposition is designed as open/closed: adding a new inline
 * handler = add a case to the VM switch + change the compiler to
 * emit the specific opcode instead of FALLBACK_EVAL.  No existing
 * code changes.
 */
#include "bytecode.h"
#include "evaluator_internal.h"
#include "../taurus_internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    TaurusXPathBytecode* bc;
    int error;
} CompilerState;

static int reserve_code(CompilerState* st, size_t extra) {
    if (st->bc->code_len + extra <= st->bc->code_cap) return 0;
    size_t new_cap = st->bc->code_cap ? st->bc->code_cap : 64;
    while (new_cap < st->bc->code_len + extra) new_cap *= 2;
    unsigned char* grown = (unsigned char*)realloc(st->bc->code, new_cap);
    if (!grown) { st->error = 1; return -1; }
    st->bc->code = grown;
    st->bc->code_cap = new_cap;
    return 0;
}

static int reserve_constants(CompilerState* st, size_t extra) {
    if (st->bc->const_count + extra <= st->bc->const_cap) return 0;
    size_t new_cap = st->bc->const_cap ? st->bc->const_cap : 16;
    while (new_cap < st->bc->const_count + extra) new_cap *= 2;
    XPathConstant* grown = (XPathConstant*)realloc(st->bc->constants,
                                                    new_cap * sizeof(XPathConstant));
    if (!grown) { st->error = 1; return -1; }
    st->bc->constants = grown;
    st->bc->const_cap = new_cap;
    return 0;
}

static void emit_op(CompilerState* st, XPathOpcode op) {
    if (reserve_code(st, 1) == 0) {
        st->bc->code[st->bc->code_len++] = (unsigned char)op;
    }
}

static void emit_op_u16(CompilerState* st, XPathOpcode op, uint16_t operand) {
    if (reserve_code(st, 3) == 0) {
        st->bc->code[st->bc->code_len++] = (unsigned char)op;
        st->bc->code[st->bc->code_len++] = (operand >> 8) & 0xFF;
        st->bc->code[st->bc->code_len++] = operand & 0xFF;
    }
}

static uint16_t add_const_number(CompilerState* st, double n) {
    if (reserve_constants(st, 1) < 0) return 0xFFFF;
    if (st->bc->const_count >= 0xFFFE) { st->error = 1; return 0xFFFF; }
    uint16_t idx = (uint16_t)st->bc->const_count++;
    st->bc->constants[idx].type = XPATH_CONST_NUMBER;
    st->bc->constants[idx].v.number = n;
    return idx;
}

static uint16_t add_const_string(CompilerState* st, const char* s) {
    if (reserve_constants(st, 1) < 0) return 0xFFFF;
    if (st->bc->const_count >= 0xFFFE) { st->error = 1; return 0xFFFF; }
    uint16_t idx = (uint16_t)st->bc->const_count++;
    char* copy = s ? strdup(s) : strdup("");
    if (!copy) { st->error = 1; return 0xFFFF; }
    st->bc->constants[idx].type = XPATH_CONST_STRING;
    st->bc->constants[idx].v.string = copy;
    return idx;
}

static uint16_t add_const_ast(CompilerState* st, XPathASTNode* ast) {
    if (reserve_constants(st, 1) < 0) return 0xFFFF;
    if (st->bc->const_count >= 0xFFFE) { st->error = 1; return 0xFFFF; }
    uint16_t idx = (uint16_t)st->bc->const_count++;
    st->bc->constants[idx].type = XPATH_CONST_AST_NODE;
    st->bc->constants[idx].v.ast = ast;
    return idx;
}

static void compile_node(CompilerState* st, XPathASTNode* node) {
    if (!node || st->error) return;

    /* Leaf nodes get inline opcodes.  Everything else punts to
     * evaluate_expr via BC_FALLBACK_EVAL with the AST node in the
     * constant pool.  Future phases can add inline handlers for
     * specific node types (OPERATOR, STEP, FUNCTION_CALL) by
     * replacing the default case with explicit cases. */
    switch (node->type) {
        case XPATH_AST_NUMBER:
            emit_op_u16(st, XPATH_BC_LITERAL_NUMBER,
                        add_const_number(st, node->number_value));
            break;
        case XPATH_AST_STRING:
            emit_op_u16(st, XPATH_BC_LITERAL_STRING,
                        add_const_string(st, node->value));
            break;
        default:
            /* All non-literal nodes: store AST node in constant pool
             * and emit FALLBACK_EVAL.  The VM calls evaluate_expr
             * on the stored node. */
            emit_op_u16(st, XPATH_BC_FALLBACK_EVAL, add_const_ast(st, node));
            break;
    }
}

TaurusXPathBytecode* taurus_xpath_compile_ast(XPathASTNode* ast) {
    if (!ast) return NULL;
    TaurusXPathBytecode* bc = (TaurusXPathBytecode*)calloc(1, sizeof(*bc));
    if (!bc) return NULL;
    CompilerState st = { .bc = bc, .error = 0 };
    compile_node(&st, ast);
    emit_op(&st, XPATH_BC_RETURN);
    if (st.error) {
        taurus_xpath_bytecode_free(bc);
        return NULL;
    }
    return bc;
}

void taurus_xpath_bytecode_free(TaurusXPathBytecode* bc) {
    if (!bc) return;
    free(bc->code);
    for (size_t i = 0; i < bc->const_count; i++) {
        if (bc->constants[i].type == XPATH_CONST_STRING) {
            free(bc->constants[i].v.string);
        }
    }
    free(bc->constants);
    free(bc);
}
