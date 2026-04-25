#include "lexer.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// PRINT_STRING is defined in string.c (unity build).

static const char *type_name_cstr(Type t)
{
    switch (t) {
        case TYPE_INT:  return "int";
        case TYPE_STR:  return "str";
        case TYPE_BOOL: return "bool";
    }
    return "?";
}

static const char *keyword_name_cstr(Keyword kw)
{
    switch (kw) {
        case KEYWORD_FN:     return "fn";
        case KEYWORD_LET:    return "let";
        case KEYWORD_SET:    return "set";
        case KEYWORD_IF:     return "if";
        case KEYWORD_WHILE:  return "while";
        case KEYWORD_CALL:   return "call";
        case KEYWORD_RETURN: return "return";
        case KEYWORD_TRUE:   return "true";
        case KEYWORD_FALSE:  return "false";
    }
    return "?";
}

typedef enum AST_Kind
{
    AST_NONE = 0,
    AST_PROGRAM,
    AST_FN,
    AST_PARAM,
    AST_LET,
    AST_SET,
    AST_RETURN,
    AST_INTEGER,
    AST_IDENT,
    AST_BINOP,
    AST_CALL,         // function call used as an expression (return value kept)
    AST_CALL_STMT,    // `call` statement (return value discarded)
} AST_Kind;

static const char *ast_kind_name(AST_Kind kind)
{
    switch (kind) {
        case AST_NONE:      return "NONE";
        case AST_PROGRAM:   return "PROGRAM";
        case AST_FN:        return "FN";
        case AST_PARAM:     return "PARAM";
        case AST_LET:       return "LET";
        case AST_SET:       return "SET";
        case AST_RETURN:    return "RETURN";
        case AST_INTEGER:   return "INTEGER";
        case AST_IDENT:     return "IDENT";
        case AST_BINOP:     return "BINOP";
        case AST_CALL:      return "CALL";
        case AST_CALL_STMT: return "CALL_STMT";
    }
    return "?";
}

typedef enum Binary_Op
{
    BINOP_ADD,
    BINOP_SUB,
    BINOP_MUL,
    BINOP_DIV,
    BINOP_MOD,
} Binary_Op;

static const char *binop_symbol(Binary_Op op)
{
    switch (op) {
        case BINOP_ADD: return "+";
        case BINOP_SUB: return "-";
        case BINOP_MUL: return "*";
        case BINOP_DIV: return "/";
        case BINOP_MOD: return "%";
    }
    return "?";
}

typedef struct AST_Node AST_Node;

typedef struct AST_List
{
    long count;
    AST_Node *first;
    AST_Node *last;
} AST_List;

typedef struct AST_Fn_Data
{
    String name;
    AST_List parameters;
    bool has_return_type;
    Type return_type;
    AST_List body;
} AST_Fn_Data;

typedef struct AST_Binop_Data
{
    Binary_Op op;
    AST_Node *left;
    AST_Node *right;
} AST_Binop_Data;

typedef struct AST_Let_Data
{
    String    name;
    Type      type;
    AST_Node *init;     // NULL if the declaration has no initializer
} AST_Let_Data;

typedef struct AST_Set_Data
{
    String    name;
    AST_Node *value;
} AST_Set_Data;

typedef struct AST_Param_Data
{
    String name;
    Type   type;
} AST_Param_Data;

typedef struct AST_Call_Data
{
    String   name;
    AST_List args;     // each arg is an expression AST_Node
} AST_Call_Data;

struct AST_Node
{
    AST_Kind kind;
    Loc loc;
    AST_Node *prev;
    AST_Node *next;
    union {
        AST_List       program;
        AST_Fn_Data    fn;
        AST_Param_Data param;
        AST_Node      *ret_expr;
        long           int_value;
        String         ident_name;
        AST_Binop_Data binop;
        AST_Let_Data   let;
        AST_Set_Data   set;
        AST_Call_Data  call;
    };
};

typedef struct Parser
{
    Token_Array tokens;
    long tok_index;
    bool has_error;
} Parser;

