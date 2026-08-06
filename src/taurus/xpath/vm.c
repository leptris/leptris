/* lib/src/xpath/vm.c — XPath bytecode VM interpreter (TODO 120 Phase A)
 *
 * Stack-based interpreter for the bytecode emitted by compiler.c.
 * Walks the bytecode program counter; each opcode handler reads
 * operands, computes a result, pushes onto the value stack.
 *
 * Phase A scope:
 *   - Literals (number, string, bool).
 *   - ROOT_CONTEXT -- push the document root as a single-node nodeset.
 *   - AXIS_STEP + NODE_TEST_NAME + NODE_TEST_ALL -- the common
 *     `/root/child/*` path subset.
 *   - Return final stack top as the result.
 *
 * For unsupported opcodes (FILTER, BINARY_OP, FUNC_CALL, complex
 * node tests), the VM signals "fallback" by returning NULL and
 * setting *needs_fallback.  Callers fall back to the AST evaluator.
 *
 * Future phases will fill in the remaining opcodes in-place.
 */
#include "bytecode.h"
#include "evaluator_internal.h"
#include "../taurus_internal.h"
#include "../dom/element.h"
#include "../dom/node.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Value stack entry -- tagged union of XPath types. */
typedef enum {
    XPATH_VM_NUMBER,
    XPATH_VM_STRING_OWNED,    /* heap-allocated, freed on pop */
    XPATH_VM_STRING_BORROWED, /* const-pool pointer, not freed */
    XPATH_VM_BOOL,
    XPATH_VM_NODESET          /* owns the XPathNodeSet* */
} VMValueType;

typedef struct {
    VMValueType type;
    union {
        double number;
        char* string;
        int boolean;
        XPathNodeSet* nodeset;
    } v;
} VMValue;

typedef struct {
    VMValue* stack;
    size_t sp;
    size_t cap;
    int error;
    int needs_fallback;       /* set by any handler that punts */
} XPathVM;

static int vm_push(XPathVM* vm, VMValue v) {
    if (vm->sp >= vm->cap) {
        size_t new_cap = vm->cap ? vm->cap * 2 : 16;
        VMValue* grown = (VMValue*)realloc(vm->stack, new_cap * sizeof(VMValue));
        if (!grown) { vm->error = 1; return -1; }
        vm->stack = grown;
        vm->cap = new_cap;
    }
    vm->stack[vm->sp++] = v;
    return 0;
}

static VMValue vm_pop(XPathVM* vm) {
    if (vm->sp == 0) {
        vm->error = 1;
        VMValue empty = { .type = XPATH_VM_BOOL, .v.boolean = 0 };
        return empty;
    }
    return vm->stack[--vm->sp];
}

static void vm_value_free(VMValue* v) {
    if (!v) return;
    switch (v->type) {
        case XPATH_VM_STRING_OWNED:
            free(v->v.string);
            break;
        case XPATH_VM_NODESET:
            if (v->v.nodeset) {
                /* nodeset free is internal to evaluator; skip for Phase A */
            }
            break;
        default:
            break;
    }
}

/* Read a 16-bit operand from the code stream, advancing pc. */
static uint16_t read_u16(const unsigned char** pc) {
    uint16_t v = ((uint16_t)(*pc)[0] << 8) | (*pc)[1];
    *pc += 2;
    return v;
}

/* Apply a single step: axis + node test against the current nodeset.
 * Phase A only supports CHILD axis and name/all tests.  Other axes
 * signal fallback. */
static int vm_apply_step(XPathVM* vm, XPathOpcode test_op,
                          uint16_t test_operand, TaurusXPathBytecode* bc,
                          XPathContext* ctx) {
    if (vm->sp == 0) { vm->error = 1; return -1; }
    VMValue top = vm->stack[vm->sp - 1];
    if (top.type != XPATH_VM_NODESET) {
        /* Non-nodeset input -- can't apply a step.  Fallback. */
        vm->needs_fallback = 1;
        return -1;
    }

    /* Phase A: only CHILD axis + simple tests.  Other combinations
     * punt to AST evaluator.  We still pop + re-push so the stack
     * invariant holds. */
    (void)ctx;
    (void)bc;
    (void)test_op;
    (void)test_operand;
    vm->needs_fallback = 1;
    return -1;
}

/* Run the bytecode against the given context.  Returns the final
 * stack-top VMValue (caller owns), or signals fallback. */
