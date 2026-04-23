#include <stdbool.h>
#include <stdio.h>

static void generate_preamble(FILE *out_file)
{
    fprintf(out_file, "global _start\n");
    fprintf(out_file, "\n");
    fprintf(out_file, "_start:\n");
    fprintf(out_file, "    call main\n");
    fprintf(out_file, "    mov rdi, rax\n");
    fprintf(out_file, "    mov rax, 60\n");
    fprintf(out_file, "    syscall\n");
    fprintf(out_file, "\n");
}

static bool generate_asm_for_fn(AST_Node *fn_node, FILE *out_file)
{
    if (!fn_node || fn_node->kind != AST_FN) {
        fprintf(stderr, "codegen: internal error: expected AST_FN\n");
        return false;
    }

    fprintf(out_file, "%.*s:\n", PRINT_STRING(fn_node->fn.name));

    for (AST_Node *stmt = fn_node->fn.body.first; stmt != NULL; stmt = stmt->next) {
        if (stmt->kind != AST_RETURN) {
            fprintf(stderr, "%s:%ld:%ld: codegen error: only return statements are supported at this stage\n",
                    stmt->loc.file_name, stmt->loc.line, stmt->loc.col);
            return false;
        }
        AST_Node *expr = stmt->ret_expr;
        if (!expr || expr->kind != AST_INTEGER) {
            Loc loc = stmt->loc;
            fprintf(stderr, "%s:%ld:%ld: codegen error: return expression must be an integer literal at this stage\n",
                    loc.file_name, loc.line, loc.col);
            return false;
        }
        fprintf(out_file, "    mov rax, %ld\n", expr->int_value);
        fprintf(out_file, "    ret\n");
    }
    fprintf(out_file, "\n");

    return true;
}

bool generate_asm(AST_Node *ast, FILE *out_file)
{
    if (!ast || ast->kind != AST_PROGRAM) {
        fprintf(stderr, "codegen: root AST node was not PROGRAM\n");
        return false;
    }

    generate_preamble(out_file);

    for (AST_Node *fn_node = ast->program.first; fn_node != NULL; fn_node = fn_node->next) {
        if (!generate_asm_for_fn(fn_node, out_file)) return false;
    }

    return true;
}
