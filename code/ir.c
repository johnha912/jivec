#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// Stack-machine intermediate representation. Sits between the AST (built by
// parser.c) and the x86-64 emitter (codegen.c). Every expression is lowered
// to a post-order sequence of PUSHes and binary ops; the runtime stack
// holds operand values while a function is executing.
//
// Stage 4 added local variables. Each function carries its own
// Symbol_Table, which assigns a stack slot to every `let`-declared name.
// Identifier references and `set` statements are checked against that
// table — undeclared or redeclared names raise a semantic error here,
// before any assembly is emitted.

typedef enum IR_Op_Kind
{
    IR_FN,
    IR_END_FN,
    IR_PUSH,
    IR_LOAD_LOCAL,
    IR_STORE_LOCAL,
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
        long push_value;
        long slot;
        struct {
            String name;
            long   n_locals;   // valid only on IR_FN; backpatched after the body
        } fn;
    };
} IR_Op;

typedef struct IR_Program
{
    IR_Op *items;
    long   count;
    long   capacity;
} IR_Program;

typedef struct IR_Result
{
    IR_Program program;
    bool       success;
} IR_Result;

typedef struct IR_Builder
{
    IR_Program   *prog;
    Symbol_Table *symbols;
    bool          has_error;
} IR_Builder;

static const char *ir_op_kind_name(IR_Op_Kind kind)
{
    switch (kind) {
        case IR_FN:           return "FN";
        case IR_END_FN:       return "END_FN";
        case IR_PUSH:         return "PUSH";
        case IR_LOAD_LOCAL:   return "LOAD_LOCAL";
        case IR_STORE_LOCAL:  return "STORE_LOCAL";
        case IR_ADD:          return "ADD";
        case IR_SUB:          return "SUB";
        case IR_MUL:          return "MUL";
        case IR_DIV:          return "DIV";
        case IR_MOD:          return "MOD";
        case IR_RETURN:       return "RETURN";
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

static void ir_report_error(IR_Builder *builder, Loc loc, const char *msg)
{
    fprintf(stderr, "%s:%ld:%ld: error: %s\n", loc.file_name, loc.line, loc.col, msg);
    builder->has_error = true;
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

static void emit_expression(IR_Builder *builder, AST_Node *expr)
{
    switch (expr->kind) {
        case AST_INTEGER: {
            IR_Op op = {0};
            op.kind = IR_PUSH;
            op.push_value = expr->int_value;
            ir_program_append(builder->prog, op);
        } break;

        case AST_IDENT: {
            Symbol *sym = symbol_table_lookup(builder->symbols, expr->ident_name);
            if (!sym) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "'%.*s' has not been declared",
                         PRINT_STRING(expr->ident_name));
                ir_report_error(builder, expr->loc, msg);
                return;
            }
            IR_Op op = {0};
            op.kind = IR_LOAD_LOCAL;
            op.slot = sym->slot;
            ir_program_append(builder->prog, op);
        } break;

        case AST_BINOP: {
            emit_expression(builder, expr->binop.left);
            emit_expression(builder, expr->binop.right);
            IR_Op op = {0};
            op.kind = ir_op_for_binop(expr->binop.op);
            ir_program_append(builder->prog, op);
        } break;

        default: {
            fprintf(stderr, "ir: internal error: unexpected expression kind %s\n",
                    ast_kind_name(expr->kind));
            exit(1);
        } break;
    }
}

static void emit_statement(IR_Builder *builder, AST_Node *stmt)
{
    switch (stmt->kind) {
        case AST_LET: {
            // Reserve the slot first so the initializer can reference earlier
            // variables, then run the initializer and pop into the new slot.
            Symbol *sym = symbol_table_declare(builder->symbols,
                                               stmt->let.name, stmt->let.type);
            if (!sym) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "'%.*s' has already been declared in this scope",
                         PRINT_STRING(stmt->let.name));
                ir_report_error(builder, stmt->loc, msg);
                return;
            }
            if (stmt->let.init) {
                emit_expression(builder, stmt->let.init);
                if (builder->has_error) return;
                IR_Op op = {0};
                op.kind = IR_STORE_LOCAL;
                op.slot = sym->slot;
                ir_program_append(builder->prog, op);
            }
        } break;

        case AST_SET: {
            Symbol *sym = symbol_table_lookup(builder->symbols, stmt->set.name);
            if (!sym) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "'%.*s' has not been declared",
                         PRINT_STRING(stmt->set.name));
                ir_report_error(builder, stmt->loc, msg);
                return;
            }
            emit_expression(builder, stmt->set.value);
            if (builder->has_error) return;
            IR_Op op = {0};
            op.kind = IR_STORE_LOCAL;
            op.slot = sym->slot;
            ir_program_append(builder->prog, op);
        } break;

        case AST_RETURN: {
            emit_expression(builder, stmt->ret_expr);
            if (builder->has_error) return;
            IR_Op op = {0};
            op.kind = IR_RETURN;
            ir_program_append(builder->prog, op);
        } break;

        default: {
            fprintf(stderr, "ir: internal error: unexpected statement kind %s\n",
                    ast_kind_name(stmt->kind));
            exit(1);
        } break;
    }
}

static void emit_function(IR_Builder *builder, AST_Node *fn_node)
{
    // Each function gets a fresh symbol table so locals don't leak between
    // functions. We emit IR_FN immediately and patch its `n_locals` field
    // once the body has been lowered and we know the final slot count.
    Symbol_Table table = {0};
    symbol_table_init(&table);

    long fn_op_index = builder->prog->count;
    IR_Op enter = {0};
    enter.kind = IR_FN;
    enter.fn.name = fn_node->fn.name;
    enter.fn.n_locals = 0;
    ir_program_append(builder->prog, enter);

    Symbol_Table *prev_symbols = builder->symbols;
    builder->symbols = &table;

    for (AST_Node *stmt = fn_node->fn.body.first; stmt != NULL; stmt = stmt->next) {
        emit_statement(builder, stmt);
        // Keep going on error so that we can report multiple problems per
        // function; the driver bails out before codegen if has_error is set.
    }

    builder->prog->items[fn_op_index].fn.n_locals = table.next_slot;

    builder->symbols = prev_symbols;
    symbol_table_free(&table);

    IR_Op leave = {0};
    leave.kind = IR_END_FN;
    leave.fn.name = fn_node->fn.name;
    ir_program_append(builder->prog, leave);
}

IR_Result ir_from_ast(AST_Node *ast)
{
    IR_Result result = {0};
    if (!ast || ast->kind != AST_PROGRAM) {
        fprintf(stderr, "ir: root AST node was not PROGRAM\n");
        exit(1);
    }

    IR_Builder builder = {0};
    builder.prog = &result.program;

    for (AST_Node *fn = ast->program.first; fn != NULL; fn = fn->next) {
        emit_function(&builder, fn);
    }

    result.success = !builder.has_error;
    return result;
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
                printf("FN %.*s (locals=%ld)\n",
                       PRINT_STRING(op->fn.name), op->fn.n_locals);
                break;
            case IR_END_FN:
                printf("END_FN\n");
                break;
            case IR_PUSH:
                printf("PUSH %ld\n", op->push_value);
                break;
            case IR_LOAD_LOCAL:
                printf("LOAD_LOCAL %ld\n", op->slot);
                break;
            case IR_STORE_LOCAL:
                printf("STORE_LOCAL %ld\n", op->slot);
                break;
            default:
                printf("%s\n", ir_op_kind_name(op->kind));
                break;
        }
    }
}
