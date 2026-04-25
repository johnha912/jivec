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
//
// Stage 7 added strings, dynamic memory, and built-ins. The runtime
// preamble defines `print`, `print_int`, `print_nl`, and `alloc` as
// hand-written NASM functions following our calling convention; the
// compiler simply emits regular IR_CALLs to them. String literals get
// interned in IR_Program.strings and are emitted at the end of the file
// in `.data` as length-prefixed records (`dq <len>` then `db <bytes>`).
// IR_PUSH_STR loads the literal's address into rax and pushes it onto the
// operand stack. Indexing assumes 8-byte slots, so IR_LOAD_INDEX and
// IR_STORE_INDEX scale the index by 8.

// Hand-written runtime built-ins. They follow the same calling convention
// as user functions: caller pushes args, args at [rbp+16+...], result in rax,
// caller cleans up. We use raw Linux x86-64 syscalls so the produced ELF
// has no libc dependency — `nasm -felf64 ... | ld` is enough.
//
//   sys_write = 1 (rdi=fd, rsi=buf, rdx=count)
//   sys_mmap  = 9 (rdi=addr, rsi=len, rdx=prot, r10=flags, r8=fd, r9=offset)
//   sys_exit  = 60 (rdi=code)
static void emit_runtime(FILE *out_file)
{
    // print(s: str) — s points at a length-prefixed string: 8 bytes of
    // length followed by the raw bytes.
    fprintf(out_file, "print:\n");
    fprintf(out_file, "    push rbp\n");
    fprintf(out_file, "    mov rbp, rsp\n");
    fprintf(out_file, "    mov rsi, [rbp+16]    ; rsi = string struct\n");
    fprintf(out_file, "    mov rdx, [rsi]       ; rdx = length\n");
    fprintf(out_file, "    add rsi, 8           ; rsi = bytes\n");
    fprintf(out_file, "    mov rax, 1           ; sys_write\n");
    fprintf(out_file, "    mov rdi, 1           ; stdout\n");
    fprintf(out_file, "    syscall\n");
    fprintf(out_file, "    xor rax, rax\n");
    fprintf(out_file, "    mov rsp, rbp\n");
    fprintf(out_file, "    pop rbp\n");
    fprintf(out_file, "    ret\n\n");

    // print_nl() — write a single '\\n'.
    fprintf(out_file, "print_nl:\n");
    fprintf(out_file, "    push rbp\n");
    fprintf(out_file, "    mov rbp, rsp\n");
    fprintf(out_file, "    sub rsp, 8\n");
    fprintf(out_file, "    mov byte [rbp-1], 10\n");
    fprintf(out_file, "    mov rax, 1\n");
    fprintf(out_file, "    mov rdi, 1\n");
    fprintf(out_file, "    lea rsi, [rbp-1]\n");
    fprintf(out_file, "    mov rdx, 1\n");
    fprintf(out_file, "    syscall\n");
    fprintf(out_file, "    xor rax, rax\n");
    fprintf(out_file, "    mov rsp, rbp\n");
    fprintf(out_file, "    pop rbp\n");
    fprintf(out_file, "    ret\n\n");

    // print_int(n: int) — convert to decimal in a small stack buffer, then
    // write the resulting bytes. Handles negative numbers and zero.
    fprintf(out_file, "print_int:\n");
    fprintf(out_file, "    push rbp\n");
    fprintf(out_file, "    mov rbp, rsp\n");
    fprintf(out_file, "    sub rsp, 32         ; buffer [rbp-32 .. rbp-1]\n");
    fprintf(out_file, "    mov rax, [rbp+16]\n");
    fprintf(out_file, "    xor r8, r8           ; sign flag\n");
    fprintf(out_file, "    test rax, rax\n");
    fprintf(out_file, "    jns .pi_abs\n");
    fprintf(out_file, "    neg rax\n");
    fprintf(out_file, "    mov r8, 1\n");
    fprintf(out_file, ".pi_abs:\n");
    fprintf(out_file, "    lea rcx, [rbp-1]     ; write digits right-to-left\n");
    fprintf(out_file, "    mov r9, 10\n");
    fprintf(out_file, ".pi_loop:\n");
    fprintf(out_file, "    xor rdx, rdx\n");
    fprintf(out_file, "    div r9               ; rax /= 10, rdx = digit\n");
    fprintf(out_file, "    add dl, '0'\n");
    fprintf(out_file, "    mov [rcx], dl\n");
    fprintf(out_file, "    dec rcx\n");
    fprintf(out_file, "    test rax, rax\n");
    fprintf(out_file, "    jnz .pi_loop\n");
    fprintf(out_file, "    test r8, r8\n");
    fprintf(out_file, "    jz .pi_no_sign\n");
    fprintf(out_file, "    mov byte [rcx], '-'\n");
    fprintf(out_file, "    dec rcx\n");
    fprintf(out_file, ".pi_no_sign:\n");
    fprintf(out_file, "    inc rcx              ; rcx = first byte to write\n");
    fprintf(out_file, "    mov rdx, rbp\n");
    fprintf(out_file, "    sub rdx, rcx         ; bytes = (rbp-1 - rcx) + 1 = rbp - rcx\n");
    fprintf(out_file, "    mov rsi, rcx\n");
    fprintf(out_file, "    mov rax, 1\n");
    fprintf(out_file, "    mov rdi, 1\n");
    fprintf(out_file, "    syscall\n");
    fprintf(out_file, "    xor rax, rax\n");
    fprintf(out_file, "    mov rsp, rbp\n");
    fprintf(out_file, "    pop rbp\n");
    fprintf(out_file, "    ret\n\n");

    // alloc(item_count: int) -> [any] — mmap an anonymous private region of
    // 8 * item_count bytes and return the pointer in rax.
    fprintf(out_file, "alloc:\n");
    fprintf(out_file, "    push rbp\n");
    fprintf(out_file, "    mov rbp, rsp\n");
    fprintf(out_file, "    xor rdi, rdi         ; addr = NULL (kernel chooses)\n");
    fprintf(out_file, "    mov rsi, [rbp+16]\n");
    fprintf(out_file, "    shl rsi, 3           ; len = item_count * 8\n");
    fprintf(out_file, "    mov rdx, 3           ; PROT_READ | PROT_WRITE\n");
    fprintf(out_file, "    mov r10, 0x22        ; MAP_PRIVATE | MAP_ANONYMOUS\n");
    fprintf(out_file, "    mov r8, -1           ; fd\n");
    fprintf(out_file, "    xor r9, r9           ; offset\n");
    fprintf(out_file, "    mov rax, 9           ; sys_mmap\n");
    fprintf(out_file, "    syscall\n");
    fprintf(out_file, "    mov rsp, rbp\n");
    fprintf(out_file, "    pop rbp\n");
    fprintf(out_file, "    ret\n\n");
}

