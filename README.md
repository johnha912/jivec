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
- [x] Stage 3 — arithmetic expressions + stack-machine IR
- [x] Stage 4 — local variables (`let` / `set`) + symbol table

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

Local variables (compiles and runs today — the program below exits with code 42):

```jive
fn main() -> int
{
    let a: int = 1     // a = 1
    let b: int = a + 5 // b = 6
    set a = b * 2      // a = 12
    set b = a * 3      // b = 36
    return a + b - 6   // return 42
}
```

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

`code/build.sh` is the canonical build script: it compiles `code/main.c` (a unity-build translation unit that `#include`s `string.c`, `lexer.c`, `parser.c`, `ir.c`, and `codegen.c`) into `build/jive`, then smoke-tests the compiler on the sample programs in `jive_programs/`.

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
    mov rdi, rax    ; return code
    mov rax, 60     ; exit syscall
    syscall

main:
    push rbp
    mov rbp, rsp
    push 42   ; PUSH 42

    ; RETURN
    pop rax
    mov rsp, rbp
    pop rbp
    ret
```

Useful flags:

- `-o <file>` — output assembly path (defaults to `out.asm`)
- `--dump-tokens` — print the lexer output
- `--dump-ast` — print the parsed abstract syntax tree
- `--dump-ir` — print the stack-machine intermediate representation

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
  string.c        String type, PRINT_STRING macro, small helpers
  lexer.h         token and public API definitions
  lexer.c         lexer implementation
  parser.c        AST definitions + recursive-descent parser
  symbol_table.c  open-addressed hash table for per-function locals
  ir.c            stack-machine IR + AST→IR lowering (uses symbol table)
  codegen.c       NASM emitter (consumes IR)
  main.c          unity-build entry for the jive compiler
  test_lexer.c    unity-build entry for the lexer test driver
  build.sh        canonical build script (cd code && ./build.sh)
jive_programs/    sample .jive input programs
Makefile          convenience wrapper
build/            compiled binaries and .asm output (git-ignored)
```

## How the compiler works

`jive` is a four-stage pipeline: each stage turns the previous stage's output into something closer to machine code.

```
  source.jive ──► lexer ──► tokens ──► parser ──► AST ──► ir.c ──► IR ──► codegen ──► out.asm
  (text)                   (stream)            (tree)    │      (stack ops)        (NASM)
                                                          ▼
                                                     symbol table
                                                  (per-function locals)
```

**Lexer** ([code/lexer.c](code/lexer.c)). Scans the source character by character and groups characters into tokens — keywords, identifiers, numbers, strings, punctuation. Whitespace and `//` comments are dropped. Each token carries its `file:line:col` location so later stages can report errors that point back to the source.