typedef struct Parse_Result
{
    AST_Node *ast;
    bool success;
} Parse_Result;

static AST_Node *make_ast_node(AST_Kind kind)
{
    AST_Node *node = (AST_Node *)calloc(1, sizeof(AST_Node));
    if (!node) { fprintf(stderr, "parser: out of memory\n"); exit(1); }
    node->kind = kind;
    return node;
}

static void ast_list_append(AST_List *list, AST_Node *node)
{
    if (!node) return;
    node->prev = list->last;
    node->next = NULL;
    if (list->last) {
        list->last->next = node;
    } else {
        list->first = node;
    }
    list->last = node;
    list->count++;
}

static Token *peek_token(Parser *parser, long offset)
{
    long index = parser->tok_index + offset;
    if (index >= parser->tokens.count) {
        return &parser->tokens.items[parser->tokens.count - 1];
    }
    return &parser->tokens.items[index];
}

static void report_error_at(Parser *parser, Loc loc, const char *msg)
{
    fprintf(stderr, "%s:%ld:%ld: error: %s\n", loc.file_name, loc.line, loc.col, msg);
    parser->has_error = true;
}

static Token *expect_token(Parser *parser, Token_Kind expected_kind)
{
    Token *actual = peek_token(parser, 0);
    if (actual->kind != expected_kind) {
        char msg[128];
        snprintf(msg, sizeof(msg), "expected %s, got %s",
                 token_kind_name(expected_kind), token_kind_name(actual->kind));
        report_error_at(parser, actual->loc, msg);
        return actual;
    }
    parser->tok_index++;
    return actual;
}

static Token *expect_keyword(Parser *parser, Keyword expected_kw)
{
    Token *actual = peek_token(parser, 0);
    if (actual->kind != TOKEN_KEYWORD || actual->keyword != expected_kw) {
        char msg[128];
        snprintf(msg, sizeof(msg), "expected keyword '%s'", keyword_name_cstr(expected_kw));
        report_error_at(parser, actual->loc, msg);
        return actual;
    }
    parser->tok_index++;
    return actual;
}

static AST_Node *parse_expression(Parser *parser);

// Parse a comma-separated list of expressions inside an already-consumed '('
// up to (but not including) the matching ')'. Appends each parsed expression
// to `args`. Returns with the closing ')' still on the token stream so the
// caller can consume it (and report a useful error if it's missing).
static void parse_argument_list(Parser *parser, AST_List *args)
{
    Token *first = peek_token(parser, 0);
    if (first->kind == TOKEN_CLOSE_PAREN) return;

    AST_Node *arg = parse_expression(parser);
    if (parser->has_error) return;
    ast_list_append(args, arg);

    while (peek_token(parser, 0)->kind == TOKEN_COMMA) {
        parser->tok_index++;
        AST_Node *next_arg = parse_expression(parser);
        if (parser->has_error) return;
        ast_list_append(args, next_arg);
    }
}

static AST_Node *parse_primary(Parser *parser)
{
    Token *tok = peek_token(parser, 0);
    if (tok->kind == TOKEN_INTEGER) {
        AST_Node *node = make_ast_node(AST_INTEGER);
        node->loc = tok->loc;
        node->int_value = tok->long_value;
        parser->tok_index++;
        return node;
    }
    if (tok->kind == TOKEN_IDENT) {
        Token *next = peek_token(parser, 1);
        if (next->kind == TOKEN_OPEN_PAREN) {
            // Call expression: ident '(' [ args ] ')'
            AST_Node *node = make_ast_node(AST_CALL);
            node->loc = tok->loc;
            node->call.name = tok->source;
            parser->tok_index += 2; // consume name and '('
            parse_argument_list(parser, &node->call.args);
            if (parser->has_error) return node;
            expect_token(parser, TOKEN_CLOSE_PAREN);
            return node;
        }
        AST_Node *node = make_ast_node(AST_IDENT);
        node->loc = tok->loc;
        node->ident_name = tok->source;
        parser->tok_index++;
        return node;
    }
    if (tok->kind == TOKEN_OPEN_PAREN) {
        parser->tok_index++;
        AST_Node *inner = parse_expression(parser);
        if (parser->has_error) return inner;
        Token *close = peek_token(parser, 0);
        if (close->kind != TOKEN_CLOSE_PAREN) {
            report_error_at(parser, close->loc, "expected ')' to close parenthesized expression");
            return inner;
        }
        parser->tok_index++;
        return inner;
    }
    report_error_at(parser, tok->loc, "expected an expression");
    return NULL;
}

