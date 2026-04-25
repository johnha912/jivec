#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// Stack-machine intermediate representation. Sits between the AST (built by
// parser.c) and the x86-64 emitter (codegen.c). Every expression is lowered
// to a post-order sequence of PUSHes, binary ops, and (since stage 5) calls.
// The runtime stack holds operand values while a function is executing.
//
// Variable model. Each function carries its own Symbol_Table. Locals (`let`)
// live below the saved rbp at negative byte offsets; parameters live above
// the saved rbp + return address at positive offsets. The IR records the
// signed frame offset directly on every LOAD/STORE, so codegen just emits
// `[rbp + offset]` without further calculation.
//
// Calling convention. Caller evaluates arguments left-to-right, pushing each
// onto the operand stack. IR_CALL emits the actual `call`, then cleans up
// the n_args words and pushes the callee's return value (rax) back as a new
// operand. The matching `call` statement is `IR_CALL` followed by `IR_DROP`,
// which discards the unwanted return value.

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
    IR_EQ,
    IR_NEQ,
    IR_LT,
    IR_LE,
    IR_GT,
    IR_GE,
    IR_AND,
    IR_OR,
    IR_LABEL,
    IR_JMP,
    IR_JMP_IF_FALSE,
    IR_CALL,
    IR_DROP,
    IR_RETURN,
} IR_Op_Kind;

typedef struct IR_Op
{
    IR_Op_Kind kind;
    union {
        long push_value;
        long frame_offset;       // for IR_LOAD_LOCAL / IR_STORE_LOCAL
        long label_id;           // for IR_LABEL / IR_JMP / IR_JMP_IF_FALSE
        struct {
            String name;
            long   n_locals;     // valid only on IR_FN; backpatched after the body
        } fn;
        struct {
            String name;
            long   n_args;
        } call;
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
    long          next_label_id;   // monotonic counter for if/while branch labels
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
        case IR_EQ:           return "EQ";
        case IR_NEQ:          return "NEQ";
        case IR_LT:           return "LT";
        case IR_LE:           return "LE";
        case IR_GT:           return "GT";
        case IR_GE:           return "GE";
        case IR_AND:          return "AND";
        case IR_OR:           return "OR";
        case IR_LABEL:        return "LABEL";
        case IR_JMP:          return "JMP";
        case IR_JMP_IF_FALSE: return "JMP_IF_FALSE";
        case IR_CALL:         return "CALL";
        case IR_DROP:         return "DROP";
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
        case BINOP_EQ:  return IR_EQ;
        case BINOP_NEQ: return IR_NEQ;
        case BINOP_LT:  return IR_LT;
        case BINOP_LE:  return IR_LE;
        case BINOP_GT:  return IR_GT;
        case BINOP_GE:  return IR_GE;
        case BINOP_AND: return IR_AND;
        case BINOP_OR:  return IR_OR;
    }
    return IR_ADD;
}

static void emit_expression(IR_Builder *builder, AST_Node *expr);

