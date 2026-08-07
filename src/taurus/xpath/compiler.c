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

/* Try to lower a STEP AST to a specialized axis opcode (TODO 126).
 * Returns 1 if emitted, 0 if the shape doesn't match a fast path
 * and the caller should fall back to BC_AXIS_STEP.
 *
 * Match criteria:
 *   - axis is one of CHILD, ATTRIBUTE, SELF, PARENT
 *   - first child (node test) is NODE_TEST_NAME with no namespace
 *     prefix, OR NODE_TEST_ALL
 *   - no predicate children (child_count == 1)
 *
 * Name tests with a `:` (namespace prefix) require XPath's
 * namespace-aware matching semantics, which the inline handler
 * doesn't implement. Wildcards with namespace prefix same. */
static int try_compile_specialized_axis(CompilerState* st, XPathASTNode* step);

/* Classify a predicate AST into a "simple" shape that the VM can
 * apply inline (TODO 128). Returns PRED_KIND_NONE if the predicate
 * is too complex; out-params carry the matched values.
 *
 * Recognized shapes:
 *   - [@attr]            → PRED_KIND_ATTR_EXISTS
 *       The predicate is a bare attribute-axis STEP or a PATH_EXPR
 *       wrapping one, with a single name test.
 *   - [@attr = 'lit']    → PRED_KIND_ATTR_EQ_STRING
 *       Operator EQUAL with @attr on the left and a string literal
 *       on the right.
 *   - [N]                → PRED_KIND_POSITION
 *       Predicate is a numeric literal. */
typedef enum {
    PRED_KIND_NONE,
    PRED_KIND_ATTR_EXISTS,
    PRED_KIND_ATTR_EQ_STRING,
    PRED_KIND_POSITION
} PredKind;

static const char* pred_attr_name(XPathASTNode* pred) {
    /* pred may be a STEP or a PATH_EXPR wrapping a STEP. */
    XPathASTNode* step = pred;
    if (step->type == XPATH_AST_PATH_EXPR && step->child_count == 1) {
        step = step->children[0];
    }
    if (!step || step->type != XPATH_AST_STEP) return NULL;
    if (step->axis_id != XPATH_AXIS_ATTRIBUTE) return NULL;
    if (step->child_count != 1) return NULL;  /* predicate on the attr step */
    XPathASTNode* test = step->children[0];
    if (!test || test->type != XPATH_AST_NODE_TEST_NAME) return NULL;
    if (!test->value || strchr(test->value, ':')) return NULL;  /* no prefix */
    return test->value;
}

static PredKind classify_predicate(XPathASTNode* pred,
                                     const char** out_attr_name,
                                     const char** out_attr_value,
                                     long* out_position) {
    if (!pred) return PRED_KIND_NONE;
    *out_attr_name = NULL;
    *out_attr_value = NULL;
    *out_position = 0;

    /* [N] — numeric literal. */
    if (pred->type == XPATH_AST_NUMBER) {
        double v = pred->number_value;
        if (v >= 1.0 && v == (double)(long)v) {
            *out_position = (long)v;
            return PRED_KIND_POSITION;
        }
        return PRED_KIND_NONE;
    }

    /* [@attr] — bare attribute step. */
    if (pred->type == XPATH_AST_STEP || pred->type == XPATH_AST_PATH_EXPR) {
        const char* attr = pred_attr_name(pred);
        if (attr) {
            *out_attr_name = attr;
            return PRED_KIND_ATTR_EXISTS;
        }
    }

    /* [@attr = 'literal'] — operator EQUAL with @attr left, string right. */
    if (pred->type == XPATH_AST_OPERATOR &&
        (XPathOperatorType)pred->number_value == XPATH_OP_EQUAL &&
        pred->child_count == 2) {
        const char* attr = pred_attr_name(pred->children[0]);
        XPathASTNode* rhs = pred->children[1];
        if (attr && rhs && rhs->type == XPATH_AST_STRING && rhs->value) {
            *out_attr_name = attr;
            *out_attr_value = rhs->value;
            return PRED_KIND_ATTR_EQ_STRING;
        }
    }

    return PRED_KIND_NONE;
}

/* Try to compile a STEP with its predicates specialized (TODO 128).
 * Emits a specialized axis opcode (if the axis shape matches) followed
 * by one specialized predicate opcode per simple predicate. Falls
 * back to BC_AXIS_STEP if the axis shape doesn't match or any
 * predicate is non-simple.
 *
 * Returns 1 if emitted, 0 on fallback. */
