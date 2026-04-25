#include <stdio.h>
#include <stdlib.h>

// Stack machine intermediate representation. Sits between the AST (built by
// parser.c) and the x86-64 emitter (codegen.c). Every expression is lowered to
// a post-order sequence of PUSHes and binary ops; the runtime stack holds
// operand values while a function is executing.

typedef enum IR_Op_Kind
{
    IR_FN,
    IR_END_FN,
    IR_PUSH,
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_DIV,
    IR_MOD,
    IR_RETURN,
} IR_Op_Kind;

typedef struct IR_Op
{
    IR_Op_Kind kind;
    union {
        long   push_value;
        String fn_name;
    };
} IR_Op;

typedef struct IR_Program
{
    IR_Op *items;
    long   count;
    long   capacity;
} IR_Program;

static const char *ir_op_kind_name(IR_Op_Kind kind)
{
    switch (kind) {
        case IR_FN:     return "FN";
        case IR_END_FN: return "END_FN";
        case IR_PUSH:   return "PUSH";
        case IR_ADD:    return "ADD";
        case IR_SUB:    return "SUB";
        case IR_MUL:    return "MUL";
        case IR_DIV:    return "DIV";
        case IR_MOD:    return "MOD";
        case IR_RETURN: return "RETURN";
    }
    return "?";
}

static void ir_program_append(IR_Program *prog, IR_Op op)
{
    if (prog->count >= prog->capacity) {
        long new_cap = prog->capacity == 0 ? 32 : prog->capacity * 2;
        IR_Op *new_items = (IR_Op *)realloc(prog->items, (size_t)new_cap * sizeof(IR_Op));
        if (!new_items) { fprintf(stderr, "ir: out of memory\n"); exit(1); }
        prog->items = new_items;
        prog->capacity = new_cap;
    }
    prog->items[prog->count++] = op;
}

static IR_Op_Kind ir_op_for_binop(Binary_Op op)
{
    switch (op) {
        case BINOP_ADD: return IR_ADD;
        case BINOP_SUB: return IR_SUB;
        case BINOP_MUL: return IR_MUL;
        case BINOP_DIV: return IR_DIV;
        case BINOP_MOD: return IR_MOD;
    }
    return IR_ADD;
}

static void emit_expression(IR_Program *prog, AST_Node *expr)
{
    switch (expr->kind) {
        case AST_INTEGER: {
            IR_Op op = {0};
            op.kind = IR_PUSH;
            op.push_value = expr->int_value;
            ir_program_append(prog, op);
        } break;

        case AST_BINOP: {
            emit_expression(prog, expr->binop.left);
            emit_expression(prog, expr->binop.right);
            IR_Op op = {0};
            op.kind = ir_op_for_binop(expr->binop.op);
            ir_program_append(prog, op);
        } break;

        default: {
            fprintf(stderr, "ir: internal error: unexpected expression kind %s\n",
                    ast_kind_name(expr->kind));
            exit(1);
        } break;
    }
}

static void emit_statement(IR_Program *prog, AST_Node *stmt)
{
    switch (stmt->kind) {
        case AST_RETURN: {
            emit_expression(prog, stmt->ret_expr);
            IR_Op op = {0};
            op.kind = IR_RETURN;
            ir_program_append(prog, op);
        } break;

        default: {
            fprintf(stderr, "ir: internal error: unexpected statement kind %s\n",
                    ast_kind_name(stmt->kind));
            exit(1);
        } break;
    }
}

static void emit_function(IR_Program *prog, AST_Node *fn_node)
{
    IR_Op enter = {0};
    enter.kind = IR_FN;
    enter.fn_name = fn_node->fn.name;
    ir_program_append(prog, enter);

    for (AST_Node *stmt = fn_node->fn.body.first; stmt != NULL; stmt = stmt->next) {
        emit_statement(prog, stmt);
    }

    IR_Op leave = {0};
    leave.kind = IR_END_FN;
    leave.fn_name = fn_node->fn.name;
    ir_program_append(prog, leave);
}

IR_Program ir_from_ast(AST_Node *ast)
{
    IR_Program prog = {0};
    if (!ast || ast->kind != AST_PROGRAM) {
        fprintf(stderr, "ir: root AST node was not PROGRAM\n");
        exit(1);
    }
    for (AST_Node *fn = ast->program.first; fn != NULL; fn = fn->next) {
        emit_function(&prog, fn);
    }
    return prog;
}

void ir_program_free(IR_Program *prog)
{
    free(prog->items);
    prog->items = NULL;
    prog->count = 0;
    prog->capacity = 0;
}

void print_ir(const IR_Program *prog)
{
    for (long i = 0; i < prog->count; i++) {
        const IR_Op *op = &prog->items[i];
        switch (op->kind) {
            case IR_FN:
                printf("FN %.*s\n", PRINT_STRING(op->fn_name));
                break;
            case IR_END_FN:
                printf("END_FN\n");
                break;
            case IR_PUSH:
                printf("PUSH %ld\n", op->push_value);
                break;
            default:
                printf("%s\n", ir_op_kind_name(op->kind));
                break;
        }
    }
}