**Parser** ([code/parser.c](code/parser.c)). A recursive-descent parser that walks the token stream following the [Jive EBNF grammar](#grammar-ebnf) and builds an abstract syntax tree. Each grammar rule (`parse_program`, `parse_fn_def`, `parse_block`, `parse_statement`) consumes exactly the tokens it owns. Expressions are handled by a precedence-climbing trio — `parse_primary` (integer literal or `(` expr `)`), `parse_multiplicative` (`* / %`), and `parse_additive` (`+ -`) — that produces a left-associated tree where `*`/`/`/`%` bind tighter than `+`/`-`. Mismatches turn into `file:line:col: error: expected X, got Y` diagnostics. The AST is a tree of `AST_Node`s linked together with `AST_List` (head + tail + count, doubly linked).

Dump the AST with `--dump-ast`:

```
$ ./build/jive jive_programs/simple.jive --dump-ast -o /dev/null
=== ast ===
program
  fn main() -> int
    return
      integer 42
```

**Symbol table** ([code/symbol_table.c](code/symbol_table.c)). A small open-addressed hash table mapping a variable name to a `Symbol` record (name, type, stack-slot index). Each function gets its own table, so locals don't leak between functions. The table starts at capacity 16 and doubles whenever it crosses 50% load; `symbol_table_declare` returns `NULL` when a name is already declared in the current scope, which is how `let a: int = 3 / let a: int = 5` is rejected as a redeclaration. Lookups use FNV-1a, and capacity is always a power of two so the hash maps to a slot with a single mask.

**IR generator** ([code/ir.c](code/ir.c)). Lowers each function's AST into a flat sequence of stack-machine instructions: `FN`, `END_FN`, `PUSH <n>`, `LOAD_LOCAL <slot>`, `STORE_LOCAL <slot>`, `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `RETURN`. Expressions are emitted in post-order, so operands are pushed before the op that consumes them. While lowering a function, the IR generator drives a fresh symbol table — `let` calls `symbol_table_declare` and emits a `STORE_LOCAL` for any initializer, `set` looks the name up and emits a `STORE_LOCAL`, and bare identifiers in expressions become `LOAD_LOCAL`. Names that resolve to nothing (or that are declared twice) become `file:line:col: error: 'a' has not been declared` style diagnostics — the same shape the parser uses, and the driver bails out before codegen if any of them fired. For example, `return 2 + 3 * 4 - 5` becomes:

```
$ ./build/jive jive_programs/expr.jive --dump-ir -o /dev/null
=== ir ===
FN main (locals=0)
PUSH 2
PUSH 3
PUSH 4
MUL
ADD
PUSH 5
SUB
RETURN
END_FN
...
```

A function with locals carries its slot count on `FN`, and `let`/`set`/identifier references show up as `STORE_LOCAL` and `LOAD_LOCAL` ops:

```
$ ./build/jive jive_programs/vars.jive --dump-ir -o /dev/null
=== ir ===
FN main (locals=2)
PUSH 1
STORE_LOCAL 0
LOAD_LOCAL 0
PUSH 5
ADD
STORE_LOCAL 1
LOAD_LOCAL 1
PUSH 2
MUL
STORE_LOCAL 0
...
```

This level decouples "what to compute" from "how to emit machine code" — the IR is small, easy to read with `--dump-ir`, and pleasant to debug.

**Code generator** ([code/codegen.c](code/codegen.c)). Walks the IR linearly and emits NASM. It starts with a fixed `_start` preamble that calls `main` and exits with its return value, then translates each IR op directly: `PUSH n` becomes `push n`, binary ops pop right-hand-side into `rcx` and left-hand-side into `rax`, compute, and re-push. `DIV` / `MOD` use `cqo` + `idiv` for signed 64-bit division. `IR_FN` opens a function with a standard `push rbp / mov rbp, rsp / sub rsp, 8*n_locals` prologue so that local slot N lives at `[rbp - 8*(N+1)]`; `LOAD_LOCAL` becomes `push qword [rbp-…]` and `STORE_LOCAL` becomes `pop qword [rbp-…]`. `IR_RETURN` pops the result into `rax`, then `mov rsp, rbp / pop rbp / ret` tears the frame back down — even if the operand stack still holds residue.

The output for `fn main() -> int { return 42 }` is:

```asm
global _start

_start:
    call main
    mov rdi, rax    ; return code
    mov rax, 60     ; exit syscall
    syscall

main:
    push rbp
    mov rbp, rsp
    push 42   ; PUSH 42

    ; RETURN
    pop rax
    mov rsp, rbp
    pop rbp
    ret
```

Assemble + link with `nasm -felf64` and `ld`, and the resulting ELF exits with code 42.

## Troubleshooting

- **`make: command not found`** — you're inside WSL Ubuntu but haven't installed `build-essential` yet; see step 2.
- **`gcc: error: unrecognized command-line option '-Wpedantic'`** — your `gcc` is very old. Install a newer one via `sudo apt install gcc`.
- **Running on Windows without WSL** — MSYS2/MinGW GCC works but produces `.exe` output and is not the supported path. If you must, add `.exe` to the output name.

## Credits

The **Jive** language was designed by **Professor Lothar Narins** for CS 5008 at Northeastern University. The language design, grammar, and course structure are his work; this repository is my compiler implementation.

## License

See [LICENSE](LICENSE).
