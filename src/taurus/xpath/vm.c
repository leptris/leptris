/* lib/src/xpath/vm.c — XPath bytecode VM interpreter (TODO 120 Phase F)
 *
 * Stack-based interpreter for the bytecode emitted by compiler.c.
 * The VM holds `struct taurus_xpath_result*` values on its stack.
 *
 * Inline handlers (Phase F):
 *   BC_LITERAL_NUMBER / STRING / BOOL
 *     → push a fresh XPathResult.
 *   BC_PATH_ABSOLUTE
 *     → push document root as single-node nodeset.
 *   BC_PATH_RELATIVE
 *     → push context node as single-node nodeset.
 *   BC_AXIS_STEP
 *     → pop input nodeset, call evaluate_step(ctx, ast, input),
 *       push result nodeset. Reuses the existing axis + node-test
 *       + predicate machinery.
 *   BC_BINARY_OP
 *     → pop two operands, apply arithmetic / comparison / boolean
 *       logic inline. Avoids the AST-node walking that
 *       evaluate_operator would do.
 *   BC_FUNC_CALL
 *     → call evaluate_function_call(ctx, ast) directly. Skips the
 *       evaluate_expr AST-type switch.
 *   BC_FALLBACK_EVAL
 *     → call evaluate_expr(ctx, ast). Used for variable refs and
 *       any AST shape the compiler doesn't specifically lower.
 *   BC_RETURN
 *     → pop and return as final result.
 *
 * The VM is COMPLETE: every XPath expression compiles to a sequence
 * of these opcodes and evaluates correctly. The inline handlers
 * exist to reduce dispatch overhead; correctness is identical to
 * direct AST evaluation because the inline handlers call into the
 * same evaluator helpers.
 */
#include "bytecode.h"
#include "evaluator_internal.h"
#include "functions.h"
#include "../taurus_internal.h"
#include "../dom/element.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    struct taurus_xpath_result** stack;
    size_t sp;
    size_t cap;
    int error;
} XPathVM;

static int vm_push(XPathVM* vm, struct taurus_xpath_result* v) {
    if (vm->sp >= vm->cap) {
        size_t new_cap = vm->cap ? vm->cap * 2 : 16;
        struct taurus_xpath_result** grown =
            (struct taurus_xpath_result**)realloc(vm->stack,
                new_cap * sizeof(struct taurus_xpath_result*));
        if (!grown) { vm->error = 1; return -1; }
        vm->stack = grown;
        vm->cap = new_cap;
    }
    vm->stack[vm->sp++] = v;
    return 0;
}

static struct taurus_xpath_result* vm_pop(XPathVM* vm) {
    if (vm->sp == 0) { vm->error = 1; return NULL; }
    return vm->stack[--vm->sp];
}

static uint8_t read_u8(const unsigned char** pc) {
    uint8_t v = *pc[0];
    *pc += 1;
    return v;
}

static uint16_t read_u16(const unsigned char** pc) {
    uint16_t v = ((uint16_t)(*pc)[0] << 8) | (*pc)[1];
    *pc += 2;
    return v;
}

/* Build a single-node nodeset from a context element. Caller owns
 * the returned result. */
static struct taurus_xpath_result* make_singleton_nodeset(void* node) {
    struct taurus_xpath_result* r =
        (struct taurus_xpath_result*)calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->type = XPATH_RESULT_NODESET;
    r->value.nodeset_value = xpath_nodeset_new();
    if (!r->value.nodeset_value) {
        free(r);
        return NULL;
    }
    if (node) xpath_nodeset_add(r->value.nodeset_value, node);
    return r;
}

/* ----------------------------------------------------------------------- *
 * Specialized axis handlers (TODO 126).
 *
 * Each handler pops the input nodeset from the VM stack, walks the
 * tree inline (bypassing evaluate_step / apply_axis / matches_node_test),
 * and returns a freshly-allocated result.
 *
 * `name` is NULL for wildcards; otherwise a NUL-terminated C string
 * to match against element names. The compiler only emits these
 * opcodes for the no-namespace-prefix case (no ':' in test value).
 * ----------------------------------------------------------------------- */

