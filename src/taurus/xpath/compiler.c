/* lib/src/xpath/compiler.c — XPath AST → bytecode (TODO 120 Phase A)
 *
 * Walks the AST and emits bytecode + populates the constant pool.
 * Recursive descent over XPathASTNode.  Each AST node type maps to
 * one or more opcodes:
 *
 *   XPATH_AST_NUMBER         → LITERAL_NUMBER <idx>
 *   XPATH_AST_STRING         → LITERAL_STRING <idx>
 *   XPATH_AST_PATH_EXPR      → recurse on child (path root + steps)
 *   XPATH_AST_STEP           → AXIS_STEP <axis> ; NODE_TEST_* [; FILTER ...]
 *   XPATH_AST_NODE_TEST_*    → NODE_TEST_* opcodes
 *   XPATH_AST_PREDICATE      → recurse expr ; FILTER
 *   XPATH_AST_OPERATOR       → recurse left/right ; BINARY_OP <op>
 *
 * AST node types we don't yet support fall back to OP_FALLBACK_EVAL
 * with the AST node index in the constant pool.  The VM recognizes
 * the fallback and delegates to evaluate_expr.
 */
#include "bytecode.h"
#include "evaluator_internal.h"
#include "../taurus_internal.h"
#include <stdlib.h>
#include <string.h>

/* Internal bytecode builder state -- grows code buffer + constant
 * pool as needed.  Opaque to callers; only the public API in
 * bytecode.h is exposed. */
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

/* Append a single opcode. */
static void emit_op(CompilerState* st, XPathOpcode op) {
    if (reserve_code(st, 1) == 0) {
        st->bc->code[st->bc->code_len++] = (unsigned char)op;
    }
}

/* Append an opcode + 1-byte operand. */
static void emit_op_u8(CompilerState* st, XPathOpcode op, uint8_t operand) {
    if (reserve_code(st, 2) == 0) {
        st->bc->code[st->bc->code_len++] = (unsigned char)op;
        st->bc->code[st->bc->code_len++] = operand;
    }
}

/* Append an opcode + 16-bit operand (constant-pool index). */
static void emit_op_u16(CompilerState* st, XPathOpcode op, uint16_t operand) {
    if (reserve_code(st, 3) == 0) {
        st->bc->code[st->bc->code_len++] = (unsigned char)op;
        st->bc->code[st->bc->code_len++] = (operand >> 8) & 0xFF;
        st->bc->code[st->bc->code_len++] = operand & 0xFF;
    }
}

/* Append a constant-pool entry; returns its index, or 0xFFFF on failure. */
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

/* Forward decl. */
static void compile_node(CompilerState* st, XPathASTNode* node);

/* Compile a node-test AST into one of the NODE_TEST_* opcodes. */
static void compile_node_test(CompilerState* st, XPathASTNode* test) {
    if (!test) { emit_op(st, XPATH_BC_NODE_TEST_ALL); return; }
    switch (test->type) {
        case XPATH_AST_NODE_TEST_NAME: {
            uint16_t idx = add_const_string(st, test->value);
            emit_op_u16(st, XPATH_BC_NODE_TEST_NAME, idx);
            break;
        }
        case XPATH_AST_NODE_TEST_ALL:
            emit_op(st, XPATH_BC_NODE_TEST_ALL);
            break;
        default:
            /* Complex node tests (TYPE, PI, ALL_IN_NS) fall back. */
            emit_op_u16(st, XPATH_BC_FALLBACK_EVAL, add_const_ast(st, test));
            break;
    }
}

/* Compile a STEP AST: emit AXIS_STEP + node test + filter ops for
 * each predicate.  The path-root context is expected on the stack
 * before this opcode sequence runs. */
static void compile_step(CompilerState* st, XPathASTNode* step) {
    if (!step) return;
    /* AXIS_STEP consumes the axis_id from operand. */
    emit_op_u8(st, XPATH_BC_AXIS_STEP, (uint8_t)step->axis_id);
    /* The node test is the first child. */
    if (step->child_count >= 1) {
        compile_node_test(st, step->children[0]);
    } else {
        emit_op(st, XPATH_BC_NODE_TEST_ALL);
    }
    /* Remaining children are predicates. */
    for (size_t i = 1; i < step->child_count; i++) {
        compile_node(st, step->children[i]);
        emit_op(st, XPATH_BC_FILTER);
    }
}

/* Compile a path expression -- sequence of steps starting from a
 * context (root or current). */
static void compile_path(CompilerState* st, XPathASTNode* path) {
    if (!path) return;
    /* For an absolute path, the first child is typically a special
     * "root" marker; for a relative path, the first step starts from
     * the current context.  Phase A emits ROOT_CONTEXT for absolute
     * paths. */
    emit_op(st, XPATH_BC_ROOT_CONTEXT);
    for (size_t i = 0; i < path->child_count; i++) {
        compile_step(st, path->children[i]);
    }
}

static void compile_operator(CompilerState* st, XPathASTNode* node) {
    /* Operators store op_type in node->axis_id field (see parser.c).
     * left = children[0], right = children[1]. */
    if (node->child_count >= 1) compile_node(st, node->children[0]);
    if (node->child_count >= 2) compile_node(st, node->children[1]);
    emit_op_u8(st, XPATH_BC_BINARY_OP, (uint8_t)node->axis_id);
}

static void compile_node(CompilerState* st, XPathASTNode* node) {
    if (!node || st->error) return;
    switch (node->type) {
        case XPATH_AST_NUMBER:
            emit_op_u16(st, XPATH_BC_LITERAL_NUMBER,
                        add_const_number(st, node->number_value));
            break;
        case XPATH_AST_STRING:
            emit_op_u16(st, XPATH_BC_LITERAL_STRING,
                        add_const_string(st, node->value));
            break;
        case XPATH_AST_PATH_EXPR:
            compile_path(st, node);
            break;
        case XPATH_AST_STEP:
            compile_step(st, node);
            break;
        case XPATH_AST_NODE_TEST_NAME:
        case XPATH_AST_NODE_TEST_ALL:
        case XPATH_AST_NODE_TEST_TYPE:
        case XPATH_AST_NODE_TEST_PI:
        case XPATH_AST_NODE_TEST_ALL_IN_NS:
            compile_node_test(st, node);
            break;
        case XPATH_AST_OPERATOR:
            compile_operator(st, node);
            break;
        default:
            /* Unsupported node type -- fall back to AST eval. */
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
