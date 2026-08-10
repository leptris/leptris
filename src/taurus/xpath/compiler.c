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
#include <stdio.h>

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

/* Try to lower a function call to an inline VM opcode (TODO 130).
 * Returns 1 if emitted, 0 if the function isn't in the inline set
 * or has unexpected arg count (caller falls back to BC_FUNC_CALL).
 *
 * For each inlinable function, emits the arg bytecode (via
 * compile_node) followed by the specialized opcode. The VM evals
 * the arg using all the existing axis / predicate optimizations,
 * then applies the function inline.
 *
 * Functions that take an optional arg (string, name, etc.) get the
 * context node as default when no arg is supplied. We approximate
 * this by emitting BC_PATH_RELATIVE (which pushes [context_node])
 * as the implicit arg. */
static int try_compile_inline_function(CompilerState* st, XPathASTNode* node) {
    if (!node || node->type != XPATH_AST_FUNCTION_CALL) return 0;
    const char* name = node->value;
    if (!name) return 0;
    size_t nargs = node->child_count;

    /* Helper: emit bytecode for arg i. */
    #define EMIT_ARG(i) do { \
        if ((i) < nargs && node->children[(i)]) { \
            compile_node(st, node->children[(i)]); \
        } else { \
            /* Implicit context-node arg for optional-arg functions. */ \
            emit_op(st, XPATH_BC_PATH_RELATIVE); \
        } \
    } while (0)

    XPathOpcode op = XPATH_BC_NOP;

    /* No-arg functions. */
    if (strcmp(name, "true") == 0 && nargs == 0) {
        emit_op(st, XPATH_BC_FUNC_TRUE);
        return 1;
    }
    if (strcmp(name, "false") == 0 && nargs == 0) {
        emit_op(st, XPATH_BC_FUNC_FALSE);
        return 1;
    }
    if (strcmp(name, "position") == 0 && nargs == 0) {
        emit_op(st, XPATH_BC_FUNC_POSITION);
        return 1;
    }
    if (strcmp(name, "last") == 0 && nargs == 0) {
        emit_op(st, XPATH_BC_FUNC_LAST);
        return 1;
    }

    /* Required-1-arg functions. */
    if (strcmp(name, "count") == 0 && nargs == 1) {
        EMIT_ARG(0);
        emit_op(st, XPATH_BC_FUNC_COUNT);
        return 1;
    }
    if (strcmp(name, "sum") == 0 && nargs == 1) {
        EMIT_ARG(0);
        emit_op(st, XPATH_BC_FUNC_SUM);
        return 1;
    }
    if (strcmp(name, "not") == 0 && nargs == 1) {
        EMIT_ARG(0);
        emit_op(st, XPATH_BC_FUNC_NOT);
        return 1;
    }

    /* Optional-1-arg functions. */
    if (strcmp(name, "string") == 0 && nargs <= 1) {
        if (nargs == 0) emit_op(st, XPATH_BC_PATH_RELATIVE);
        else EMIT_ARG(0);
        emit_op(st, XPATH_BC_FUNC_STRING);
        return 1;
    }
    if (strcmp(name, "number") == 0 && nargs <= 1) {
        if (nargs == 0) emit_op(st, XPATH_BC_PATH_RELATIVE);
        else EMIT_ARG(0);
        emit_op(st, XPATH_BC_FUNC_NUMBER);
        return 1;
    }
    if (strcmp(name, "boolean") == 0 && nargs == 1) {
        EMIT_ARG(0);
        emit_op(st, XPATH_BC_FUNC_BOOLEAN);
        return 1;
    }
    /* name / local-name / namespace-uri: keep on BC_FUNC_CALL for now.
     * The QName construction (prefix + ":" + local) requires more
     * plumbing than the inline handler saves. TODO future. */

    #undef EMIT_ARG
    (void)op;
    return 0;
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
     * there are predicates; check each is simple.
     *
     * IMPORTANT: position predicates ([N]) are context-sensitive.
     * The semantics are "position within the current step's
     * candidate set, per input context". The BC_PRED_POSITION
     * opcode implements "position within the global result",
     * which is only correct when the input is a single root
     * (absolute-path fusion). For relative paths and most
     * specialized axes, the input may be multi-context, so we
     * must NOT inline position predicates there. Fall back to
     * BC_AXIS_STEP + apply_predicates which handles per-context
     * position correctly. */
    size_t pred_count = step->child_count - 1;
    if (pred_count > 0) {
        for (size_t i = 0; i < pred_count; i++) {
            XPathASTNode* pred = step->children[1 + i];
            const char *a, *v;
            long p;
            PredKind kind = classify_predicate(pred, &a, &v, &p);
            if (kind == PRED_KIND_NONE) {
                return 0;  /* non-simple predicate — fall back */
            }
            if (kind == PRED_KIND_POSITION) {
                return 0;  /* position is per-context — fall back */
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

    /* Fused axis+predicate fast path (TODO 134). For descendant::*
     * and descendant-or-self::* with a single attribute predicate,
     * emit one opcode that the VM can serve directly from the
     * attribute index when input is the document root. */
    if ((axis == XPATH_AXIS_DESCENDANT || axis == XPATH_AXIS_DESCENDANT_OR_SELF) &&
        has_wild && pred_count == 1) {
        const char *a, *v;
        long p;
        PredKind kind = classify_predicate(step->children[1], &a, &v, &p);
        XPathOpcode exists_op, eq_op;
        if (axis == XPATH_AXIS_DESCENDANT) {
            exists_op = XPATH_BC_AXIS_DESCENDANT_WILD_PRED_ATTR_EXISTS;
            eq_op = XPATH_BC_AXIS_DESCENDANT_WILD_PRED_ATTR_EQ_STRING;
        } else {
            exists_op = XPATH_BC_AXIS_DESCENDANT_OR_SELF_WILD_PRED_ATTR_EXISTS;
            eq_op = XPATH_BC_AXIS_DESCENDANT_OR_SELF_WILD_PRED_ATTR_EQ_STRING;
        }
        if (kind == PRED_KIND_ATTR_EXISTS) {
            emit_op_u16(st, exists_op, add_const_string(st, a));
            return 1;
        }
        if (kind == PRED_KIND_ATTR_EQ_STRING) {
            uint16_t name_idx = add_const_string(st, a);
            uint16_t value_idx = add_const_string(st, v);
            if (reserve_code(st, 5) == 0) {
                st->bc->code[st->bc->code_len++] = (unsigned char)eq_op;
                st->bc->code[st->bc->code_len++] = (name_idx >> 8) & 0xFF;
                st->bc->code[st->bc->code_len++] = name_idx & 0xFF;
                st->bc->code[st->bc->code_len++] = (value_idx >> 8) & 0xFF;
                st->bc->code[st->bc->code_len++] = value_idx & 0xFF;
            }
            return 1;
        }
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

/* Forward decl. */
static int try_compile_specialized_axis(CompilerState* st, XPathASTNode* step);

/* Try to compile an ABSOLUTE_PATH root step into a specialized
 * absolute-root opcode (TODO 129). Returns 1 if emitted, 0 on
 * fallback.
 *
 * Match: step is the first child of an ABSOLUTE_PATH, axis is
 * CHILD / DESCENDANT / DESCENDANT_OR_SELF, test is a name (no colon)
 * or wildcard, no predicates. The compiler emits:
 *   - BC_ABSOLUTE_ROOT_MATCH_NAME / WILD          for root-level name
 *   - BC_ABSOLUTE_DESCENDANT_NAME / WILD          for descendant axis
 *   - BC_ABSOLUTE_DESCENDANT_OR_SELF_NAME / WILD  for double-slash
 *     descendant-or-self axis
 *
 * For shapes we don't match (predicate on first step, namespace
 * prefix, multi-axis), caller falls back to BC_FALLBACK_EVAL on
 * the whole ABSOLUTE_PATH. */
static int try_compile_absolute_root_step(CompilerState* st, XPathASTNode* step) {
    if (!step || step->type != XPATH_AST_STEP) return 0;
    if (step->child_count != 1) return 0;  /* has predicates */

    XPathASTNode* test = step->children[0];
    if (!test) return 0;

    int has_name = (test->type == XPATH_AST_NODE_TEST_NAME);
    int has_wild = (test->type == XPATH_AST_NODE_TEST_ALL);
    if (!has_name && !has_wild) return 0;
    if (has_name && (!test->value || strchr(test->value, ':'))) return 0;

    XPathAxisType axis = step->axis_id;
    XPathOpcode op_name, op_wild;

    switch (axis) {
        case XPATH_AXIS_CHILD:
            op_name = XPATH_BC_ABSOLUTE_ROOT_MATCH_NAME;
            op_wild = XPATH_BC_ABSOLUTE_ROOT_MATCH_WILD;
            break;
        case XPATH_AXIS_DESCENDANT:
            op_name = XPATH_BC_ABSOLUTE_DESCENDANT_NAME;
            op_wild = XPATH_BC_ABSOLUTE_DESCENDANT_WILD;
            break;
        case XPATH_AXIS_DESCENDANT_OR_SELF:
            op_name = XPATH_BC_ABSOLUTE_DESCENDANT_OR_SELF_NAME;
            op_wild = XPATH_BC_ABSOLUTE_DESCENDANT_OR_SELF_WILD;
            break;
        default:
            return 0;
    }

    if (has_wild) {
        emit_op(st, op_wild);
    } else {
        emit_op_u16(st, op_name, add_const_string(st, test->value));
    }
    return 1;
}

/* Compile an absolute path. Try to specialize the first step + emit
 * subsequent steps via the regular try_compile_specialized_axis.
 * Falls back to BC_FALLBACK_EVAL on the whole path if the first
 * step doesn't match a specialization shape. */
static void compile_absolute_path(CompilerState* st, XPathASTNode* node) {
    /* The first child may be a STEP directly or wrapped in
     * RELATIVE_PATH. Handle both. */
    if (node->child_count == 0) {
        /* `/` alone — select the document root. */
        emit_op(st, XPATH_BC_ABSOLUTE_ROOT_MATCH_WILD);
        return;
    }

    XPathASTNode* first = node->children[0];
    XPathASTNode* first_step = NULL;
    XPathASTNode* second_step = NULL;
    XPathASTNode** rest = NULL;
    size_t rest_count = 0;

    if (first->type == XPATH_AST_RELATIVE_PATH) {
        if (first->child_count >= 1) first_step = first->children[0];
        if (first->child_count >= 2) second_step = first->children[1];
        if (first->child_count > 2) {
            rest = &first->children[2];
            rest_count = first->child_count - 2;
        }
    } else if (first->type == XPATH_AST_STEP) {
        first_step = first;
        /* The second child of ABSOLUTE_PATH may be a STEP directly
         * or a RELATIVE_PATH wrapping steps. Handle both. */
        if (node->child_count >= 2) {
            XPathASTNode* second = node->children[1];
            if (second->type == XPATH_AST_STEP) {
                second_step = second;
                if (node->child_count > 2) {
                    rest = &node->children[2];
                    rest_count = node->child_count - 2;
                }
            } else if (second->type == XPATH_AST_RELATIVE_PATH) {
                if (second->child_count >= 1) second_step = second->children[0];
                if (second->child_count > 1) {
                    rest = &second->children[1];
                    rest_count = second->child_count - 1;
                }
            }
        }
    } else {
        emit_op_u16(st, XPATH_BC_FALLBACK_EVAL, add_const_ast(st, node));
        return;
    }

    /* Fusion: `//foo` parses to `/descendant-or-self::node()/child::foo`.
     * Detect this two-step shape and lower it to a single
     * BC_ABSOLUTE_DESCENDANT_OR_SELF_NAME / WILD opcode that walks
     * the subtree once. Same for double-slash wildcard.
     *
     * Match: first step is descendant-or-self axis with wildcard or
     * node() test (no predicates), second step is child axis with
     * name (no colon) or wildcard test (no predicates). */
    if (first_step && second_step &&
        first_step->type == XPATH_AST_STEP &&
        first_step->axis_id == XPATH_AXIS_DESCENDANT_OR_SELF &&
        first_step->child_count == 1) {
        XPathASTNode* dstest = first_step->children[0];
        /* descendant-or-self::node()  OR  descendant-or-self::* */
        int ds_is_wild = (dstest && dstest->type == XPATH_AST_NODE_TEST_ALL);
        int ds_is_node = (dstest && dstest->type == XPATH_AST_NODE_TEST_TYPE &&
                          dstest->value && strcmp(dstest->value, "node") == 0);
        if (ds_is_wild || ds_is_node) {
            if (second_step->type == XPATH_AST_STEP &&
                second_step->axis_id == XPATH_AXIS_CHILD &&
                second_step->child_count >= 1) {
                XPathASTNode* ctest = second_step->children[0];
                int c_has_name = (ctest && ctest->type == XPATH_AST_NODE_TEST_NAME &&
                                  ctest->value && !strchr(ctest->value, ':'));
                int c_has_wild = (ctest && ctest->type == XPATH_AST_NODE_TEST_ALL);

                if (c_has_name || c_has_wild) {
                    /* Check predicates on the child step. Position
                     * predicates are context-sensitive even in the
                     * fused absolute-path case (`//title[1]` means
                     * first-title-per-book, not first-title-globally),
                     * so only allow attribute predicates here. */
                    size_t cpred = second_step->child_count - 1;
                    int preds_ok = 1;
                    for (size_t i = 0; i < cpred; i++) {
                        const char *a, *v;
                        long p;
                        PredKind pk = classify_predicate(
                            second_step->children[1 + i], &a, &v, &p);
                        if (pk != PRED_KIND_ATTR_EXISTS &&
                            pk != PRED_KIND_ATTR_EQ_STRING) {
                            preds_ok = 0;
                            break;
                        }
                    }

                    if (preds_ok) {
                        /* Emit the fused opcode. */
                        if (c_has_wild) {
                            emit_op(st, XPATH_BC_ABSOLUTE_DESCENDANT_OR_SELF_WILD);
                        } else {
                            emit_op_u16(st, XPATH_BC_ABSOLUTE_DESCENDANT_OR_SELF_NAME,
                                        add_const_string(st, ctest->value));
                        }
                        /* Emit predicate opcodes. Only attr predicates
                         * are eligible (see preds_ok check above). */
                        for (size_t i = 0; i < cpred; i++) {
                            const char *a, *v;
                            long p;
                            PredKind k = classify_predicate(second_step->children[1 + i],
                                                              &a, &v, &p);
                            if (k == PRED_KIND_ATTR_EXISTS) {
                                emit_op_u16(st, XPATH_BC_PRED_ATTR_EXISTS,
                                            add_const_string(st, a));
                            } else if (k == PRED_KIND_ATTR_EQ_STRING) {
                                uint16_t n = add_const_string(st, a);
                                uint16_t v2 = add_const_string(st, v);
                                if (reserve_code(st, 5) == 0) {
                                    st->bc->code[st->bc->code_len++] = (unsigned char)XPATH_BC_PRED_ATTR_EQ_STRING;
                                    st->bc->code[st->bc->code_len++] = (n >> 8) & 0xFF;
                                    st->bc->code[st->bc->code_len++] = n & 0xFF;
                                    st->bc->code[st->bc->code_len++] = (v2 >> 8) & 0xFF;
                                    st->bc->code[st->bc->code_len++] = v2 & 0xFF;
                                }
                            }
                            /* PRED_KIND_POSITION is excluded by preds_ok. */
                        }
                        /* Then any remaining steps. */
                        for (size_t i = 0; i < rest_count; i++) {
                            XPathASTNode* s = rest[i];
                            if (s && s->type == XPATH_AST_STEP) {
                                if (!try_compile_specialized_axis(st, s)) {
                                    emit_op_u16(st, XPATH_BC_AXIS_STEP, add_const_ast(st, s));
                                }
                            }
                        }
                        return;
                    }
                }
            }
        }
    }

    if (!try_compile_absolute_root_step(st, first_step)) {
        emit_op_u16(st, XPATH_BC_FALLBACK_EVAL, add_const_ast(st, node));
        return;
    }

    /* Subsequent steps use the regular specialized-axis lowering. */
    if (second_step) {
        if (second_step->type == XPATH_AST_STEP) {
            if (!try_compile_specialized_axis(st, second_step)) {
                emit_op_u16(st, XPATH_BC_AXIS_STEP, add_const_ast(st, second_step));
            }
        }
    }
    for (size_t i = 0; i < rest_count; i++) {
        XPathASTNode* step = rest[i];
        if (!step) continue;
        if (step->type == XPATH_AST_STEP) {
            if (!try_compile_specialized_axis(st, step)) {
                emit_op_u16(st, XPATH_BC_AXIS_STEP, add_const_ast(st, step));
            }
        } else if (step->type == XPATH_AST_RELATIVE_PATH) {
            emit_op_u16(st, XPATH_BC_FALLBACK_EVAL, add_const_ast(st, step));
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
            /* Try to specialize the first step (TODO 129); fall back
             * to BC_FALLBACK_EVAL if the shape doesn't match. */
            compile_absolute_path(st, node);
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
            /* Try to emit an inline function opcode (TODO 130).
             * Falls back to BC_FUNC_CALL if the function isn't in
             * the inline set or has unexpected arg count. */
            if (!try_compile_inline_function(st, node)) {
                /* The handler signature takes AST args, so we don't
                 * pre-evaluate them on the stack. Stash the FUNC_CALL
                 * AST in the constant pool; the VM calls
                 * evaluate_function_call(ctx, ast_fc) directly,
                 * skipping the evaluate_expr AST-type switch. */
                emit_op_u16(st, XPATH_BC_FUNC_CALL, add_const_ast(st, node));
            }
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