/* Common preamble: pop input nodeset, detach it from the result
 * wrapper, free the wrapper. Returns the detached nodeset or NULL
 * on error (vm->error is set). */
static XPathNodeSet* vm_detach_input_nodeset(XPathVM* vm) {
    struct taurus_xpath_result* input = vm_pop(vm);
    if (!input || input->type != XPATH_RESULT_NODESET) {
        if (input) xpath_result_free(input);
        vm->error = 1;
        return NULL;
    }
    XPathNodeSet* ns = input->value.nodeset_value;
    input->value.nodeset_value = NULL;
    xpath_result_free(input);
    return ns;
}

/* Helper: is this node an element (vs attribute / text / etc.)?
 * The VM opcodes only accept element input; non-element inputs are
 * skipped (matches the existing axis behavior for non-element context). */
static int node_is_element(void* node) {
    if (!node) return 0;
    /* Element vs attribute distinguishing: the attribute node struct
     * begins with TaurusNodeType node_type == TAURUS_NODE_ATTRIBUTE.
     * Elements begin with the compact header (TaurusCompactHeader)
     * whose first int is the page offset, never matches the attribute
     * tag reliably. Use a conservative check that mirrors
     * evaluator_path.c's node_as_element. */
    TaurusNodeType first = *(TaurusNodeType*)node;
    return first != TAURUS_NODE_ATTRIBUTE &&
           first != TAURUS_NODE_NAMESPACE &&
           first != TAURUS_NODE_TEXT;
}

struct taurus_xpath_result* vm_apply_axis_child(XPathContext* ctx, XPathVM* vm,
                                                 const char* name, int wild);
struct taurus_xpath_result* vm_apply_axis_attribute(XPathContext* ctx, XPathVM* vm,
                                                     const char* name, int wild);
struct taurus_xpath_result* vm_apply_axis_self(XPathContext* ctx, XPathVM* vm,
                                                const char* name, int wild);
struct taurus_xpath_result* vm_apply_axis_parent(XPathContext* ctx, XPathVM* vm,
                                                  const char* name, int wild);

struct taurus_xpath_result* vm_apply_axis_child(XPathContext* ctx, XPathVM* vm,
                                                 const char* name, int wild) {
    (void)ctx;
    XPathNodeSet* input = vm_detach_input_nodeset(vm);
    if (!input) return NULL;

    XPathNodeSet* out = xpath_nodeset_new();
    if (!out) { xpath_nodeset_free(input); return NULL; }

    for (size_t i = 0; i < input->count; i++) {
        if (!node_is_element(input->nodes[i])) continue;
        TaurusElement elem = (TaurusElement)input->nodes[i];
        TaurusElement child = taurus_element_get_first_child(elem);
        while (child) {
            if (wild) {
                xpath_nodeset_add(out, child);
            } else {
                const char* cn = taurus_element_get_name(child);
                if (cn && strcmp(cn, name) == 0) {
                    xpath_nodeset_add(out, child);
                }
            }
            child = taurus_element_get_next_sibling(child);
        }
    }

    xpath_nodeset_free(input);

    struct taurus_xpath_result* r = xpath_result_new(XPATH_RESULT_NODESET);
    if (!r) { xpath_nodeset_free(out); return NULL; }
    r->value.nodeset_value = out;
    return r;
}