static int try_compile_specialized_axis(CompilerState* st, XPathASTNode* step) {
    if (!step || step->type != XPATH_AST_STEP) return 0;

    XPathASTNode* test = (step->child_count >= 1) ? step->children[0] : NULL;
    if (!test) return 0;

    /* The test value carries the qualified name (may contain ':').
     * For namespace-aware fast path we'd need to split + resolve.
     * For now, only fast-path the no-colon case. */
    int has_name = (test->type == XPATH_AST_NODE_TEST_NAME);
    int has_wild = (test->type == XPATH_AST_NODE_TEST_ALL);
    if (!has_name && !has_wild) return 0;
    if (has_name && (!test->value || strchr(test->value, ':'))) return 0;

    /* Predicates: child_count==1 means no predicates. >1 means
     * there are predicates; check each is simple. */
    size_t pred_count = step->child_count - 1;
    if (pred_count > 0) {
        for (size_t i = 0; i < pred_count; i++) {
            XPathASTNode* pred = step->children[1 + i];
            const char *a, *v;
            long p;
            if (classify_predicate(pred, &a, &v, &p) == PRED_KIND_NONE) {
                return 0;  /* non-simple predicate — fall back */
            }
        }
    }

    XPathAxisType axis = step->axis_id;
    XPathOpcode op_name, op_wild;

    switch (axis) {
        case XPATH_AXIS_CHILD:      op_name = XPATH_BC_AXIS_CHILD_NAME;      op_wild = XPATH_BC_AXIS_CHILD_WILD;      break;
        case XPATH_AXIS_ATTRIBUTE:  op_name = XPATH_BC_AXIS_ATTRIBUTE_NAME;  op_wild = XPATH_BC_AXIS_ATTRIBUTE_WILD;  break;
        case XPATH_AXIS_SELF:       op_name = XPATH_BC_AXIS_SELF_NAME;       op_wild = XPATH_BC_AXIS_SELF_WILD;       break;
        case XPATH_AXIS_PARENT:     op_name = XPATH_BC_AXIS_PARENT_NAME;     op_wild = XPATH_BC_AXIS_PARENT_WILD;     break;
        case XPATH_AXIS_DESCENDANT:
            op_name = XPATH_BC_AXIS_DESCENDANT_NAME;
            op_wild = XPATH_BC_AXIS_DESCENDANT_WILD;
            break;
        case XPATH_AXIS_DESCENDANT_OR_SELF:
            op_name = XPATH_BC_AXIS_DESCENDANT_OR_SELF_NAME;
            op_wild = XPATH_BC_AXIS_DESCENDANT_OR_SELF_WILD;
            break;
        default:
            return 0;  /* ancestor / following / etc. stay on BC_AXIS_STEP */
    }

    /* Emit the axis opcode. */
    if (has_wild) {
        emit_op(st, op_wild);
    } else {
        emit_op_u16(st, op_name, add_const_string(st, test->value));
    }

    /* Emit one predicate opcode per simple predicate. The VM applies
     * them in order, which matches XPath semantics for chained
     * predicates ([@a][@b] = has-attr-a AND has-attr-b). */
    for (size_t i = 0; i < pred_count; i++) {
        XPathASTNode* pred = step->children[1 + i];
        const char *a, *v;
        long p;
        PredKind kind = classify_predicate(pred, &a, &v, &p);

        switch (kind) {
            case PRED_KIND_ATTR_EXISTS:
                emit_op_u16(st, XPATH_BC_PRED_ATTR_EXISTS,
                            add_const_string(st, a));
                break;
            case PRED_KIND_ATTR_EQ_STRING: {
                uint16_t name_idx = add_const_string(st, a);
                uint16_t value_idx = add_const_string(st, v);
                /* Encode the pair as two consecutive u16 operands
                 * in the instruction stream. */
                if (reserve_code(st, 5) == 0) {
                    st->bc->code[st->bc->code_len++] = (unsigned char)XPATH_BC_PRED_ATTR_EQ_STRING;
                    st->bc->code[st->bc->code_len++] = (name_idx >> 8) & 0xFF;
                    st->bc->code[st->bc->code_len++] = name_idx & 0xFF;
                    st->bc->code[st->bc->code_len++] = (value_idx >> 8) & 0xFF;
                    st->bc->code[st->bc->code_len++] = value_idx & 0xFF;
                }
                break;
            }
            case PRED_KIND_POSITION:
                /* XPath positions are 1-based; cap at 255 to fit in u8.
                 * Larger positions are rare; fall back to BC_AXIS_STEP
                 * would require re-issuing the axis — not worth it. */
                if (p >= 1 && p <= 255) {
                    emit_op_u8(st, XPATH_BC_PRED_POSITION, (uint8_t)p);
                } else {
                    /* Predicates were classified as simple but we
                     * can't encode this one. Bail — restart with
                     * BC_AXIS_STEP. This branch is unreachable in
                     * practice (classify gates to >= 1). */
                    return 0;
                }
                break;
            default:
                return 0;  /* unreachable due to gating above */
        }
    }

    return 1;
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
            if (!try_compile_specialized_axis(st, child)) {
                emit_op_u16(st, XPATH_BC_AXIS_STEP, add_const_ast(st, child));
            }
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
            if (!try_compile_specialized_axis(st, node)) {
                emit_op_u16(st, XPATH_BC_AXIS_STEP, add_const_ast(st, node));
            }
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
