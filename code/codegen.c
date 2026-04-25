#include <stdbool.h>
#include <stdio.h>

// Translate stack-machine IR (built by ir.c) into x86-64 NASM assembly.
// Operand values live on the runtime CPU stack. Binary ops pop their right-
// hand side into rcx and their left-hand side into rax (so the order is the
// reverse of how they were pushed), compute the result in rax, and re-push.

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

bool generate_asm(const IR_Program *prog, FILE *out_file)
{
    generate_preamble(out_file);

    for (long i = 0; i < prog->count; i++) {
        const IR_Op *op = &prog->items[i];
        switch (op->kind) {
            case IR_FN: {
                fprintf(out_file, "%.*s:\n", PRINT_STRING(op->fn_name));
            } break;

            case IR_END_FN: {
                fprintf(out_file, "\n");
            } break;

            case IR_PUSH: {
                fprintf(out_file, "    push %ld   ; PUSH %ld\n",
                        op->push_value, op->push_value);
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

            case IR_RETURN: {
                fprintf(out_file, "\n    ; RETURN\n");
                fprintf(out_file, "    pop rax\n");
                fprintf(out_file, "    ret\n");
            } break;
        }
    }

    return true;
}