static AST_Node *parse_multiplicative(Parser *parser)
{
    AST_Node *left = parse_primary(parser);
    if (parser->has_error) return left;

    for (;;) {
        Token *tok = peek_token(parser, 0);
        Binary_Op op;
        if      (tok->kind == TOKEN_STAR)    op = BINOP_MUL;
        else if (tok->kind == TOKEN_SLASH)   op = BINOP_DIV;
        else if (tok->kind == TOKEN_PERCENT) op = BINOP_MOD;
        else break;

        Loc op_loc = tok->loc;
        parser->tok_index++;

        AST_Node *right = parse_primary(parser);
        if (parser->has_error) return left;

        AST_Node *node = make_ast_node(AST_BINOP);
        node->loc = op_loc;
        node->binop.op = op;
        node->binop.left = left;
        node->binop.right = right;
        left = node;
    }

    return left;
}

static AST_Node *parse_additive(Parser *parser)
{
    AST_Node *left = parse_multiplicative(parser);
    if (parser->has_error) return left;

    for (;;) {
        Token *tok = peek_token(parser, 0);
        Binary_Op op;
        if      (tok->kind == TOKEN_PLUS)  op = BINOP_ADD;
        else if (tok->kind == TOKEN_MINUS) op = BINOP_SUB;
        else break;

        Loc op_loc = tok->loc;
        parser->tok_index++;

        AST_Node *right = parse_multiplicative(parser);
        if (parser->has_error) return left;

        AST_Node *node = make_ast_node(AST_BINOP);
        node->loc = op_loc;
        node->binop.op = op;
        node->binop.left = left;
        node->binop.right = right;
        left = node;
    }

    return left;
}

static AST_Node *parse_expression(Parser *parser)
{
    return parse_additive(parser);
}

static AST_Node *parse_let_statement(Parser *parser)
{
    AST_Node *node = make_ast_node(AST_LET);
    Token *let_kw = peek_token(parser, 0);
    node->loc = let_kw->loc;
    parser->tok_index++; // consume 'let'

    Token *name = expect_token(parser, TOKEN_IDENT);
    if (parser->has_error) return node;
    node->let.name = name->source;

    expect_token(parser, TOKEN_COLON);
    if (parser->has_error) return node;

    Token *type_tok = expect_token(parser, TOKEN_TYPE);
    if (parser->has_error) return node;
    node->let.type = type_tok->type;

    Token *maybe_eq = peek_token(parser, 0);
    if (maybe_eq->kind == TOKEN_EQ) {
        parser->tok_index++;
        AST_Node *init = parse_expression(parser);
        if (parser->has_error) return node;
        node->let.init = init;
    }
    return node;
}

static AST_Node *parse_set_statement(Parser *parser)
{
    AST_Node *node = make_ast_node(AST_SET);
    Token *set_kw = peek_token(parser, 0);
    node->loc = set_kw->loc;
    parser->tok_index++; // consume 'set'

    Token *name = expect_token(parser, TOKEN_IDENT);
    if (parser->has_error) return node;
    node->set.name = name->source;

    expect_token(parser, TOKEN_EQ);
    if (parser->has_error) return node;

    AST_Node *value = parse_expression(parser);
    if (parser->has_error) return node;
    node->set.value = value;
    return node;
}

