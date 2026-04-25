#include <stdbool.h>
#include <stdio.h>

// Translate stack-machine IR (built by ir.c) into x86-64 NASM assembly.
//
// Operand values live on the runtime CPU stack. Binary ops pop their right-
// hand side into rcx and their left-hand side into rax (so the order is the
// reverse of how they were pushed), compute the result in rax, and re-push.
//
// Stage 4 added local variables and a per-function rbp/rsp frame:
//   * IR_FN reserves 8 bytes per `let`-declared local with `sub rsp`.
//   * IR_LOAD_LOCAL / IR_STORE_LOCAL are just `push qword [rbp+off]` /
//     `pop qword [rbp+off]` — the IR carries the signed frame offset
//     directly so we do no arithmetic here.
//   * IR_RETURN tears the frame back down before `ret`.
//
// Stage 5 added function calls. Calling convention:
//   * Caller evaluates each argument and leaves it on the operand stack.
//   * IR_CALL emits `call name`, then `add rsp, 8*n_args` to drop the
//     arguments, then `push rax` so the callee's return value becomes the
//     new top operand.
//   * Parameters live in the caller's frame at positive rbp offsets, so the
//     callee accesses them through the same IR_LOAD_LOCAL / IR_STORE_LOCAL
//     ops as locals — only the sign of the offset differs.
//   * IR_DROP backs the operand stack up by 8 bytes and is paired with
//     IR_CALL to implement the `call` statement, which discards the return.
//
// Stage 6 added comparisons (==, !=, <, <=, >, >=), logical && / ||, and
// branching ops (LABEL / JMP / JMP_IF_FALSE) for `if` and `while`.
// Comparisons leave a 0/1 result on the operand stack via the cmp + setCC
// + movzx idiom. Logical AND/OR normalize each operand to 0 or 1 first so
// non-boolean ints (e.g. 2 && 1) still produce a sensible 0/1 result.
// Labels are emitted as NASM `.L<id>:`, which are scoped to the current
// function thanks to NASM's local-label convention.

static void generate_preamble(FILE *out_file)
{
    // The Linux x86-64 program entry point. We call main, then move its return
    // value (rax) into rdi and invoke the exit syscall (syscall number 60).
    fprintf(out_file, "global _start\n");
    fprintf(out_file, "\n");
    fprintf(out_file, "_start:\n");
    fprintf(out_file, "    call main\n");
    fprintf(out_file, "    mov rdi, rax    ; return code\n");
    fprintf(out_file, "    mov rax, 60     ; exit syscall\n");
    fprintf(out_file, "    syscall\n");
    fprintf(out_file, "\n");
}

static void emit_binop_pop_operands(FILE *out_file)
{
    // Right-hand side was pushed last, so it pops first into rcx.
    // Left-hand side pops second into rax.
    fprintf(out_file, "    pop rcx\n");
    fprintf(out_file, "    pop rax\n");
}

// Emit a signed integer comparison that leaves a 0/1 result on the operand
// stack. `set_cc` is the suffix of the setCC instruction (e.g. "l" for <,
// "le" for <=, "e" for ==).
static void emit_compare(FILE *out_file, const char *label, const char *set_cc)
{
    fprintf(out_file, "\n    ; %s\n", label);
    emit_binop_pop_operands(out_file);
    fprintf(out_file, "    cmp rax, rcx\n");
    fprintf(out_file, "    set%s al\n", set_cc);
    fprintf(out_file, "    movzx rax, al\n");
    fprintf(out_file, "    push rax\n");
}