// Walk the call's argument list, lowering each argument expression in turn.
// After this returns, exactly `args.count` operands sit on the operand stack
// and an IR_CALL with the matching n_args will balance back to one value.
static void emit_call_args(IR_Builder *builder, AST_List args)
{
    for (AST_Node *arg = args.first; arg != NULL; arg = arg->next) {
        emit_expression(builder, arg);
        if (builder->has_error) return;
    }
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
            Symbol_Data *sym = lookup_symbol(builder->symbols, expr->ident_name);
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
            op.frame_offset = symbol_frame_offset(builder->symbols, sym);
            ir_program_append(builder->prog, op);
        } break;

        case AST_BINOP: {
            emit_expression(builder, expr->binop.left);
            emit_expression(builder, expr->binop.right);
            IR_Op op = {0};
            op.kind = ir_op_for_binop(expr->binop.op);
            ir_program_append(builder->prog, op);
        } break;

        case AST_CALL: {
            emit_call_args(builder, expr->call.args);
            if (builder->has_error) return;
            IR_Op op = {0};
            op.kind = IR_CALL;
            op.call.name = expr->call.name;
            op.call.n_args = expr->call.args.count;
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
            Symbol_Data *sym = declare_local(builder->symbols,
                                             stmt->let.name, stmt->let.type, stmt->loc);
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
                op.frame_offset = symbol_frame_offset(builder->symbols, sym);
                ir_program_append(builder->prog, op);
            }
        } break;

        case AST_SET: {
            Symbol_Data *sym = lookup_symbol(builder->symbols, stmt->set.name);
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
            op.frame_offset = symbol_frame_offset(builder->symbols, sym);
            ir_program_append(builder->prog, op);
        } break;

        case AST_CALL_STMT: {
            // A `call` statement is just an IR_CALL whose return value we
            // immediately discard so the operand stack stays balanced.
            emit_call_args(builder, stmt->call.args);
            if (builder->has_error) return;
            IR_Op call_op = {0};
            call_op.kind = IR_CALL;
            call_op.call.name = stmt->call.name;
            call_op.call.n_args = stmt->call.args.count;
            ir_program_append(builder->prog, call_op);

            IR_Op drop_op = {0};
            drop_op.kind = IR_DROP;
            ir_program_append(builder->prog, drop_op);
        } break;

        case AST_RETURN: {
            emit_expression(builder, stmt->ret_expr);
            if (builder->has_error) return;
            IR_Op op = {0};
            op.kind = IR_RETURN;
            ir_program_append(builder->prog, op);
        } break;

        case AST_BLOCK: {
            // A block is a flat sequence of statements at the same scope.
            // We do not introduce a new scope here, so any `let` declared
            // inside the block lives in the enclosing function's symbol
            // table — matching the language's flat per-function scoping.
            for (AST_Node *s = stmt->block.first; s != NULL; s = s->next) {
                emit_statement(builder, s);
            }
        } break;

        case AST_IF: {
            //   <cond>
            //   JMP_IF_FALSE  L_else_or_end
            //   <then>
            //   [JMP L_end          ; only when there is an else clause]
            //   [L_else: <else>]
            //   L_end:
            long else_or_end = builder->next_label_id++;
            emit_expression(builder, stmt->if_stmt.cond);
            if (builder->has_error) return;

            IR_Op jmp_if_false = {0};
            jmp_if_false.kind = IR_JMP_IF_FALSE;
            jmp_if_false.label_id = else_or_end;
            ir_program_append(builder->prog, jmp_if_false);

            emit_statement(builder, stmt->if_stmt.then_branch);

            if (stmt->if_stmt.else_branch) {
                long end_label = builder->next_label_id++;
                IR_Op jmp_end = {0};
                jmp_end.kind = IR_JMP;
                jmp_end.label_id = end_label;
                ir_program_append(builder->prog, jmp_end);

                IR_Op else_label = {0};
                else_label.kind = IR_LABEL;
                else_label.label_id = else_or_end;
                ir_program_append(builder->prog, else_label);

                emit_statement(builder, stmt->if_stmt.else_branch);

                IR_Op end = {0};
                end.kind = IR_LABEL;
                end.label_id = end_label;
                ir_program_append(builder->prog, end);
            } else {
                IR_Op end = {0};
                end.kind = IR_LABEL;
                end.label_id = else_or_end;
                ir_program_append(builder->prog, end);
            }
        } break;

        case AST_WHILE: {
            //   L_top: <cond>
            //          JMP_IF_FALSE L_end
            //          <body>
            //          JMP L_top
            //   L_end:
            long top = builder->next_label_id++;
            long end = builder->next_label_id++;

            IR_Op top_label = {0};
            top_label.kind = IR_LABEL;
            top_label.label_id = top;
            ir_program_append(builder->prog, top_label);

            emit_expression(builder, stmt->while_stmt.cond);
            if (builder->has_error) return;

            IR_Op jmp_if_false = {0};
            jmp_if_false.kind = IR_JMP_IF_FALSE;
            jmp_if_false.label_id = end;
            ir_program_append(builder->prog, jmp_if_false);

            emit_statement(builder, stmt->while_stmt.body);

            IR_Op jmp_top = {0};
            jmp_top.kind = IR_JMP;
            jmp_top.label_id = top;
            ir_program_append(builder->prog, jmp_top);

            IR_Op end_label = {0};
            end_label.kind = IR_LABEL;
            end_label.label_id = end;
            ir_program_append(builder->prog, end_label);
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
    // Each function gets a fresh symbol table so names don't leak between
    // functions. Parameters are declared first (they live in the caller's
    // frame), then locals get assigned slots as `let` statements appear in
    // the body. We patch IR_FN's `n_locals` once the body is lowered.
    Symbol_Table table = make_symbol_table(16);

    long fn_op_index = builder->prog->count;
    IR_Op enter = {0};
    enter.kind = IR_FN;
    enter.fn.name = fn_node->fn.name;
    enter.fn.n_locals = 0;
    ir_program_append(builder->prog, enter);

    Symbol_Table *prev_symbols = builder->symbols;
    builder->symbols = &table;

    // Set n_params up front so symbol_frame_offset works even if a duplicate
    // parameter name aborts one of the inserts below.
    table.n_params = fn_node->fn.parameters.count;
    long param_index = 0;
    for (AST_Node *p = fn_node->fn.parameters.first; p != NULL; p = p->next) {
        Symbol_Data *sym = declare_param(&table, p->param.name, p->param.type,
                                         param_index, p->loc);
        if (!sym) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "parameter '%.*s' has already been declared in this scope",
                     PRINT_STRING(p->param.name));
            ir_report_error(builder, p->loc, msg);
        }
        param_index++;
    }

    for (AST_Node *stmt = fn_node->fn.body.first; stmt != NULL; stmt = stmt->next) {
        emit_statement(builder, stmt);
        // Keep going on error so multiple problems can be reported per
        // function; the driver bails out before codegen if has_error is set.
    }

    builder->prog->items[fn_op_index].fn.n_locals = table.n_locals;

    builder->symbols = prev_symbols;
    free_symbol_table(&table);

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
                printf("LOAD_LOCAL [rbp%+ld]\n", op->frame_offset);
                break;
            case IR_STORE_LOCAL:
                printf("STORE_LOCAL [rbp%+ld]\n", op->frame_offset);
                break;
            case IR_CALL:
                printf("CALL %.*s (args=%ld)\n",
                       PRINT_STRING(op->call.name), op->call.n_args);
                break;
            case IR_LABEL:
                printf("LABEL .L%ld\n", op->label_id);
                break;
            case IR_JMP:
                printf("JMP .L%ld\n", op->label_id);
                break;
            case IR_JMP_IF_FALSE:
                printf("JMP_IF_FALSE .L%ld\n", op->label_id);
                break;
            default:
                printf("%s\n", ir_op_kind_name(op->kind));
                break;
        }
    }
}