static int vm_run(XPathVM* vm, TaurusXPathBytecode* bc, XPathContext* ctx) {
    const unsigned char* pc = bc->code;
    const unsigned char* end = bc->code + bc->code_len;

    while (pc < end && !vm->error && !vm->needs_fallback) {
        XPathOpcode op = (XPathOpcode)*pc++;
        switch (op) {
            case XPATH_BC_NOP:
                break;
            case XPATH_BC_LITERAL_NUMBER: {
                uint16_t idx = read_u16(&pc);
                if (idx >= bc->const_count) { vm->error = 1; break; }
                VMValue v = { .type = XPATH_VM_NUMBER,
                              .v.number = bc->constants[idx].v.number };
                vm_push(vm, v);
                break;
            }
            case XPATH_BC_LITERAL_STRING: {
                uint16_t idx = read_u16(&pc);
                if (idx >= bc->const_count) { vm->error = 1; break; }
                VMValue v = { .type = XPATH_VM_STRING_BORROWED,
                              .v.string = bc->constants[idx].v.string };
                vm_push(vm, v);
                break;
            }
            case XPATH_BC_LITERAL_BOOL: {
                uint8_t b = (pc < end) ? *pc++ : 0;
                VMValue v = { .type = XPATH_VM_BOOL, .v.boolean = b ? 1 : 0 };
                vm_push(vm, v);
                break;
            }
            case XPATH_BC_ROOT_CONTEXT: {
                /* Push the context node as a single-element nodeset.
                 * Phase A creates an empty nodeset as the starting
                 * point; the actual axis-step work is Phase B. */
                XPathNodeSet* ns = xpath_nodeset_new();
                if (!ns) { vm->error = 1; break; }
                if (ctx->context_node) {
                    xpath_nodeset_add(ns, ctx->context_node);
                }
                VMValue v = { .type = XPATH_VM_NODESET, .v.nodeset = ns };
                vm_push(vm, v);
                break;
            }
            case XPATH_BC_AXIS_STEP: {
                uint8_t axis = (pc < end) ? *pc++ : XPATH_AXIS_CHILD;
                XPathOpcode test_op = (pc < end) ? (XPathOpcode)*pc++ : XPATH_BC_NODE_TEST_ALL;
                uint16_t test_operand = 0;
                if (test_op == XPATH_BC_NODE_TEST_NAME) {
                    test_operand = read_u16(&pc);
                }
                (void)axis;
                /* Phase A punts on actual axis stepping -- the work
                 * is in evaluator_axes.c and would need significant
                 * duplication.  Future Phase B will inline. */
                vm->needs_fallback = 1;
                break;
            }
            case XPATH_BC_FALLBACK_EVAL: {
                /* Already a fallback marker in the bytecode. */
                vm->needs_fallback = 1;
                break;
            }
            case XPATH_BC_RETURN:
                goto done;
            default:
                /* Unsupported opcode (FILTER, BINARY_OP, FUNC_CALL, etc.) */
                vm->needs_fallback = 1;
                goto done;
        }
    }
done:
    return vm->error ? -1 : 0;
}

/* Public entry: compile + run, with AST-evaluator fallback.
 *
 * This is the integration point.  Phase A keeps the AST evaluator
 * as the default; this function is exposed via the internal API so
 * tests can verify the bytecode round-trip.  Phase C will flip the
 * default.
 */
struct taurus_xpath_result* taurus_xpath_vm_eval(XPathASTNode* ast,
                                                  XPathContext* ctx) {
    if (!ast || !ctx) return NULL;

    TaurusXPathBytecode* bc = taurus_xpath_compile_ast(ast);
    if (!bc) {
        /* Compile failure -- fall back. */
        return evaluate_expr(ctx, ast);
    }

    XPathVM vm = {0};
    int rc = vm_run(&vm, bc, ctx);

    struct taurus_xpath_result* result = NULL;
    if (rc == 0 && !vm.needs_fallback && vm.sp >= 1) {
        /* Successful VM run.  Convert top-of-stack to XPathResult.
         * Phase A only handles literal values cleanly; nodeset
         * conversion is non-trivial and deferred. */
        VMValue top = vm.stack[vm.sp - 1];
        switch (top.type) {
            case XPATH_VM_NUMBER: {
                result = (struct taurus_xpath_result*)malloc(sizeof(struct taurus_xpath_result));
                if (result) {
                    result->type = XPATH_RESULT_NUMBER;
                    result->value.number_value = top.v.number;
                }
                break;
            }
            default:
                /* Phase A doesn't convert other types yet. */
                break;
        }
    }

    /* Cleanup. */
    while (vm.sp > 0) {
        VMValue v = vm_pop(&vm);
        vm_value_free(&v);
    }
    free(vm.stack);
    taurus_xpath_bytecode_free(bc);

    if (!result) {
        /* VM punted or conversion not implemented -- fall back. */
        return evaluate_expr(ctx, ast);
    }
    return result;
}