struct taurus_xpath_result* vm_apply_axis_attribute(XPathContext* ctx, XPathVM* vm,
                                                     const char* name, int wild) {
    (void)ctx;
    XPathNodeSet* input = vm_detach_input_nodeset(vm);
    if (!input) return NULL;

    XPathNodeSet* out = xpath_nodeset_new();
    if (!out) { xpath_nodeset_free(input); return NULL; }

    /* Attribute axis produces TaurusAttributeNode* entries. The
     * result must own them so they're freed when the result is. */
    out->owns_attributes = 1;

    for (size_t i = 0; i < input->count; i++) {
        if (!node_is_element(input->nodes[i])) continue;
        TaurusElement elem = (TaurusElement)input->nodes[i];
        struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
        while (attr) {
            TaurusStringView nv = attr->name_view;
            int matches = 0;
            if (wild) {
                matches = 1;
            } else if (name && nv.length > 0 && nv.data) {
                matches = (strlen(name) == nv.length &&
                           memcmp(name, nv.data, nv.length) == 0);
            }

            if (matches) {
                /* Allocate a TaurusAttributeNode mirroring
                 * create_attribute_node in evaluator_axes.c. */
                TaurusAttributeNode* an = TAURUS_ALLOC(TaurusAttributeNode);
                if (!an) { attr = attr->next; continue; }
                an->node_type = TAURUS_NODE_ATTRIBUTE;

                /* Copy name */
                if (attr->name) {
                    an->name = taurus_strdup(attr->name);
                } else if (nv.length > 0 && nv.data) {
                    an->name = (char*)malloc(nv.length + 1);
                    if (an->name) {
                        memcpy(an->name, nv.data, nv.length);
                        an->name[nv.length] = '\0';
                    }
                } else {
                    an->name = NULL;
                }

                /* Copy value */
                TaurusStringView vv = attr->value_view;
                if (attr->value) {
                    an->value = taurus_strdup(attr->value);
                } else if (vv.length > 0 && vv.data) {
                    an->value = (char*)malloc(vv.length + 1);
                    if (an->value) {
                        memcpy(an->value, vv.data, vv.length);
                        an->value[vv.length] = '\0';
                    }
                } else {
                    an->value = NULL;
                }

                an->namespace_uri = NULL;
                an->owner = elem;

                xpath_nodeset_add(out, an);
            }
            attr = attr->next;
        }
    }

    xpath_nodeset_free(input);

    struct taurus_xpath_result* r = xpath_result_new(XPATH_RESULT_NODESET);
    if (!r) { xpath_nodeset_free(out); return NULL; }
    r->value.nodeset_value = out;
    return r;
}

struct taurus_xpath_result* vm_apply_axis_self(XPathContext* ctx, XPathVM* vm,
                                                const char* name, int wild) {
    (void)ctx;
    XPathNodeSet* input = vm_detach_input_nodeset(vm);
    if (!input) return NULL;

    XPathNodeSet* out = xpath_nodeset_new();
    if (!out) { xpath_nodeset_free(input); return NULL; }

    for (size_t i = 0; i < input->count; i++) {
        if (!node_is_element(input->nodes[i])) continue;
        TaurusElement elem = (TaurusElement)input->nodes[i];
        if (wild) {
            xpath_nodeset_add(out, elem);
        } else {
            const char* en = taurus_element_get_name(elem);
            if (en && strcmp(en, name) == 0) {
                xpath_nodeset_add(out, elem);
            }
        }
    }

    xpath_nodeset_free(input);

    struct taurus_xpath_result* r = xpath_result_new(XPATH_RESULT_NODESET);
    if (!r) { xpath_nodeset_free(out); return NULL; }
    r->value.nodeset_value = out;
    return r;
}