static void generate_preamble(FILE *out_file)
{
    fprintf(out_file, "section .text\n");
    fprintf(out_file, "global _start\n");
    fprintf(out_file, "\n");
    // The Linux x86-64 program entry point. We call main, then move its return
    // value (rax) into rdi and invoke the exit syscall (syscall number 60).
    fprintf(out_file, "_start:\n");
    fprintf(out_file, "    call main\n");
    fprintf(out_file, "    mov rdi, rax    ; return code\n");
    fprintf(out_file, "    mov rax, 60     ; exit syscall\n");
    fprintf(out_file, "    syscall\n");
    fprintf(out_file, "\n");
    emit_runtime(out_file);
}

// Decode one Jive escape sequence at *p[0..]. On success returns the number
// of input bytes consumed (1 for a non-escaped byte, 2 for a `\X` escape)
// and writes the decoded byte through `out`. Unknown escapes pass through
// the second character verbatim — easier than failing in codegen.
static int decode_escape(const char *p, long avail, unsigned char *out)
{
    if (avail <= 0) return 0;
    if (p[0] != '\\' || avail < 2) {
        *out = (unsigned char)p[0];
        return 1;
    }
    switch (p[1]) {
        case 'n':  *out = '\n'; break;
        case 't':  *out = '\t'; break;
        case 'r':  *out = '\r'; break;
        case '0':  *out = '\0'; break;
        case '\\': *out = '\\'; break;
        case '"':  *out = '"';  break;
        case '\'': *out = '\''; break;
        default:   *out = (unsigned char)p[1]; break;
    }
    return 2;
}

