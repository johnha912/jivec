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

## Language overview

A minimal Jive program:

```jive
fn main() -> int
{
    return 42
}
```

- **Keywords:** `fn` `let` `set` `if` `while` `call` `return` `true` `false`
- **Types:** `int` `str` `bool`
- **Symbols:** `+ - * / % & | ^ == != < > <= >= && || ~ ! ( ) [ ] { } , : ->`
- **Comments:** `// ...` to end of line

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