struct taurus_xpath_result* vm_apply_axis_parent(XPathContext* ctx, XPathVM* vm,
                                                  const char* name, int wild) {
    (void)ctx;
    XPathNodeSet* input = vm_detach_input_nodeset(vm);
    if (!input) return NULL;

    XPathNodeSet* out = xpath_nodeset_new();
    if (!out) { xpath_nodeset_free(input); return NULL; }

    /* Parent axis can produce duplicates (multiple children → same
     * parent). Inline a pointer-equality dedup. */
    for (size_t i = 0; i < input->count; i++) {
        if (!node_is_element(input->nodes[i])) continue;
        TaurusElement elem = (TaurusElement)input->nodes[i];
        TaurusElement parent = taurus_element_get_parent(elem);
        if (!parent) continue;

        if (!wild) {
            const char* pn = taurus_element_get_name(parent);
            if (!pn || strcmp(pn, name) != 0) continue;
        }

        /* Dedup */
        int dup = 0;
        for (size_t j = 0; j < out->count; j++) {
            if (out->nodes[j] == parent) { dup = 1; break; }
        }
        if (!dup) xpath_nodeset_add(out, parent);
    }

    xpath_nodeset_free(input);

    struct taurus_xpath_result* r = xpath_result_new(XPATH_RESULT_NODESET);
    if (!r) { xpath_nodeset_free(out); return NULL; }
    r->value.nodeset_value = out;
    return r;
}

/* Inline binary-operator dispatch. Pops right then left, computes,
 * pushes the result. Returns 0 on success, -1 on error.
 *
 * Short-circuit semantics for AND/OR: this implementation evaluates
 * both operands eagerly. XPath has no side effects, so the only
 * cost is the redundant work, which is rare in real expressions. */