// Emit a string literal's raw bytes as a sequence of `db` decimal values,
// 16 per line so the .asm stays readable. Returns the decoded byte count.
static long emit_string_data(FILE *out_file, String src)
{
    long byte_count = 0;
    long pos = 0;
    while (pos < src.count) {
        unsigned char b;
        int consumed = decode_escape(src.data + pos, src.count - pos, &b);
        if (byte_count % 16 == 0) {
            if (byte_count != 0) fprintf(out_file, "\n");
            fprintf(out_file, "    db ");
        } else {
            fprintf(out_file, ", ");
        }
        fprintf(out_file, "%u", (unsigned)b);
        byte_count++;
        pos += consumed;
    }
    if (byte_count > 0) fprintf(out_file, "\n");
    return byte_count;
}

static void emit_data_section(const IR_Program *prog, FILE *out_file)
{
    if (prog->strings.count == 0) return;
    fprintf(out_file, "\nsection .data\n");
    for (long i = 0; i < prog->strings.count; i++) {
        // Decode once to get the length, then again to actually emit. The
        // decoded length is what `print` reads at [s] when it runs.
        long len = 0;
        long pos = 0;
        String src = prog->strings.items[i];
        while (pos < src.count) {
            unsigned char b;
            pos += decode_escape(src.data + pos, src.count - pos, &b);
            len++;
        }
        fprintf(out_file, "__str_%ld:\n", i);
        fprintf(out_file, "    dq %ld\n", len);
        if (len > 0) emit_string_data(out_file, src);
    }
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
                // If the body never reached an explicit `return`, the
                // function would fall through into whatever code comes
                // after it — which is almost always a different function
                // and segfaults. Emit a defensive epilogue so void
                // functions return cleanly to the caller.
                fprintf(out_file, "\n    ; END_FN — fallthrough epilogue\n");
                fprintf(out_file, "    xor rax, rax\n");
                fprintf(out_file, "    mov rsp, rbp\n");
                fprintf(out_file, "    pop rbp\n");
                fprintf(out_file, "    ret\n\n");
            } break;

            case IR_PUSH: {
                fprintf(out_file, "    push %ld   ; PUSH %ld\n",
                        op->push_value, op->push_value);
            } break;

            case IR_PUSH_STR: {
                // The string literal lives in `.data`; load its absolute
                // address (which may exceed 32 bits at link time) into rax
                // first, then push.
                fprintf(out_file, "    mov rax, __str_%ld\n", op->string_id);
                fprintf(out_file, "    push rax   ; PUSH_STR\n");
            } break;

            case IR_LOAD_INDEX: {
                fprintf(out_file, "\n    ; LOAD_INDEX\n");
                fprintf(out_file, "    pop rcx               ; index\n");
                fprintf(out_file, "    pop rax               ; array ptr\n");
                fprintf(out_file, "    push qword [rax + rcx*8]\n");
            } break;

            case IR_STORE_INDEX: {
                fprintf(out_file, "\n    ; STORE_INDEX\n");
                fprintf(out_file, "    pop rcx               ; index\n");
                fprintf(out_file, "    pop rax               ; array ptr\n");
                fprintf(out_file, "    pop rdx               ; value\n");
                fprintf(out_file, "    mov [rax + rcx*8], rdx\n");
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

    emit_data_section(prog, out_file);
    return true;
}
