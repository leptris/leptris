/* lib/src/xpath/compiler.c — XPath AST → bytecode (TODO 120)
 *
 * Walks the AST and emits bytecode + populates the constant pool.
 *
 * Phase F design: every AST node compiles to a sequence of opcodes.
 *   - XPATH_AST_NUMBER / STRING  → BC_LITERAL_*
 *   - XPATH_AST_ABSOLUTE_PATH    → BC_PATH_ABSOLUTE + step sequence
 *   - XPATH_AST_RELATIVE_PATH / PATH_EXPR → BC_PATH_RELATIVE + steps
 *   - XPATH_AST_STEP (bare)      → BC_PATH_RELATIVE + BC_AXIS_STEP
 *   - XPATH_AST_OPERATOR         → operands then BC_BINARY_OP
 *   - XPATH_AST_FUNCTION_CALL    → BC_FUNC_CALL (const-pool refs AST)
 *   - everything else            → BC_FALLBACK_EVAL (const-pool refs AST)
 *
 * The VM handles each opcode inline (vm.c). Fallback preserves
 * completeness — any AST node we don't specifically compile still
 * evaluates correctly via evaluate_expr.
 *
 * Open/closed: adding a new inline handler = add a case here + add
 * a case to the VM switch. No existing case changes.
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

static void emit_op_u8(CompilerState* st, XPathOpcode op, uint8_t operand) {
    if (reserve_code(st, 2) == 0) {
        st->bc->code[st->bc->code_len++] = (unsigned char)op;
        st->bc->code[st->bc->code_len++] = operand;
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

/* Forward decl for recursive compilation. */
static void compile_node(CompilerState* st, XPathASTNode* node);

/* Heuristic: does this AST type potentially produce a nodeset at
 * eval time? Used by the compiler to decide whether a comparison
 * can be lowered to BC_BINARY_OP (correct only for scalar operands)
 * or must fall back to evaluate_expr (which implements XPath's
 * any-pair nodeset-comparison semantics).
 *
 * Conservative: err on the side of "yes". False positives only
 * cost a micro-optimization; false negatives would be a real bug. */
static int ast_may_produce_nodeset(const XPathASTNode* node) {
    if (!node) return 1;
    switch (node->type) {
        case XPATH_AST_ABSOLUTE_PATH:
        case XPATH_AST_RELATIVE_PATH:
        case XPATH_AST_PATH_EXPR:
        case XPATH_AST_STEP:
        case XPATH_AST_VARIABLE_REFERENCE:
            return 1;
        case XPATH_AST_FUNCTION_CALL:
            /* id() can return a nodeset; we conservatively treat
             * all function calls as potentially nodeset-producing.
             * Cost: one fallback eval for comparisons whose operand
             * is a function call — rare in micro-benchmarks. */
            return 1;
        case XPATH_AST_OPERATOR:
            /* UNION produces a nodeset; arithmetic/comparison do not. */
            if ((XPathOperatorType)node->number_value == XPATH_OP_UNION)
                return 1;
            return 0;
        default:
            return 0;
    }
}

/* Is this operator a comparison? */
static int op_is_comparison(XPathOperatorType op) {
    return op == XPATH_OP_EQUAL || op == XPATH_OP_NOT_EQUAL ||
           op == XPATH_OP_LESS || op == XPATH_OP_LESS_EQUAL ||
           op == XPATH_OP_GREATER || op == XPATH_OP_GREATER_EQUAL;
}

/* Emit bytecode for a sequence of STEP nodes that appear as children
 * of a path expression. Each step becomes one BC_AXIS_STEP that
 * consumes the previous step's nodeset and produces the next.
 *
 * Children of RELATIVE_PATH / ABSOLUTE_PATH may be either bare STEP
 * nodes or a RELATIVE_PATH container holding STEP children — the
 * parser uses both shapes. Walk either uniformly. */
