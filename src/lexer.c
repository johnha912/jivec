#include "lexer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Lexer
{
    String source;
    long index;
    Loc loc;
} Lexer;

static char *dup_cstr(const char *s)
{
    size_t n = strlen(s) + 1;
    char *out = (char *)malloc(n);
    if (!out) { fprintf(stderr, "lexer: out of memory\n"); exit(1); }
    memcpy(out, s, n);
    return out;
}

static void token_array_append(Token_Array *arr, Token tok)
{
    if (arr->count >= arr->capacity) {
        long new_cap = arr->capacity == 0 ? 16 : arr->capacity * 2;
        Token *new_items = (Token *)realloc(arr->items, (size_t)new_cap * sizeof(Token));
        if (!new_items) { fprintf(stderr, "lexer: out of memory\n"); exit(1); }
        arr->items = new_items;
        arr->capacity = new_cap;
    }
    arr->items[arr->count++] = tok;
}

static int at_end(const Lexer *lex) { return lex->index >= lex->source.count; }

static int peek_at(const Lexer *lex, long offset)
{
    long i = lex->index + offset;
    if (i < 0 || i >= lex->source.count) return -1;
    return (unsigned char)lex->source.data[i];
}

static int current(const Lexer *lex) { return peek_at(lex, 0); }

static void advance(Lexer *lex)
{
    if (at_end(lex)) return;
    char c = lex->source.data[lex->index++];
    if (c == '\n') {
        lex->loc.line++;
        lex->loc.col = 1;
    } else {
        lex->loc.col++;
    }
}

static void skip_whitespace_and_comments(Lexer *lex)
{
    for (;;) {
        int c = current(lex);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lex);
        } else if (c == '/' && peek_at(lex, 1) == '/') {
            while (!at_end(lex) && current(lex) != '\n') advance(lex);
        } else {
            break;
        }
    }
}

