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
    AST_RETURN,
    AST_INTEGER,
} AST_Kind;

static const char *ast_kind_name(AST_Kind kind)
{
    switch (kind) {
        case AST_NONE:    return "NONE";
        case AST_PROGRAM: return "PROGRAM";
        case AST_FN:      return "FN";
        case AST_RETURN:  return "RETURN";
        case AST_INTEGER: return "INTEGER";
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

struct AST_Node
{
    AST_Kind kind;
    Loc loc;
    AST_Node *prev;
    AST_Node *next;
    union {
        AST_List    program;
        AST_Fn_Data fn;
        AST_Node   *ret_expr;
        long        int_value;
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

static AST_Node *parse_expression(Parser *parser)
{
    Token *tok = peek_token(parser, 0);
    if (tok->kind == TOKEN_INTEGER) {
        AST_Node *node = make_ast_node(AST_INTEGER);
        node->loc = tok->loc;
        node->int_value = tok->long_value;
        parser->tok_index++;
        return node;
    }
    report_error_at(parser, tok->loc, "expected an expression (integer literal)");
    return NULL;
}

static AST_Node *parse_statement(Parser *parser)
{
    Token *tok = peek_token(parser, 0);
    if (tok->kind == TOKEN_KEYWORD && tok->keyword == KEYWORD_RETURN) {
        AST_Node *ret = make_ast_node(AST_RETURN);
        ret->loc = tok->loc;
        parser->tok_index++;
        AST_Node *expr = parse_expression(parser);
        if (parser->has_error) return ret;
        ret->ret_expr = expr;
        return ret;
    }
    report_error_at(parser, tok->loc, "expected a statement (only 'return' is supported at this stage)");
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

    // Parameters are not supported yet; any token other than ')' is an error.
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
            printf("%*sfn %.*s()", 2 * depth, "", PRINT_STRING(node->fn.name));
            if (node->fn.has_return_type) {
                printf(" -> %s", type_name_cstr(node->fn.return_type));
            }
            printf("\n");
            for (AST_Node *stmt = node->fn.body.first; stmt != NULL; stmt = stmt->next) {
                print_ast_with_indent(stmt, depth + 1);
            }
        } break;

        case AST_RETURN: {
            printf("%*sreturn\n", 2 * depth, "");
            if (node->ret_expr) print_ast_with_indent(node->ret_expr, depth + 1);
        } break;

        case AST_INTEGER: {
            printf("%*sinteger %ld\n", 2 * depth, "", node->int_value);
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