static AST_Node *parse_call_statement(Parser *parser)
{
    AST_Node *node = make_ast_node(AST_CALL_STMT);
    Token *call_kw = peek_token(parser, 0);
    node->loc = call_kw->loc;
    parser->tok_index++; // consume 'call'

    Token *name = expect_token(parser, TOKEN_IDENT);
    if (parser->has_error) return node;
    node->call.name = name->source;

    expect_token(parser, TOKEN_OPEN_PAREN);
    if (parser->has_error) return node;
    parse_argument_list(parser, &node->call.args);
    if (parser->has_error) return node;
    expect_token(parser, TOKEN_CLOSE_PAREN);
    return node;
}

static AST_Node *parse_statement(Parser *parser)
{
    Token *tok = peek_token(parser, 0);
    if (tok->kind == TOKEN_KEYWORD) {
        switch (tok->keyword) {
            case KEYWORD_LET:  return parse_let_statement(parser);
            case KEYWORD_SET:  return parse_set_statement(parser);
            case KEYWORD_CALL: return parse_call_statement(parser);
            case KEYWORD_RETURN: {
                AST_Node *ret = make_ast_node(AST_RETURN);
                ret->loc = tok->loc;
                parser->tok_index++;
                AST_Node *expr = parse_expression(parser);
                if (parser->has_error) return ret;
                ret->ret_expr = expr;
                return ret;
            }
            default: break;
        }
    }
    report_error_at(parser, tok->loc, "expected a statement");
    return NULL;
}

static AST_List parse_block(Parser *parser)
{
    AST_List result = {0};

    expect_token(parser, TOKEN_OPEN_BRACE);
    if (parser->has_error) return result;

    for (;;) {
        Token *tok = peek_token(parser, 0);
        if (tok->kind == TOKEN_CLOSE_BRACE) break;
        if (tok->kind == TOKEN_EOF) {
            report_error_at(parser, tok->loc, "unexpected end of file inside function body (missing '}')");
            return result;
        }
        AST_Node *stmt = parse_statement(parser);
        if (parser->has_error) return result;
        ast_list_append(&result, stmt);
    }

    expect_token(parser, TOKEN_CLOSE_BRACE);
    return result;
}

static AST_Node *parse_fn_def(Parser *parser)
{
    AST_Node *result = make_ast_node(AST_FN);

    Token *fn_kw = peek_token(parser, 0);
    result->loc = fn_kw->loc;

    expect_keyword(parser, KEYWORD_FN);
    if (parser->has_error) return result;

    Token *name = expect_token(parser, TOKEN_IDENT);
    if (parser->has_error) return result;
    result->fn.name = name->source;

    expect_token(parser, TOKEN_OPEN_PAREN);
    if (parser->has_error) return result;

    // Parameter list: identifier ":" type, comma-separated, possibly empty.
    Token *peek_after_open = peek_token(parser, 0);
    if (peek_after_open->kind != TOKEN_CLOSE_PAREN) {
        for (;;) {
            AST_Node *param = make_ast_node(AST_PARAM);
            Token *param_name = expect_token(parser, TOKEN_IDENT);
            if (parser->has_error) return result;
            param->loc = param_name->loc;
            param->param.name = param_name->source;

            expect_token(parser, TOKEN_COLON);
            if (parser->has_error) return result;

            Token *param_type = expect_token(parser, TOKEN_TYPE);
            if (parser->has_error) return result;
            param->param.type = param_type->type;

            ast_list_append(&result->fn.parameters, param);

            Token *next = peek_token(parser, 0);
            if (next->kind != TOKEN_COMMA) break;
            parser->tok_index++; // consume comma, expect another param
        }
    }
    expect_token(parser, TOKEN_CLOSE_PAREN);
    if (parser->has_error) return result;

    Token *maybe_arrow = peek_token(parser, 0);
    if (maybe_arrow->kind == TOKEN_ARROW) {
        parser->tok_index++;
        Token *ret_type_tok = expect_token(parser, TOKEN_TYPE);
        if (parser->has_error) return result;
        result->fn.has_return_type = true;
        result->fn.return_type = ret_type_tok->type;
    }

    result->fn.body = parse_block(parser);
    if (parser->has_error) return result;

    return result;
}