static int is_ident_start(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_ident_cont(int c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

typedef struct { const char *name; Keyword kw; } Keyword_Entry;
static const Keyword_Entry KEYWORDS[] = {
    {"fn",     KEYWORD_FN},
    {"let",    KEYWORD_LET},
    {"set",    KEYWORD_SET},
    {"if",     KEYWORD_IF},
    {"while",  KEYWORD_WHILE},
    {"call",   KEYWORD_CALL},
    {"return", KEYWORD_RETURN},
    {"true",   KEYWORD_TRUE},
    {"false",  KEYWORD_FALSE},
};
static const size_t N_KEYWORDS = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);

typedef struct { const char *name; Type type; } Type_Entry;
static const Type_Entry TYPES[] = {
    {"int",  TYPE_INT},
    {"str",  TYPE_STR},
    {"bool", TYPE_BOOL},
};
static const size_t N_TYPES = sizeof(TYPES) / sizeof(TYPES[0]);

static int string_equals_cstr(String s, const char *cstr)
{
    size_t n = strlen(cstr);
    if ((size_t)s.count != n) return 0;
    return memcmp(s.data, cstr, n) == 0;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
static void lex_error(Loc loc, const char *msg)
{
    fprintf(stderr, "%s:%ld:%ld: error: %s\n", loc.file_name, loc.line, loc.col, msg);
    exit(1);
}

static Token make_token(Token_Kind kind, Loc loc, String source)
{
    Token t = {0};
    t.kind = kind;
    t.loc = loc;
    t.source = source;
    return t;
}

static Token next_token(Lexer *lex)
{
    skip_whitespace_and_comments(lex);

    Loc start_loc = lex->loc;
    long start = lex->index;

    if (at_end(lex)) {
        String src = { lex->source.data + start, 0 };
        return make_token(TOKEN_EOF, start_loc, src);
    }

    int c = current(lex);

    // Identifier / keyword / type
    if (is_ident_start(c)) {
        while (is_ident_cont(current(lex))) advance(lex);
        String src = { lex->source.data + start, lex->index - start };

        for (size_t i = 0; i < N_KEYWORDS; i++) {
            if (string_equals_cstr(src, KEYWORDS[i].name)) {
                Token t = make_token(TOKEN_KEYWORD, start_loc, src);
                t.keyword = KEYWORDS[i].kw;
                return t;
            }
        }
        for (size_t i = 0; i < N_TYPES; i++) {
            if (string_equals_cstr(src, TYPES[i].name)) {
                Token t = make_token(TOKEN_TYPE, start_loc, src);
                t.type = TYPES[i].type;
                return t;
            }
        }
        return make_token(TOKEN_IDENT, start_loc, src);
    }

    // Integer literal
    if (c >= '0' && c <= '9') {
        long value = 0;
        while (current(lex) >= '0' && current(lex) <= '9') {
            value = value * 10 + (current(lex) - '0');
            advance(lex);
        }
        String src = { lex->source.data + start, lex->index - start };
        Token t = make_token(TOKEN_INTEGER, start_loc, src);
        t.long_value = value;
        return t;
    }

    // String literal — content between quotes, with backslash-escapes passed through raw
    if (c == '"') {
        advance(lex); // consume opening quote
        long content_start = lex->index;
        while (!at_end(lex) && current(lex) != '"' && current(lex) != '\n') {
            if (current(lex) == '\\' && peek_at(lex, 1) != -1) {
                advance(lex);
                advance(lex);
            } else {
                advance(lex);
            }
        }
        if (current(lex) != '"') lex_error(start_loc, "unterminated string literal");
        String src = { lex->source.data + content_start, lex->index - content_start };
        advance(lex); // consume closing quote
        return make_token(TOKEN_STRING, start_loc, src);
    }

    // Symbols — advance past first char, then look at the next for 2-char ops
    advance(lex);
    int c2 = current(lex);
    String one = { lex->source.data + start, 1 };
    String two = { lex->source.data + start, 2 };

    switch (c) {
        case '(': return make_token(TOKEN_OPEN_PAREN, start_loc, one);
        case ')': return make_token(TOKEN_CLOSE_PAREN, start_loc, one);
        case '{': return make_token(TOKEN_OPEN_BRACE, start_loc, one);
        case '}': return make_token(TOKEN_CLOSE_BRACE, start_loc, one);
        case '[': return make_token(TOKEN_OPEN_BRACKET, start_loc, one);
        case ']': return make_token(TOKEN_CLOSE_BRACKET, start_loc, one);
        case ',': return make_token(TOKEN_COMMA, start_loc, one);
        case ':': return make_token(TOKEN_COLON, start_loc, one);
        case '+': return make_token(TOKEN_PLUS, start_loc, one);
        case '*': return make_token(TOKEN_STAR, start_loc, one);
        case '/': return make_token(TOKEN_SLASH, start_loc, one);
        case '%': return make_token(TOKEN_PERCENT, start_loc, one);
        case '^': return make_token(TOKEN_CARET, start_loc, one);
        case '~': return make_token(TOKEN_TILDE, start_loc, one);

        case '-':
            if (c2 == '>') { advance(lex); return make_token(TOKEN_ARROW, start_loc, two); }
            return make_token(TOKEN_MINUS, start_loc, one);

        case '=':
            if (c2 == '=') { advance(lex); return make_token(TOKEN_EQ_EQ, start_loc, two); }
            lex_error(start_loc, "unexpected '=' (did you mean '=='?)");

        case '!':
            if (c2 == '=') { advance(lex); return make_token(TOKEN_BANG_EQ, start_loc, two); }
            return make_token(TOKEN_BANG, start_loc, one);

        case '<':
            if (c2 == '=') { advance(lex); return make_token(TOKEN_LT_EQ, start_loc, two); }
            return make_token(TOKEN_LT, start_loc, one);

        case '>':
            if (c2 == '=') { advance(lex); return make_token(TOKEN_GT_EQ, start_loc, two); }
            return make_token(TOKEN_GT, start_loc, one);

        case '&':
            if (c2 == '&') { advance(lex); return make_token(TOKEN_AMP_AMP, start_loc, two); }
            return make_token(TOKEN_AMP, start_loc, one);

        case '|':
            if (c2 == '|') { advance(lex); return make_token(TOKEN_PIPE_PIPE, start_loc, two); }
            return make_token(TOKEN_PIPE, start_loc, one);

        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "unexpected character '%c' (0x%02X)", c, c);
            lex_error(start_loc, msg);
        }
    }

    Token unreachable = {0};
    return unreachable;
}