static void compile_step_sequence(CompilerState* st, XPathASTNode** children,
                                   size_t child_count) {
    for (size_t i = 0; i < child_count; i++) {
        XPathASTNode* child = children[i];
        if (!child) continue;

        if (child->type == XPATH_AST_STEP) {
            emit_op_u16(st, XPATH_BC_AXIS_STEP, add_const_ast(st, child));
        } else if (child->type == XPATH_AST_RELATIVE_PATH) {
            compile_step_sequence(st, child->children, child->child_count);
        } else {
            /* Unexpected shape under a path — fall back to AST eval
             * so correctness is preserved. */
            emit_op_u16(st, XPATH_BC_FALLBACK_EVAL, add_const_ast(st, child));
        }
    }
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

        case XPATH_AST_ABSOLUTE_PATH:
            /* Absolute paths require the document-root special case
             * from evaluate_location_path (matching `/rootname` to
             * the document root element). Replicating that in the
             * VM is brittle; fall back to evaluate_expr, which
             * calls evaluate_location_path. The bytecode cache
             * still amortizes compile cost across evals. */
            emit_op_u16(st, XPATH_BC_FALLBACK_EVAL, add_const_ast(st, node));
            break;

        case XPATH_AST_RELATIVE_PATH:
        case XPATH_AST_PATH_EXPR:
            /* Push context node, then walk each step. */
            emit_op(st, XPATH_BC_PATH_RELATIVE);
            compile_step_sequence(st, node->children, node->child_count);
            break;

        case XPATH_AST_STEP:
            /* Bare step (e.g. `@id`, `.`, `..`) — evaluate as a
             * one-step relative path from the context node. */
            emit_op(st, XPATH_BC_PATH_RELATIVE);
            emit_op_u16(st, XPATH_BC_AXIS_STEP, add_const_ast(st, node));
            break;

        case XPATH_AST_OPERATOR: {
            XPathOperatorType op = (XPathOperatorType)node->number_value;

            /* Comparison operators with a nodeset operand require
             * XPath's any-pair semantics, which the inline VM handler
             * does not implement. Fall back to evaluate_expr so the
             * result is correct; the cost is one AST eval per
             * comparison, which is the existing baseline. */
            if (op_is_comparison(op)) {
                int lhs_is_ns = (node->child_count >= 1 &&
                                 ast_may_produce_nodeset(node->children[0]));
                int rhs_is_ns = (node->child_count >= 2 &&
                                 ast_may_produce_nodeset(node->children[1]));
                if (lhs_is_ns || rhs_is_ns) {
                    emit_op_u16(st, XPATH_BC_FALLBACK_EVAL,
                                add_const_ast(st, node));
                    break;
                }
            }

            /* Unary negation: one operand. */
            if (op == XPATH_OP_NEGATION) {
                if (node->child_count >= 1) {
                    compile_node(st, node->children[0]);
                }
                emit_op_u8(st, XPATH_BC_BINARY_OP, (uint8_t)op);
                break;
            }

            /* Binary: left then right then op. */
            if (node->child_count >= 1) {
                compile_node(st, node->children[0]);
            }
            if (node->child_count >= 2) {
                compile_node(st, node->children[1]);
            }
            emit_op_u8(st, XPATH_BC_BINARY_OP, (uint8_t)op);
            break;
        }

        case XPATH_AST_FUNCTION_CALL:
            /* The handler signature takes AST args, so we don't
             * pre-evaluate them on the stack. Stash the FUNC_CALL
             * AST in the constant pool; the VM calls
             * evaluate_function_call(ctx, ast_fc) directly,
             * skipping the evaluate_expr AST-type switch. */
            emit_op_u16(st, XPATH_BC_FUNC_CALL, add_const_ast(st, node));
            break;

        default:
            /* Variable refs, uncommon node tests, anything else:
             * punt to evaluate_expr. Correctness preserved. */
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
