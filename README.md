# jivec

![Language: C11](https://img.shields.io/badge/language-C11-A8B9CC.svg?logo=c&logoColor=white)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20WSL-informational.svg)
![Target](https://img.shields.io/badge/target-x86--64%20NASM-red.svg)
![Build](https://img.shields.io/badge/build-unity-orange.svg)

A from-scratch compiler for **Jive**, written in C, targeting x86-64 NASM assembly.

Jive is a small teaching language designed by **Professor Lothar Narins** for CS 5008 at Northeastern University. `jivec` is my implementation of the course compiler — built up stage by stage, from lexer to codegen.

## Status

- [x] Stage 1 — lexer
- [x] Stage 2 — parser + minimal codegen (empty-param `fn`s whose body is `return <int>`)
- [ ] Stage 3 — type checker
- [ ] Stage 4 — full NASM code generator

## The Jive language

Jive is a small, statically-typed language with a Rust-flavored syntax. A program is a sequence of function definitions, and `main` is the entry point — the compiler calls it and uses its return value as the process exit code.

### A minimal program

```jive
fn main() -> int
{
    return 42
}
```

Build it, then run the assembled binary:

```sh
./build/jive hello.jive -o hello.asm
nasm -felf64 hello.asm -o hello.o && ld hello.o -o hello
./hello; echo "exit=$?"   # exit=42
```

### Keywords, types, and operators

- **Keywords:** `fn` `let` `set` `if` `while` `call` `return` `true` `false`
- **Types:** `int` `str` `bool`
- **Unary operators:** `-` `~` `!`
- **Binary operators:** `+ - * / %` &nbsp; `& | ^` &nbsp; `== != < > <= >=` &nbsp; `&& ||`
- **Grouping / punctuation:** `( )` `[ ]` `{ }` `,` `:` `->`
- **Comments:** `// ...` to end of line

### Statements

| Statement | Form                                  | Purpose                                          |
|-----------|---------------------------------------|--------------------------------------------------|
| `let`     | `let name: type [= expr]`             | declare a variable, optionally initialized      |
| `set`     | `set name = expr`                     | assign to an existing variable                  |
| `return`  | `return [expr]`                       | return from the current function                |
| `if`      | `if cond stmt [else stmt]`            | conditional branch                              |
| `while`   | `while cond stmt`                     | loop                                            |
| `call`    | `call fn_name(args)`                  | invoke a function for its side effects          |

A `stmt` may be any single statement or a `{ ... }` block.

### Examples

Recursive Fibonacci:

```jive
fn fib(n: int) -> int
{
    if n < 2 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

fn main() -> int
{
    return fib(10)
}
```

A loop with side-effectful calls (assuming built-ins `print_int` and `print_nl`):

```jive
fn main() -> int
{
    let n: int = 0
    while n < 10 {
        call print_int(n)
        call print_nl()
        set n = n + 1
    }
    return n - 10
}
```

### Grammar (EBNF)

```ebnf
letter          = "A"-"Z" | "a"-"z" ;
digit           = "0"-"9" ;
identifier      = ( "_" | letter ) , { "_" | letter | digit } ;
number          = digit , { digit } ;
string          = '"' , { ? all characters ? - ( '"' | '\n' ) } , '"' ;

type            = "int" | "str" | "bool" ;
primitive       = ( identifier , { subscript } )
                | number
                | call_expr
                | ( "(" , expr , ")" ) ;
unop            = "-" | "~" | "!" ;
binop           = "+" | "-" | "*" | "/" | "%" | "&" | "|" | "^"
                | "==" | "!=" | "<" | ">" | "<=" | ">=" | "&&" | "||" ;
term            = { unop } , primitive ;
expr            = term , { binop , term } ;
expr_list       = expr , { "," , expr } ;
argument_list   = "(" , [ expr_list ] , ")" ;
call_expr       = identifier , argument_list ;
subscript       = "[" , expr , "]" ;

call_stmt       = "call" , call_expr ;
let             = "let" , identifier , ":" , type , [ "=" , expr ] ;
set             = "set" , identifier , { subscript } , "=" , expr ;
return          = "return" , [ expr ] ;
if              = "if" , expr , statement , [ "else" , statement ] ;
while           = "while" , expr , statement ;
statement       = call_stmt | let | set | return | if | while | block ;
block           = "{" , { statement } , "}" ;

id_type         = identifier , ":" , type ;
id_type_list    = id_type , { "," , id_type } ;
parameter_list  = "(" , [ id_type_list ] , ")" ;
fn_definition   = "fn" , identifier , parameter_list , [ "->" , type ] , block ;
program         = { fn_definition } ;
```

### What compiles today

The lexer (Stage 1) recognises the full token set above. The parser + code generator (Stage 2) currently accept a restricted subset: **zero-parameter functions whose body is a single `return <integer literal>` statement.** Anything beyond that — parameters, `let`/`set`, `if`/`while`, expressions, function calls — is planned for Stages 3–4. Programs that exceed the current subset are rejected with a `file:line:col: error: ...` diagnostic.

## Getting started

`jivec` is developed and tested on **Linux**. On Windows, the recommended setup is **WSL with Ubuntu** — this matches the grading environment for CS 5008 and avoids Windows-specific toolchain quirks. Native Linux and macOS should also work, but WSL Ubuntu is the path that has been verified.

### 1. Install WSL Ubuntu (Windows only)

In **PowerShell as Administrator**:

```powershell
wsl --install -d Ubuntu
```

Reboot when prompted, then launch *Ubuntu* from the Start menu and create your UNIX user / password. If you already have WSL installed but no distro, `wsl --list --online` shows available images.

Everything below runs **inside the Ubuntu shell**, not PowerShell or Git Bash.

### 2. Install build tools

```sh
sudo apt update
sudo apt install -y build-essential git
```

`build-essential` pulls in `gcc`, `make`, and libc headers. `git` is used to clone this repo. Verify:

```sh
gcc --version
make --version
```

### 3. Clone and build

```sh
git clone https://github.com/johnha912/jivec.git
cd jivec
cd code && ./build.sh
```

`code/build.sh` is the canonical build script: it compiles `code/main.c` (a unity-build translation unit that `#include`s `string.c`, `lexer.c`, `parser.c`, and `codegen.c`) into `build/jive`, then smoke-tests the compiler on the sample programs in `jive_programs/`.

A `Makefile` at the repo root is also provided as a convenience:

```sh
make            # builds build/jive and build/test_lexer
```

Prefer to invoke `gcc` directly? One command is enough:

```sh
mkdir -p build
gcc -Wall -Wextra -Wpedantic -std=c11 -O0 -g \
    -o build/jive code/main.c
```

### 4. Compile a Jive program

```
$ ./build/jive jive_programs/simple.jive -o build/simple.asm
$ cat build/simple.asm
global _start

_start:
    call main
    mov rdi, rax
    mov rax, 60
    syscall

main:
    mov rax, 42
    ret
```

Useful flags:

- `-o <file>` — output assembly path (defaults to `out.asm`)
- `--dump-tokens` — print the lexer output
- `--dump-ast` — print the parsed abstract syntax tree

You can assemble and link the output with NASM + `ld` to produce a runnable ELF:

```sh
cd build
nasm -felf64 simple.asm -o simple.o
ld simple.o -o simple
./simple; echo "exit=$?"   # prints: exit=42
```

Need just the lexer? `build/test_lexer` is still built alongside `jive`:

```sh
./build/test_lexer jive_programs/simple.jive
```

### 5. Write your own Jive program

Create a file `hello.jive` under `jive_programs/` (or anywhere):

```jive
fn main() -> int
{
    // comments are ignored
    return 42
}
```

Then compile it:

```sh
./build/jive hello.jive -o hello.asm
```

## Project layout

```
code/
  string.c       String type, PRINT_STRING macro, small helpers
  lexer.h        token and public API definitions
  lexer.c        lexer implementation
  parser.c       AST definitions + recursive-descent parser
  codegen.c      NASM emitter
  main.c         unity-build entry for the jive compiler
  test_lexer.c   unity-build entry for the lexer test driver
  build.sh       canonical build script (cd code && ./build.sh)
jive_programs/   sample .jive input programs
Makefile         convenience wrapper
build/           compiled binaries and .asm output (git-ignored)
```

## Troubleshooting

- **`make: command not found`** — you're inside WSL Ubuntu but haven't installed `build-essential` yet; see step 2.
- **`gcc: error: unrecognized command-line option '-Wpedantic'`** — your `gcc` is very old. Install a newer one via `sudo apt install gcc`.
- **Running on Windows without WSL** — MSYS2/MinGW GCC works but produces `.exe` output and is not the supported path. If you must, add `.exe` to the output name.

## Credits

The **Jive** language was designed by **Professor Lothar Narins** for CS 5008 at Northeastern University. The language design, grammar, and course structure are his work; this repository is my compiler implementation.

## License

See [LICENSE](LICENSE).
