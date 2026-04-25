#include "lexer.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// PRINT_STRING is defined in string.c (unity build).

static const char *basic_type_name(Basic_Type t)
{
    switch (t) {
        case BTYPE_INT:  return "int";
        case BTYPE_STR:  return "str";
        case BTYPE_BOOL: return "bool";
        case BTYPE_ANY:  return "any";
    }
    return "?";
}

// Render a Type as `int`, `[bool]`, `[[str]]`, etc. into a static buffer.
// The buffer rotates between calls, so up to four pending uses are safe —
// good enough for printing a parameter list.
static const char *type_to_cstr(Type t)
{
    static char  bufs[4][64];
    static int   next = 0;
    char *buf = bufs[next];
    next = (next + 1) & 3;

    int pos = 0;
    for (int i = 0; i < t.indirection && pos + 1 < (int)sizeof(bufs[0]); i++) {
        buf[pos++] = '[';
    }
    const char *name = basic_type_name(t.basic);
    while (*name && pos + 1 < (int)sizeof(bufs[0])) buf[pos++] = *name++;
    for (int i = 0; i < t.indirection && pos + 1 < (int)sizeof(bufs[0]); i++) {
        buf[pos++] = ']';
    }
    buf[pos] = '\0';
    return buf;
}

static const char *keyword_name_cstr(Keyword kw)
{
    switch (kw) {
        case KEYWORD_FN:     return "fn";
        case KEYWORD_LET:    return "let";
        case KEYWORD_SET:    return "set";
        case KEYWORD_IF:     return "if";
        case KEYWORD_ELSE:   return "else";
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
    AST_IF,
    AST_WHILE,
    AST_BLOCK,
    AST_INTEGER,
    AST_BOOL,         // `true` / `false` literal — stored as 1 / 0 in int_value
    AST_STRING,       // string literal — stores the raw source bytes between the quotes
    AST_IDENT,
    AST_BINOP,
    AST_CALL,         // function call used as an expression (return value kept)
    AST_CALL_STMT,    // `call` statement (return value discarded)
    AST_INDEX,        // array indexing: `arr[expr]`
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
        case AST_IF:        return "IF";
        case AST_WHILE:     return "WHILE";
        case AST_BLOCK:     return "BLOCK";
        case AST_INTEGER:   return "INTEGER";
        case AST_BOOL:      return "BOOL";
        case AST_STRING:    return "STRING";
        case AST_IDENT:     return "IDENT";
        case AST_BINOP:     return "BINOP";
        case AST_CALL:      return "CALL";
        case AST_CALL_STMT: return "CALL_STMT";
        case AST_INDEX:     return "INDEX";
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
    BINOP_EQ,
    BINOP_NEQ,
    BINOP_LT,
    BINOP_LE,
    BINOP_GT,
    BINOP_GE,
    BINOP_AND,
    BINOP_OR,
} Binary_Op;

static const char *binop_symbol(Binary_Op op)
{
    switch (op) {
        case BINOP_ADD: return "+";
        case BINOP_SUB: return "-";
        case BINOP_MUL: return "*";
        case BINOP_DIV: return "/";
        case BINOP_MOD: return "%";
        case BINOP_EQ:  return "==";
        case BINOP_NEQ: return "!=";
        case BINOP_LT:  return "<";
        case BINOP_LE:  return "<=";
        case BINOP_GT:  return ">";
        case BINOP_GE:  return ">=";
        case BINOP_AND: return "&&";
        case BINOP_OR:  return "||";
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
    AST_List  indices;   // empty for plain `set name = …`; otherwise one expr per `[…]`
    AST_Node *value;
} AST_Set_Data;

typedef struct AST_Index_Data
{
    AST_Node *array;
    AST_Node *index;
} AST_Index_Data;

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

typedef struct AST_If_Data
{
    AST_Node *cond;
    AST_Node *then_branch;
    AST_Node *else_branch;   // NULL if no `else` clause
} AST_If_Data;

typedef struct AST_While_Data
{
    AST_Node *cond;
    AST_Node *body;
} AST_While_Data;

struct AST_Node
{
    AST_Kind kind;
    Loc loc;
    AST_Node *prev;
    AST_Node *next;
    union {
        AST_List       program;
        AST_List       block;     // statements inside an AST_BLOCK
        AST_Fn_Data    fn;
        AST_Param_Data param;
        AST_Node      *ret_expr;
        long           int_value;       // also used for AST_BOOL: 0 (false) or 1 (true)
        String         ident_name;
        String         string_value;    // raw source bytes between the quotes (escapes still encoded)
        AST_Binop_Data binop;
        AST_Let_Data   let;
        AST_Set_Data   set;
        AST_Call_Data  call;
        AST_If_Data    if_stmt;
        AST_While_Data while_stmt;
        AST_Index_Data index;
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
    AST_Node *node = NULL;

    if (tok->kind == TOKEN_INTEGER) {
        node = make_ast_node(AST_INTEGER);
        node->loc = tok->loc;
        node->int_value = tok->long_value;
        parser->tok_index++;
    } else if (tok->kind == TOKEN_STRING) {
        node = make_ast_node(AST_STRING);
        node->loc = tok->loc;
        node->string_value = tok->source;
        parser->tok_index++;
    } else if (tok->kind == TOKEN_KEYWORD &&
               (tok->keyword == KEYWORD_TRUE || tok->keyword == KEYWORD_FALSE)) {
        node = make_ast_node(AST_BOOL);
        node->loc = tok->loc;
        node->int_value = (tok->keyword == KEYWORD_TRUE) ? 1 : 0;
        parser->tok_index++;
    } else if (tok->kind == TOKEN_IDENT) {
        Token *next = peek_token(parser, 1);
        if (next->kind == TOKEN_OPEN_PAREN) {
            // Call expression: ident '(' [ args ] ')'
            node = make_ast_node(AST_CALL);
            node->loc = tok->loc;
            node->call.name = tok->source;
            parser->tok_index += 2; // consume name and '('
            parse_argument_list(parser, &node->call.args);
            if (parser->has_error) return node;
            expect_token(parser, TOKEN_CLOSE_PAREN);
            if (parser->has_error) return node;
        } else {
            node = make_ast_node(AST_IDENT);
            node->loc = tok->loc;
            node->ident_name = tok->source;
            parser->tok_index++;
        }
    } else if (tok->kind == TOKEN_OPEN_PAREN) {
        parser->tok_index++;
        node = parse_expression(parser);
        if (parser->has_error) return node;
        Token *close = peek_token(parser, 0);
        if (close->kind != TOKEN_CLOSE_PAREN) {
            report_error_at(parser, close->loc, "expected ')' to close parenthesized expression");
            return node;
        }
        parser->tok_index++;
    } else {
        report_error_at(parser, tok->loc, "expected an expression");
        return NULL;
    }

    // Postfix indexing: any number of `[expr]` after the primary builds a
    // left-associated chain. `arr[i][j]` parses as `INDEX(INDEX(arr, i), j)`.
    while (peek_token(parser, 0)->kind == TOKEN_OPEN_BRACKET) {
        Token *bracket = peek_token(parser, 0);
        parser->tok_index++;
        AST_Node *idx = parse_expression(parser);
        if (parser->has_error) return node;
        expect_token(parser, TOKEN_CLOSE_BRACKET);
        if (parser->has_error) return node;
        AST_Node *index_node = make_ast_node(AST_INDEX);
        index_node->loc = bracket->loc;
        index_node->index.array = node;
        index_node->index.index = idx;
        node = index_node;
    }
    return node;
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

// Build a left-associated binop tree that consumes any token from
// `kinds[0..n-1]` and pairs it with the matching `ops[i]`. The next-tighter
// precedence level is parsed via `next`.
static AST_Node *parse_left_assoc(Parser *parser,
                                  AST_Node *(*next)(Parser *),
                                  const Token_Kind *kinds,
                                  const Binary_Op  *ops,
                                  long n)
{
    AST_Node *left = next(parser);
    if (parser->has_error) return left;

    for (;;) {
        Token *tok = peek_token(parser, 0);
        long match = -1;
        for (long i = 0; i < n; i++) {
            if (tok->kind == kinds[i]) { match = i; break; }
        }
        if (match < 0) break;

        Loc op_loc = tok->loc;
        parser->tok_index++;

        AST_Node *right = next(parser);
        if (parser->has_error) return left;

        AST_Node *node = make_ast_node(AST_BINOP);
        node->loc = op_loc;
        node->binop.op = ops[match];
        node->binop.left = left;
        node->binop.right = right;
        left = node;
    }
    return left;
}

static AST_Node *parse_relational(Parser *parser)
{
    static const Token_Kind kinds[] = { TOKEN_LT, TOKEN_LT_EQ, TOKEN_GT, TOKEN_GT_EQ };
    static const Binary_Op  ops[]   = { BINOP_LT, BINOP_LE,    BINOP_GT, BINOP_GE     };
    return parse_left_assoc(parser, parse_additive, kinds, ops, 4);
}

static AST_Node *parse_equality(Parser *parser)
{
    static const Token_Kind kinds[] = { TOKEN_EQ_EQ, TOKEN_BANG_EQ };
    static const Binary_Op  ops[]   = { BINOP_EQ,    BINOP_NEQ     };
    return parse_left_assoc(parser, parse_relational, kinds, ops, 2);
}

static AST_Node *parse_logical_and(Parser *parser)
{
    static const Token_Kind kinds[] = { TOKEN_AMP_AMP };
    static const Binary_Op  ops[]   = { BINOP_AND     };
    return parse_left_assoc(parser, parse_equality, kinds, ops, 1);
}

static AST_Node *parse_logical_or(Parser *parser)
{
    static const Token_Kind kinds[] = { TOKEN_PIPE_PIPE };
    static const Binary_Op  ops[]   = { BINOP_OR        };
    return parse_left_assoc(parser, parse_logical_and, kinds, ops, 1);
}

static AST_Node *parse_expression(Parser *parser)
{
    return parse_logical_or(parser);
}

// Parse a Jive type annotation: `int`, `[bool]`, `[[str]]`, etc. Each
// pair of brackets bumps the indirection count; the innermost token must
// be a basic type (TOKEN_TYPE).
static Type parse_type(Parser *parser)
{
    Type result = {0};
    int indirection = 0;
    while (peek_token(parser, 0)->kind == TOKEN_OPEN_BRACKET) {
        parser->tok_index++;
        indirection++;
    }
    Token *basic_tok = expect_token(parser, TOKEN_TYPE);
    if (parser->has_error) return result;
    result.basic = basic_tok->basic_type;
    for (int i = 0; i < indirection; i++) {
        Token *close = expect_token(parser, TOKEN_CLOSE_BRACKET);
        (void)close;
        if (parser->has_error) return result;
    }
    result.indirection = indirection;
    return result;
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

    node->let.type = parse_type(parser);
    if (parser->has_error) return node;

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

    // Optional indexing chain on the left: `set arr[i][j] = expr` becomes
    // a SET with two index expressions plus the RHS value.
    while (peek_token(parser, 0)->kind == TOKEN_OPEN_BRACKET) {
        parser->tok_index++;
        AST_Node *idx = parse_expression(parser);
        if (parser->has_error) return node;
        expect_token(parser, TOKEN_CLOSE_BRACKET);
        if (parser->has_error) return node;
        ast_list_append(&node->set.indices, idx);
    }

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

// Forward declarations: blocks contain statements, statements may be blocks.
static AST_Node *parse_statement(Parser *parser);
static AST_List parse_block(Parser *parser);

static AST_Node *parse_block_statement(Parser *parser)
{
    AST_Node *node = make_ast_node(AST_BLOCK);
    Token *brace = peek_token(parser, 0);
    node->loc = brace->loc;
    node->block = parse_block(parser);
    return node;
}

static AST_Node *parse_if_statement(Parser *parser)
{
    AST_Node *node = make_ast_node(AST_IF);
    Token *if_kw = peek_token(parser, 0);
    node->loc = if_kw->loc;
    parser->tok_index++; // consume 'if'

    AST_Node *cond = parse_expression(parser);
    if (parser->has_error) return node;
    AST_Node *then_branch = parse_statement(parser);
    if (parser->has_error) return node;

    node->if_stmt.cond = cond;
    node->if_stmt.then_branch = then_branch;

    Token *next = peek_token(parser, 0);
    if (next->kind == TOKEN_KEYWORD && next->keyword == KEYWORD_ELSE) {
        parser->tok_index++;
        AST_Node *else_branch = parse_statement(parser);
        if (parser->has_error) return node;
        node->if_stmt.else_branch = else_branch;
    }
    return node;
}

static AST_Node *parse_while_statement(Parser *parser)
{
    AST_Node *node = make_ast_node(AST_WHILE);
    Token *while_kw = peek_token(parser, 0);
    node->loc = while_kw->loc;
    parser->tok_index++; // consume 'while'

    AST_Node *cond = parse_expression(parser);
    if (parser->has_error) return node;
    AST_Node *body = parse_statement(parser);
    if (parser->has_error) return node;

    node->while_stmt.cond = cond;
    node->while_stmt.body = body;
    return node;
}

static AST_Node *parse_statement(Parser *parser)
{
    Token *tok = peek_token(parser, 0);
    if (tok->kind == TOKEN_OPEN_BRACE) {
        return parse_block_statement(parser);
    }
    if (tok->kind == TOKEN_KEYWORD) {
        switch (tok->keyword) {
            case KEYWORD_LET:   return parse_let_statement(parser);
            case KEYWORD_SET:   return parse_set_statement(parser);
            case KEYWORD_CALL:  return parse_call_statement(parser);
            case KEYWORD_IF:    return parse_if_statement(parser);
            case KEYWORD_WHILE: return parse_while_statement(parser);
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

            param->param.type = parse_type(parser);
            if (parser->has_error) return result;

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
        result->fn.return_type = parse_type(parser);
        if (parser->has_error) return result;
        result->fn.has_return_type = true;
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
                       PRINT_STRING(p->param.name), type_to_cstr(p->param.type));
            }
            printf(")");
            if (node->fn.has_return_type) {
                printf(" -> %s", type_to_cstr(node->fn.return_type));
            }
            printf("\n");
            for (AST_Node *stmt = node->fn.body.first; stmt != NULL; stmt = stmt->next) {
                print_ast_with_indent(stmt, depth + 1);
            }
        } break;

        case AST_LET: {
            printf("%*slet %.*s: %s\n", 2 * depth, "",
                   PRINT_STRING(node->let.name), type_to_cstr(node->let.type));
            if (node->let.init) print_ast_with_indent(node->let.init, depth + 1);
        } break;

        case AST_SET: {
            printf("%*sset %.*s", 2 * depth, "", PRINT_STRING(node->set.name));
            for (long i = 0; i < node->set.indices.count; i++) printf("[]");
            printf("\n");
            for (AST_Node *i = node->set.indices.first; i != NULL; i = i->next) {
                printf("%*sindex\n", 2 * (depth + 1), "");
                print_ast_with_indent(i, depth + 2);
            }
            print_ast_with_indent(node->set.value, depth + 1);
        } break;

        case AST_RETURN: {
            printf("%*sreturn\n", 2 * depth, "");
            if (node->ret_expr) print_ast_with_indent(node->ret_expr, depth + 1);
        } break;

        case AST_INTEGER: {
            printf("%*sinteger %ld\n", 2 * depth, "", node->int_value);
        } break;

        case AST_BOOL: {
            printf("%*sbool %s\n", 2 * depth, "", node->int_value ? "true" : "false");
        } break;

        case AST_STRING: {
            printf("%*sstring \"%.*s\"\n", 2 * depth, "", PRINT_STRING(node->string_value));
        } break;

        case AST_IDENT: {
            printf("%*sident %.*s\n", 2 * depth, "", PRINT_STRING(node->ident_name));
        } break;

        case AST_INDEX: {
            printf("%*sindex\n", 2 * depth, "");
            print_ast_with_indent(node->index.array, depth + 1);
            print_ast_with_indent(node->index.index, depth + 1);
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

        case AST_IF: {
            printf("%*sif\n", 2 * depth, "");
            print_ast_with_indent(node->if_stmt.cond, depth + 1);
            printf("%*sthen\n", 2 * depth, "");
            print_ast_with_indent(node->if_stmt.then_branch, depth + 1);
            if (node->if_stmt.else_branch) {
                printf("%*selse\n", 2 * depth, "");
                print_ast_with_indent(node->if_stmt.else_branch, depth + 1);
            }
        } break;

        case AST_WHILE: {
            printf("%*swhile\n", 2 * depth, "");
            print_ast_with_indent(node->while_stmt.cond, depth + 1);
            printf("%*sdo\n", 2 * depth, "");
            print_ast_with_indent(node->while_stmt.body, depth + 1);
        } break;

        case AST_BLOCK: {
            printf("%*sblock\n", 2 * depth, "");
            for (AST_Node *s = node->block.first; s != NULL; s = s->next) {
                print_ast_with_indent(s, depth + 1);
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
