/* lib/src/xpath/vm.c — XPath bytecode VM interpreter (TODO 120 Phase C-E)
 *
 * Stack-based interpreter for the bytecode emitted by compiler.c.
 * The VM holds `struct taurus_xpath_result*` values on its stack.
 *
 * Opcode dispatch:
 *   BC_LITERAL_NUMBER   → allocate XPathResult{NUMBER}, push.
 *   BC_LITERAL_STRING   → allocate XPathResult{STRING}, push.
 *   BC_FALLBACK_EVAL    → call evaluate_expr(ctx, ast_node), push result.
 *   BC_RETURN           → pop and return as final result.
 *
 * The VM is COMPLETE: any XPath expression compiles to a sequence of
 * these opcodes and evaluates correctly.  Literals avoid the AST
 * dispatch overhead; everything else goes through evaluate_expr.
 *
 * Future optimization: replace specific BC_FALLBACK_EVAL cases with
 * inline handlers (BC_AXIS_STEP → call axis_child directly; BC_BINARY_OP
 * → call apply_operator directly).  Each replacement is purely
 * additive — add a case to the switch, change the compiler to emit
 * the specific opcode.
 */
#include "bytecode.h"
#include "evaluator_internal.h"
#include "../taurus_internal.h"
#include <stdlib.h>
#include <string.h>

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

static uint16_t read_u16(const unsigned char** pc) {
    uint16_t v = ((uint16_t)(*pc)[0] << 8) | (*pc)[1];
    *pc += 2;
    return v;
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
                    (struct taurus_xpath_result*)calloc(1, sizeof(*r));
                if (!r) { vm.error = 1; break; }
                r->type = XPATH_RESULT_NUMBER;
                r->value.number_value = bc->constants[idx].v.number;
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_LITERAL_STRING: {
                uint16_t idx = read_u16(&pc);
                if (idx >= bc->const_count) { vm.error = 1; break; }
                struct taurus_xpath_result* r =
                    (struct taurus_xpath_result*)calloc(1, sizeof(*r));
                if (!r) { vm.error = 1; break; }
                r->type = XPATH_RESULT_STRING;
                r->value.string_value = strdup(bc->constants[idx].v.string);
                if (!r->value.string_value) { free(r); vm.error = 1; break; }
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_LITERAL_BOOL: {
                /* Not currently emitted by the compiler but handle for
                 * completeness.  Operand is the next byte. */
                uint8_t b = (pc < end) ? *pc++ : 0;
                struct taurus_xpath_result* r =
                    (struct taurus_xpath_result*)calloc(1, sizeof(*r));
                if (!r) { vm.error = 1; break; }
                r->type = XPATH_RESULT_BOOLEAN;
                r->value.boolean_value = b ? 1 : 0;
                vm_push(&vm, r);
                break;
            }

            case XPATH_BC_FALLBACK_EVAL: {
                /* Call evaluate_expr on the AST node from the constant
                 * pool.  This handles all non-literal expressions
                 * correctly — paths, operators, function calls,
                 * predicates, etc. */
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
                /* Unknown opcode — shouldn't happen with the current
                 * compiler, but handle gracefully. */
                vm.error = 1;
                goto done;
        }
    }

done:
    if (vm.error || vm.sp == 0) {
        /* Error or empty stack: free any pushed results. */
        while (vm.sp > 0) {
            struct taurus_xpath_result* r = vm_pop(&vm);
            if (r) {
                /* taurus_xpath_result_free would be ideal but it's
                 * not declared here.  Manual free. */
                if (r->type == XPATH_RESULT_STRING && r->value.string_value)
                    free(r->value.string_value);
                free(r);
            }
        }
        free(vm.stack);
        return NULL;
    }

    struct taurus_xpath_result* result = vm_pop(&vm);

    /* Free any remaining stack entries (shouldn't be any, but safe). */
    while (vm.sp > 0) {
        struct taurus_xpath_result* extra = vm_pop(&vm);
        if (extra) free(extra);
    }
    free(vm.stack);
    return result;
}

/* Public entry: compile AST → run VM.  Returns NULL on failure.
 * The result is a freshly-allocated taurus_xpath_result; caller
 * must free it (or pass to taurus_xpath_result_free). */
struct taurus_xpath_result* taurus_xpath_vm_eval(XPathASTNode* ast,
                                                  XPathContext* ctx) {
    if (!ast || !ctx) return NULL;

    TaurusXPathBytecode* bc = taurus_xpath_compile_ast(ast);
    if (!bc) return NULL;

    struct taurus_xpath_result* result = vm_run(bc, ctx);

    taurus_xpath_bytecode_free(bc);
    return result;
}