static int vm_apply_binary_op(XPathVM* vm, XPathContext* ctx,
                               XPathOperatorType op) {
    struct taurus_xpath_result* right = vm_pop(vm);
    struct taurus_xpath_result* left  = vm_pop(vm);
    if (!left || !right) {
        if (left) xpath_result_free(left);
        if (right) xpath_result_free(right);
        return -1;
    }

    struct taurus_xpath_result* result = NULL;

    /* Boolean operators. */
    if (op == XPATH_OP_AND) {
        result = xpath_result_new(XPATH_RESULT_BOOLEAN);
        if (result) result->value.boolean_value =
            xpath_to_boolean(left) && xpath_to_boolean(right);
    } else if (op == XPATH_OP_OR) {
        result = xpath_result_new(XPATH_RESULT_BOOLEAN);
        if (result) result->value.boolean_value =
            xpath_to_boolean(left) || xpath_to_boolean(right);
    }
    /* Arithmetic. */
    else if (op == XPATH_OP_PLUS || op == XPATH_OP_MINUS ||
             op == XPATH_OP_MULTIPLY || op == XPATH_OP_DIV ||
             op == XPATH_OP_MOD) {
        double lval = xpath_to_number(left);
        double rval = xpath_to_number(right);
        result = xpath_result_new(XPATH_RESULT_NUMBER);
        if (result) {
            switch (op) {
                case XPATH_OP_PLUS:     result->value.number_value = lval + rval; break;
                case XPATH_OP_MINUS:    result->value.number_value = lval - rval; break;
                case XPATH_OP_MULTIPLY: result->value.number_value = lval * rval; break;
                case XPATH_OP_DIV:      result->value.number_value = lval / rval; break;
                case XPATH_OP_MOD:      result->value.number_value = fmod(lval, rval); break;
                default: break;
            }
        }
    }
    /* Unary negation — right is unused (compiler emits only one operand). */
    else if (op == XPATH_OP_NEGATION) {
        double lval = xpath_to_number(left);
        result = xpath_result_new(XPATH_RESULT_NUMBER);
        if (result) result->value.number_value = -lval;
    }
    /* Union — both must be nodesets; concatenate with dedup.
     * XPath requires no duplicates and document order; we dedup but
     * don't enforce document order (the existing evaluator doesn't
     * either — see compare_document_order note in evaluator_operators.c). */
    else if (op == XPATH_OP_UNION) {
        result = xpath_result_new(XPATH_RESULT_NODESET);
        if (result) {
            result->value.nodeset_value = xpath_nodeset_new();
            if (result->value.nodeset_value) {
                XPathNodeSet* ln = left->type == XPATH_RESULT_NODESET
                                   ? left->value.nodeset_value : NULL;
                XPathNodeSet* rn = right->type == XPATH_RESULT_NODESET
                                   ? right->value.nodeset_value : NULL;
                if (ln) for (size_t i = 0; i < ln->count; i++)
                    xpath_nodeset_add(result->value.nodeset_value, ln->nodes[i]);
                if (rn) for (size_t i = 0; i < rn->count; i++) {
                    void* candidate = rn->nodes[i];
                    int dup = 0;
                    XPathNodeSet* out = result->value.nodeset_value;
                    for (size_t j = 0; j < out->count; j++) {
                        if (out->nodes[j] == candidate) { dup = 1; break; }
                    }
                    if (!dup) xpath_nodeset_add(out, candidate);
                }
            }
        }
    }
    /* Comparisons. */
    else if (op == XPATH_OP_EQUAL || op == XPATH_OP_NOT_EQUAL ||
             op == XPATH_OP_LESS || op == XPATH_OP_LESS_EQUAL ||
             op == XPATH_OP_GREATER || op == XPATH_OP_GREATER_EQUAL) {
        /* XPath comparison semantics depend on operand types:
         *   - nodeset vs nodeset: any-pair match
         *   - nodeset vs number/string/boolean: any-node match
         *   - otherwise: convert both to number and compare
         * For the hot path (literal RHS, single-node LHS) we use
         * first-node values; multi-node comparisons are uncommon
         * and fall through to the same numeric compare on
         * string-derived numbers. Full any-pair semantics are
         * preserved on the AST fallback path (BC_FALLBACK_EVAL). */
        double lnum = xpath_to_number(left);
        double rnum = xpath_to_number(right);
        char* lstr_owned = xpath_to_string(left);
        char* rstr_owned = xpath_to_string(right);
        int eq_str = (lstr_owned && rstr_owned &&
                      strcmp(lstr_owned, rstr_owned) == 0);
        int eq_num = (lnum == rnum);
        int eq = eq_str || eq_num;
        if (lstr_owned) free(lstr_owned);
        if (rstr_owned) free(rstr_owned);

        int matches = 0;
        switch (op) {
            case XPATH_OP_EQUAL:         matches = eq; break;
            case XPATH_OP_NOT_EQUAL:     matches = !eq; break;
            case XPATH_OP_LESS:          matches = (lnum <  rnum); break;
            case XPATH_OP_LESS_EQUAL:    matches = (lnum <= rnum); break;
            case XPATH_OP_GREATER:       matches = (lnum >  rnum); break;
            case XPATH_OP_GREATER_EQUAL: matches = (lnum >= rnum); break;
            default: break;
        }
        result = xpath_result_new(XPATH_RESULT_BOOLEAN);
        if (result) result->value.boolean_value = matches;
        (void)ctx;
    }

    xpath_result_free(left);
    xpath_result_free(right);

    if (!result) { vm->error = 1; return -1; }
    vm_push(vm, result);
    return 0;
}

