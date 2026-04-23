#include "lexer.c"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <file.jive>\n", argv[0]);
        return 1;
    }

    Token_Array tokens = lex_file(argv[1]);
    for (long i = 0; i < tokens.count; i++) {
        print_token(&tokens.items[i]);
    }
    token_array_free(&tokens);
    return 0;
}