Token_Array lex_file(const char *file_name)
{
    Token_Array result = {0};

    FILE *f = fopen(file_name, "rb");
    if (!f) {
        fprintf(stderr, "lexer: could not open '%s': %s\n", file_name, strerror(errno));
        exit(1);
    }
    if (fseek(f, 0, SEEK_END) != 0) { fprintf(stderr, "lexer: fseek failed\n"); exit(1); }
    long size = ftell(f);
    if (size < 0) { fprintf(stderr, "lexer: ftell failed\n"); exit(1); }
    rewind(f);

    char *data = (char *)malloc((size_t)size + 1);
    if (!data) { fprintf(stderr, "lexer: out of memory\n"); exit(1); }
    if (size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "lexer: failed to read '%s'\n", file_name);
        exit(1);
    }
    data[size] = '\0';
    fclose(f);

    result.file_data = data;
    result.file_name_owned = dup_cstr(file_name);

    Lexer lex = {0};
    lex.source.data = data;
    lex.source.count = size;
    lex.index = 0;
    lex.loc.file_name = result.file_name_owned;
    lex.loc.line = 1;
    lex.loc.col = 1;

    for (;;) {
        Token t = next_token(&lex);
        token_array_append(&result, t);
        if (t.kind == TOKEN_EOF) break;
    }
    return result;
}

void token_array_free(Token_Array *arr)
{
    free(arr->items);
    free(arr->file_data);
    free(arr->file_name_owned);
    arr->items = NULL;
    arr->file_data = NULL;
    arr->file_name_owned = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

const char *token_kind_name(Token_Kind kind)
{
    switch (kind) {
        case TOKEN_EOF:            return "EOF";
        case TOKEN_KEYWORD:        return "KEYWORD";
        case TOKEN_IDENT:          return "IDENTIFIER";
        case TOKEN_TYPE:           return "TYPE";
        case TOKEN_INTEGER:        return "INTEGER";
        case TOKEN_STRING:         return "STRING";
        case TOKEN_OPEN_PAREN:     return "(";
        case TOKEN_CLOSE_PAREN:    return ")";
        case TOKEN_OPEN_BRACE:     return "{";
        case TOKEN_CLOSE_BRACE:    return "}";
        case TOKEN_OPEN_BRACKET:   return "[";
        case TOKEN_CLOSE_BRACKET:  return "]";
        case TOKEN_COMMA:          return ",";
        case TOKEN_COLON:          return ":";
        case TOKEN_ARROW:          return "->";
        case TOKEN_PLUS:           return "+";
        case TOKEN_MINUS:          return "-";
        case TOKEN_STAR:           return "*";
        case TOKEN_SLASH:          return "/";
        case TOKEN_PERCENT:        return "%";
        case TOKEN_AMP:            return "&";
        case TOKEN_PIPE:           return "|";
        case TOKEN_CARET:          return "^";
        case TOKEN_TILDE:          return "~";
        case TOKEN_BANG:           return "!";
        case TOKEN_EQ_EQ:          return "==";
        case TOKEN_BANG_EQ:        return "!=";
        case TOKEN_LT:             return "<";
        case TOKEN_GT:             return ">";
        case TOKEN_LT_EQ:          return "<=";
        case TOKEN_GT_EQ:          return ">=";
        case TOKEN_AMP_AMP:        return "&&";
        case TOKEN_PIPE_PIPE:      return "||";
    }
    return "?";
}

static int kind_has_label(Token_Kind kind)
{
    switch (kind) {
        case TOKEN_KEYWORD:
        case TOKEN_IDENT:
        case TOKEN_TYPE:
        case TOKEN_INTEGER:
        case TOKEN_STRING:
            return 1;
        default:
            return 0;
    }
}

void print_token(const Token *tok)
{
    char loc_buf[256];
    snprintf(loc_buf, sizeof(loc_buf), "%s:%ld:%ld",
             tok->loc.file_name, tok->loc.line, tok->loc.col);

    // Keep at least 2 spaces between columns when the loc overflows its fixed width.
    int loc_len = (int)strlen(loc_buf);
    int loc_pad = 19 - loc_len;
    if (loc_pad < 2) loc_pad = 2;

    if (tok->kind == TOKEN_EOF) {
        printf("%s%*s%s\n", loc_buf, loc_pad, "", "EOF");
        return;
    }

    if (kind_has_label(tok->kind)) {
        const char *label = token_kind_name(tok->kind);
        if (tok->kind == TOKEN_STRING) {
            printf("%s%*s%-13s\"%.*s\"\n", loc_buf, loc_pad, "", label,
                   (int)tok->source.count, tok->source.data);
        } else {
            printf("%s%*s%-13s%.*s\n", loc_buf, loc_pad, "", label,
                   (int)tok->source.count, tok->source.data);
        }
    } else {
        printf("%s%*s%s\n", loc_buf, loc_pad, "", token_kind_name(tok->kind));
    }
}