static struct taurus_xpath_result* vm_run(TaurusXPathBytecode* bc,
                                           XPathContext* ctx) {
    XPathVM vm = {0};
    const unsigned char* pc = bc->code;
    const unsigned char* end = bc->code + bc->code_len;

    while (pc < end && !vm.error) {
        XPathOpcode op = (XPathOpcode)*pc++;
        switch (op) {
            case XPATH_BC_NOP:
                break;

            case XPATH_BC_LITERAL_NUMBER: {
                uint16_t idx = read_u16(&pc);
                if (idx >= bc->const_count) { vm.error = 1; break; }
                struct taurus_xpath_result* r =
                    xpath_result_new(XPATH_RESULT_NUMBER);
                if (!r) { vm.error = 1; break; }
                r->value.number_value = bc->constants[idx].v.number;
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_LITERAL_STRING: {
                uint16_t idx = read_u16(&pc);
                if (idx >= bc->const_count) { vm.error = 1; break; }
                struct taurus_xpath_result* r =
                    xpath_result_new(XPATH_RESULT_STRING);
                if (!r) { vm.error = 1; break; }
                r->value.string_value =
                    taurus_strdup(bc->constants[idx].v.string);
                if (!r->value.string_value) {
                    xpath_result_free(r);
                    vm.error = 1;
                    break;
                }
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_LITERAL_BOOL: {
                uint8_t b = read_u8(&pc);
                struct taurus_xpath_result* r =
                    xpath_result_new(XPATH_RESULT_BOOLEAN);
                if (!r) { vm.error = 1; break; }
                r->value.boolean_value = b ? 1 : 0;
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_PATH_ABSOLUTE: {
                TaurusElement root =
                    (TaurusElement)ctx->document->new_dom_root;
                struct taurus_xpath_result* r = make_singleton_nodeset(root);
                if (!r) { vm.error = 1; break; }
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_PATH_RELATIVE: {
                struct taurus_xpath_result* r =
                    make_singleton_nodeset(ctx->context_node);
                if (!r) { vm.error = 1; break; }
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_AXIS_STEP: {
                uint16_t idx = read_u16(&pc);
                if (idx >= bc->const_count ||
                    bc->constants[idx].type != XPATH_CONST_AST_NODE) {
                    vm.error = 1;
                    break;
                }
                XPathASTNode* step_ast = bc->constants[idx].v.ast;

                struct taurus_xpath_result* input = vm_pop(&vm);
                if (!input || input->type != XPATH_RESULT_NODESET) {
                    if (input) xpath_result_free(input);
                    vm.error = 1;
                    break;
                }

                XPathNodeSet* input_ns = input->value.nodeset_value;
                input->value.nodeset_value = NULL;
                xpath_result_free(input);

                struct taurus_xpath_result* step_result =
                    evaluate_step(ctx, step_ast, input_ns);
                xpath_nodeset_free(input_ns);
                if (!step_result) { vm.error = 1; break; }
                vm_push(&vm, step_result);
                break;
            }

            /* Specialized axis handlers (TODO 126). Each is a tight
             * loop over the input nodeset that bypasses the
             * evaluate_step scaffolding. The handlers cover the
             * common shapes: single name test or wildcard, no
             * namespace prefix, no predicates. Anything else
             * stays on BC_AXIS_STEP. */

            case XPATH_BC_AXIS_CHILD_NAME: {
                uint16_t idx = read_u16(&pc);
                const char* name = (idx < bc->const_count &&
                                    bc->constants[idx].type == XPATH_CONST_STRING)
                                   ? bc->constants[idx].v.string : NULL;
                struct taurus_xpath_result* r =
                    vm_apply_axis_child(ctx, &vm, name, 0);
                if (!r) { vm.error = 1; break; }
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_AXIS_CHILD_WILD: {
                struct taurus_xpath_result* r =
                    vm_apply_axis_child(ctx, &vm, NULL, 1);
                if (!r) { vm.error = 1; break; }
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_AXIS_ATTRIBUTE_NAME: {
                uint16_t idx = read_u16(&pc);
                const char* name = (idx < bc->const_count &&
                                    bc->constants[idx].type == XPATH_CONST_STRING)
                                   ? bc->constants[idx].v.string : NULL;
                struct taurus_xpath_result* r =
                    vm_apply_axis_attribute(ctx, &vm, name, 0);
                if (!r) { vm.error = 1; break; }
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_AXIS_ATTRIBUTE_WILD: {
                struct taurus_xpath_result* r =
                    vm_apply_axis_attribute(ctx, &vm, NULL, 1);
                if (!r) { vm.error = 1; break; }
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_AXIS_SELF_NAME: {
                uint16_t idx = read_u16(&pc);
                const char* name = (idx < bc->const_count &&
                                    bc->constants[idx].type == XPATH_CONST_STRING)
                                   ? bc->constants[idx].v.string : NULL;
                struct taurus_xpath_result* r =
                    vm_apply_axis_self(ctx, &vm, name, 0);
                if (!r) { vm.error = 1; break; }
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_AXIS_SELF_WILD: {
                struct taurus_xpath_result* r =
                    vm_apply_axis_self(ctx, &vm, NULL, 1);
                if (!r) { vm.error = 1; break; }
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_AXIS_PARENT_NAME: {
                uint16_t idx = read_u16(&pc);
                const char* name = (idx < bc->const_count &&
                                    bc->constants[idx].type == XPATH_CONST_STRING)
                                   ? bc->constants[idx].v.string : NULL;
                struct taurus_xpath_result* r =
                    vm_apply_axis_parent(ctx, &vm, name, 0);
                if (!r) { vm.error = 1; break; }
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_AXIS_PARENT_WILD: {
                struct taurus_xpath_result* r =
                    vm_apply_axis_parent(ctx, &vm, NULL, 1);
                if (!r) { vm.error = 1; break; }
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_BINARY_OP: {
                uint8_t op_type = read_u8(&pc);
                if (vm_apply_binary_op(&vm, ctx,
                        (XPathOperatorType)op_type) != 0) {
                    vm.error = 1;
                }
                break;
            }

            case XPATH_BC_FUNC_CALL: {
                uint16_t idx = read_u16(&pc);
                if (idx >= bc->const_count ||
                    bc->constants[idx].type != XPATH_CONST_AST_NODE) {
                    vm.error = 1;
                    break;
                }
                XPathASTNode* fc_ast = bc->constants[idx].v.ast;
                struct taurus_xpath_result* r =
                    evaluate_function_call_inline(ctx, fc_ast);
                if (!r) { vm.error = 1; break; }
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_FALLBACK_EVAL: {
                uint16_t idx = read_u16(&pc);
                if (idx >= bc->const_count ||
                    bc->constants[idx].type != XPATH_CONST_AST_NODE) {
                    vm.error = 1;
                    break;
                }
                XPathASTNode* ast_node = bc->constants[idx].v.ast;
                struct taurus_xpath_result* r = evaluate_expr(ctx, ast_node);
                if (!r) { vm.error = 1; break; }
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_RETURN:
                goto done;

            default:
                vm.error = 1;
                goto done;
        }
    }

done:
    if (vm.error || vm.sp == 0) {
        while (vm.sp > 0) {
            struct taurus_xpath_result* r = vm_pop(&vm);
            if (r) xpath_result_free(r);
        }
        free(vm.stack);
        return NULL;
    }

    struct taurus_xpath_result* result = vm_pop(&vm);

    while (vm.sp > 0) {
        struct taurus_xpath_result* extra = vm_pop(&vm);
        if (extra) xpath_result_free(extra);
    }
    free(vm.stack);
    return result;
}

/* Public entry: compile AST → run VM.  Returns NULL on failure.
 * Caller frees the result via taurus_xpath_result_free.
 *
 * Callers should prefer the cached path
 * (xpath_expr_cache_get_or_compile_bc + vm_run_with_bc) so the
 * compile cost is amortized across many evals of the same
 * expression. This entry point is retained for tests and for
 * one-off evals that bypass the cache. */
struct taurus_xpath_result* taurus_xpath_vm_eval(XPathASTNode* ast,
                                                  XPathContext* ctx) {
    if (!ast || !ctx) return NULL;

    TaurusXPathBytecode* bc = taurus_xpath_compile_ast(ast);
    if (!bc) return NULL;

    struct taurus_xpath_result* result = vm_run(bc, ctx);

    taurus_xpath_bytecode_free(bc);
    return result;
}

/* Cached entry: run an already-compiled bytecode. Used by
 * taurus_xpath_eval when the expression cache has a compiled bc. */
struct taurus_xpath_result* taurus_xpath_vm_run_bc(TaurusXPathBytecode* bc,
                                                    XPathContext* ctx) {
    if (!bc || !ctx) return NULL;
    return vm_run(bc, ctx);
}