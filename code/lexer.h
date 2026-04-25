#ifndef JIVE_LEXER_H
#define JIVE_LEXER_H

#include <stddef.h>

typedef enum Token_Kind
{
    TOKEN_EOF = 0,

    TOKEN_KEYWORD,
    TOKEN_IDENT,
    TOKEN_TYPE,
    TOKEN_INTEGER,
    TOKEN_STRING,

    TOKEN_OPEN_PAREN,     // (
    TOKEN_CLOSE_PAREN,    // )
    TOKEN_OPEN_BRACE,     // {
    TOKEN_CLOSE_BRACE,    // }
    TOKEN_OPEN_BRACKET,   // [
    TOKEN_CLOSE_BRACKET,  // ]
    TOKEN_COMMA,          // ,
    TOKEN_COLON,          // :
    TOKEN_ARROW,          // ->

    TOKEN_PLUS,           // +
    TOKEN_MINUS,          // -
    TOKEN_STAR,           // *
    TOKEN_SLASH,          // /
    TOKEN_PERCENT,        // %
    TOKEN_AMP,            // &
    TOKEN_PIPE,           // |
    TOKEN_CARET,          // ^
    TOKEN_TILDE,          // ~
    TOKEN_BANG,           // !

    TOKEN_EQ,             // =
    TOKEN_EQ_EQ,          // ==
    TOKEN_BANG_EQ,        // !=
    TOKEN_LT,             // <
    TOKEN_GT,             // >
    TOKEN_LT_EQ,          // <=
    TOKEN_GT_EQ,          // >=
    TOKEN_AMP_AMP,        // &&
    TOKEN_PIPE_PIPE,      // ||
} Token_Kind;

typedef enum Keyword
{
    KEYWORD_FN,
    KEYWORD_LET,
    KEYWORD_SET,
    KEYWORD_IF,
    KEYWORD_ELSE,
    KEYWORD_WHILE,
    KEYWORD_CALL,
    KEYWORD_RETURN,
    KEYWORD_TRUE,
    KEYWORD_FALSE,
} Keyword;

typedef enum Type
{
    TYPE_INT,
    TYPE_STR,
    TYPE_BOOL,
} Type;

// String is defined in string.c; include string.c before lexer.h in the unity build.

typedef struct Loc
{
    const char *file_name;
    long line;
    long col;
} Loc;

typedef struct Token
{
    Token_Kind kind;
    String source;
    Loc loc;
    union {
        long long_value;
        Keyword keyword;
        Type type;
    };
} Token;

typedef struct Token_Array
{
    Token *items;
    long count;
    long capacity;

    char *file_data;
    char *file_name_owned;
} Token_Array;

Token_Array lex_file(const char *file_name);
void token_array_free(Token_Array *arr);
void print_token(const Token *tok);
const char *token_kind_name(Token_Kind kind);

#endif
