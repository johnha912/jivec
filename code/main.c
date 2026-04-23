#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "string.c"
#include "lexer.c"
#include "parser.c"
#include "codegen.c"

typedef struct Options
{
    const char *in_file_name;
    const char *out_file_name;
    bool dump_tokens;
    bool dump_ast;
} Options;

static void print_usage(const char *program_name)
{
    fprintf(stderr,
            "Usage: %s <input.jive> [-o output.asm] [--dump-tokens] [--dump-ast]\n",
            program_name);
}

int main(int arg_count, const char **args)
{
    Options options = {0};
    options.out_file_name = "out.asm";

    const char *program_name = args[0];
    int arg_index = 1;

    while (arg_index < arg_count) {
        const char *arg = args[arg_index++];
        if (strcmp(arg, "-o") == 0) {
            if (arg_index >= arg_count) {
                fprintf(stderr, "error: missing output file name after -o\n");
                return 1;
            }
            options.out_file_name = args[arg_index++];
        } else if (strcmp(arg, "--dump-tokens") == 0) {
            options.dump_tokens = true;
        } else if (strcmp(arg, "--dump-ast") == 0) {
            options.dump_ast = true;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(program_name);
            return 0;
        } else if (options.in_file_name == NULL) {
            options.in_file_name = arg;
        } else {
            fprintf(stderr, "warning: unrecognized argument '%s'\n", arg);
        }
    }

    if (options.in_file_name == NULL) {
        fprintf(stderr, "error: no input file supplied\n");
        print_usage(program_name);
        return 1;
    }

    Token_Array tokens = lex_file(options.in_file_name);

    if (options.dump_tokens) {
        printf("=== tokens ===\n");
        for (long i = 0; i < tokens.count; i++) {
            print_token(&tokens.items[i]);
        }
    }

    Parse_Result parse_result = parse_program(tokens);
    if (!parse_result.success) {
        token_array_free(&tokens);
        return 1;
    }

    if (options.dump_ast) {
        printf("=== ast ===\n");
        print_ast(parse_result.ast);
    }

    FILE *out_file = fopen(options.out_file_name, "w");
    if (!out_file) {
        fprintf(stderr, "error: could not open '%s' for writing\n", options.out_file_name);
        token_array_free(&tokens);
        return 1;
    }

    bool ok = generate_asm(parse_result.ast, out_file);
    fclose(out_file);
    token_array_free(&tokens);

    return ok ? 0 : 1;
}