bool generate_asm(const IR_Program *prog, FILE *out_file)
{
    generate_preamble(out_file);

    for (long i = 0; i < prog->count; i++) {
        const IR_Op *op = &prog->items[i];
        switch (op->kind) {
            case IR_FN: {
                fprintf(out_file, "%.*s:\n", PRINT_STRING(op->fn.name));
                // Standard frame prologue: save caller's rbp, set rbp to the
                // current rsp, then carve out room for this function's locals.
                fprintf(out_file, "    push rbp\n");
                fprintf(out_file, "    mov rbp, rsp\n");
                if (op->fn.n_locals > 0) {
                    fprintf(out_file, "    sub rsp, %ld   ; reserve %ld local(s)\n",
                            8 * op->fn.n_locals, op->fn.n_locals);
                }
            } break;

            case IR_END_FN: {
                fprintf(out_file, "\n");
            } break;

            case IR_PUSH: {
                fprintf(out_file, "    push %ld   ; PUSH %ld\n",
                        op->push_value, op->push_value);
            } break;

            case IR_LOAD_LOCAL: {
                // %+ld renders the sign explicitly (e.g. -8 or +24), so the
                // emitted address `[rbp-8]` / `[rbp+24]` is unambiguous.
                fprintf(out_file, "    push qword [rbp%+ld]   ; LOAD_LOCAL\n",
                        op->frame_offset);
            } break;

            case IR_STORE_LOCAL: {
                fprintf(out_file, "    pop qword [rbp%+ld]    ; STORE_LOCAL\n",
                        op->frame_offset);
            } break;

            case IR_ADD: {
                fprintf(out_file, "\n    ; ADD\n");
                emit_binop_pop_operands(out_file);
                fprintf(out_file, "    add rax, rcx\n");
                fprintf(out_file, "    push rax\n");
            } break;

            case IR_SUB: {
                fprintf(out_file, "\n    ; SUB\n");
                emit_binop_pop_operands(out_file);
                fprintf(out_file, "    sub rax, rcx\n");
                fprintf(out_file, "    push rax\n");
            } break;

            case IR_MUL: {
                fprintf(out_file, "\n    ; MUL\n");
                emit_binop_pop_operands(out_file);
                fprintf(out_file, "    imul rcx   ; rax = rax * rcx\n");
                fprintf(out_file, "    push rax\n");
            } break;

            case IR_DIV: {
                fprintf(out_file, "\n    ; DIV\n");
                emit_binop_pop_operands(out_file);
                // idiv divides rdx:rax by its operand. cqo sign-extends rax
                // into rdx so signed division behaves correctly.
                fprintf(out_file, "    cqo\n");
                fprintf(out_file, "    idiv rcx   ; rax = rdx:rax / rcx\n");
                fprintf(out_file, "    push rax\n");
            } break;

            case IR_MOD: {
                fprintf(out_file, "\n    ; MOD\n");
                emit_binop_pop_operands(out_file);
                fprintf(out_file, "    cqo\n");
                fprintf(out_file, "    idiv rcx   ; rdx = rdx:rax %% rcx\n");
                fprintf(out_file, "    mov rax, rdx\n");
                fprintf(out_file, "    push rax\n");
            } break;

            case IR_EQ:  emit_compare(out_file, "EQ",  "e");   break;
            case IR_NEQ: emit_compare(out_file, "NEQ", "ne");  break;
            case IR_LT:  emit_compare(out_file, "LT",  "l");   break;
            case IR_LE:  emit_compare(out_file, "LE",  "le");  break;
            case IR_GT:  emit_compare(out_file, "GT",  "g");   break;
            case IR_GE:  emit_compare(out_file, "GE",  "ge");  break;

            case IR_AND: {
                // Normalize each operand to 0/1 so e.g. (2 && 1) still gives 1.
                fprintf(out_file, "\n    ; AND\n");
                emit_binop_pop_operands(out_file);
                fprintf(out_file, "    cmp rcx, 0\n");
                fprintf(out_file, "    setne cl\n");
                fprintf(out_file, "    movzx rcx, cl\n");
                fprintf(out_file, "    cmp rax, 0\n");
                fprintf(out_file, "    setne al\n");
                fprintf(out_file, "    movzx rax, al\n");
                fprintf(out_file, "    and rax, rcx\n");
                fprintf(out_file, "    push rax\n");
            } break;

            case IR_OR: {
                fprintf(out_file, "\n    ; OR\n");
                emit_binop_pop_operands(out_file);
                fprintf(out_file, "    cmp rcx, 0\n");
                fprintf(out_file, "    setne cl\n");
                fprintf(out_file, "    movzx rcx, cl\n");
                fprintf(out_file, "    cmp rax, 0\n");
                fprintf(out_file, "    setne al\n");
                fprintf(out_file, "    movzx rax, al\n");
                fprintf(out_file, "    or rax, rcx\n");
                fprintf(out_file, "    push rax\n");
            } break;

            case IR_LABEL: {
                fprintf(out_file, ".L%ld:\n", op->label_id);
            } break;

            case IR_JMP: {
                fprintf(out_file, "    jmp .L%ld\n", op->label_id);
            } break;

            case IR_JMP_IF_FALSE: {
                fprintf(out_file, "    pop rax\n");
                fprintf(out_file, "    test rax, rax\n");
                fprintf(out_file, "    jz .L%ld   ; JMP_IF_FALSE\n", op->label_id);
            } break;

            case IR_CALL: {
                fprintf(out_file, "\n    ; CALL %.*s (%ld arg(s))\n",
                        PRINT_STRING(op->call.name), op->call.n_args);
                fprintf(out_file, "    call %.*s\n", PRINT_STRING(op->call.name));
                if (op->call.n_args > 0) {
                    fprintf(out_file, "    add rsp, %ld   ; drop %ld arg(s)\n",
                            8 * op->call.n_args, op->call.n_args);
                }
                fprintf(out_file, "    push rax   ; return value\n");
            } break;

            case IR_DROP: {
                fprintf(out_file, "    add rsp, 8   ; DROP\n");
            } break;

            case IR_RETURN: {
                fprintf(out_file, "\n    ; RETURN\n");
                fprintf(out_file, "    pop rax\n");
                // Tear down the stack frame: discard any locals + leftover
                // operand-stack residue, then restore caller's rbp.
                fprintf(out_file, "    mov rsp, rbp\n");
                fprintf(out_file, "    pop rbp\n");
                fprintf(out_file, "    ret\n");
            } break;
        }
    }

    return true;
}