Parse_Result parse_program(Token_Array tokens)
{
    Parse_Result result = {0};
    result.ast = make_ast_node(AST_PROGRAM);
    result.success = true;

    Parser parser = {0};
    parser.tokens = tokens;
    parser.tok_index = 0;

    for (;;) {
        Token *tok = peek_token(&parser, 0);
        if (tok->kind == TOKEN_EOF) break;

        AST_Node *fn_def = parse_fn_def(&parser);
        if (parser.has_error) break;
        ast_list_append(&result.ast->program, fn_def);
    }

    result.success = !parser.has_error;
    return result;
}

static void print_ast_with_indent(AST_Node *node, int depth)
{
    if (!node) return;
    switch (node->kind) {
        case AST_NONE: {
            printf("%*sAST_NONE\n", 2 * depth, "");
        } break;

        case AST_PROGRAM: {
            printf("%*sprogram\n", 2 * depth, "");
            for (AST_Node *fn = node->program.first; fn != NULL; fn = fn->next) {
                print_ast_with_indent(fn, depth + 1);
            }
        } break;

        case AST_FN: {
            printf("%*sfn %.*s(", 2 * depth, "", PRINT_STRING(node->fn.name));
            int first = 1;
            for (AST_Node *p = node->fn.parameters.first; p != NULL; p = p->next) {
                if (!first) printf(", ");
                first = 0;
                printf("%.*s: %s",
                       PRINT_STRING(p->param.name), type_name_cstr(p->param.type));
            }
            printf(")");
            if (node->fn.has_return_type) {
                printf(" -> %s", type_name_cstr(node->fn.return_type));
            }
            printf("\n");
            for (AST_Node *stmt = node->fn.body.first; stmt != NULL; stmt = stmt->next) {
                print_ast_with_indent(stmt, depth + 1);
            }
        } break;

        case AST_LET: {
            printf("%*slet %.*s: %s\n", 2 * depth, "",
                   PRINT_STRING(node->let.name), type_name_cstr(node->let.type));
            if (node->let.init) print_ast_with_indent(node->let.init, depth + 1);
        } break;

        case AST_SET: {
            printf("%*sset %.*s\n", 2 * depth, "", PRINT_STRING(node->set.name));
            print_ast_with_indent(node->set.value, depth + 1);
        } break;

        case AST_RETURN: {
            printf("%*sreturn\n", 2 * depth, "");
            if (node->ret_expr) print_ast_with_indent(node->ret_expr, depth + 1);
        } break;

        case AST_INTEGER: {
            printf("%*sinteger %ld\n", 2 * depth, "", node->int_value);
        } break;

        case AST_IDENT: {
            printf("%*sident %.*s\n", 2 * depth, "", PRINT_STRING(node->ident_name));
        } break;

        case AST_BINOP: {
            printf("%*sbinop %s\n", 2 * depth, "", binop_symbol(node->binop.op));
            print_ast_with_indent(node->binop.left,  depth + 1);
            print_ast_with_indent(node->binop.right, depth + 1);
        } break;

        case AST_CALL: {
            printf("%*scall %.*s\n", 2 * depth, "", PRINT_STRING(node->call.name));
            for (AST_Node *arg = node->call.args.first; arg != NULL; arg = arg->next) {
                print_ast_with_indent(arg, depth + 1);
            }
        } break;

        case AST_CALL_STMT: {
            printf("%*scall-stmt %.*s\n", 2 * depth, "", PRINT_STRING(node->call.name));
            for (AST_Node *arg = node->call.args.first; arg != NULL; arg = arg->next) {
                print_ast_with_indent(arg, depth + 1);
            }
        } break;

        default: {
            printf("%*s<unhandled AST kind %s>\n", 2 * depth, "", ast_kind_name(node->kind));
        } break;
    }
}

void print_ast(AST_Node *node)
{
    print_ast_with_indent(node, 0);
}
