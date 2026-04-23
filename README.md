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
- [ ] Stage 2 — parser
- [ ] Stage 3 — type checker
- [ ] Stage 4 — NASM code generator

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
make
```

If the build succeeds you will see the binary at `build/test_lexer`.

Prefer to invoke `gcc` directly? The project is a **unity build** (each executable is a single translation unit that `#include`s the `.c` files it needs), so one command is enough:

```sh
mkdir -p build
gcc -Wall -Wextra -Wpedantic -std=c11 -O0 -g \
    -o build/test_lexer src/test_lexer.c
```

### 4. Run the lexer on an example

```
$ ./build/test_lexer examples/simple.jive
examples/simple.jive:1:1    KEYWORD      fn
examples/simple.jive:1:4    IDENTIFIER   main
examples/simple.jive:1:8    (
examples/simple.jive:1:9    )
examples/simple.jive:1:11   ->
examples/simple.jive:1:14   TYPE         int
examples/simple.jive:2:1    {
examples/simple.jive:3:5    KEYWORD      return
examples/simple.jive:3:12   INTEGER      42
examples/simple.jive:4:1    }
examples/simple.jive:5:1    EOF
```

More sample programs live in [`examples/`](examples/).

### 5. Write your own Jive program

Create a file `hello.jive`:

```jive
fn main() -> int
{
    // comments are ignored
    return 7 + 35
}
```

Then lex it:

```sh
./build/test_lexer hello.jive
```

## Project layout

```
src/
  lexer.h        token and public API definitions
  lexer.c        lexer implementation
  test_lexer.c   unity-build entry for the lexer test driver
examples/        sample .jive programs
Makefile         build rules
build/           compiled binaries (git-ignored)
```

## Troubleshooting

- **`make: command not found`** — you're inside WSL Ubuntu but haven't installed `build-essential` yet; see step 2.
- **`gcc: error: unrecognized command-line option '-Wpedantic'`** — your `gcc` is very old. Install a newer one via `sudo apt install gcc`.
- **Running on Windows without WSL** — MSYS2/MinGW GCC works but produces `.exe` output and is not the supported path. If you must, add `.exe` to the output name.

## Credits

The **Jive** language was designed by **Professor Lothar Narins** for CS 5008 at Northeastern University. The language design, grammar, and course structure are his work; this repository is my compiler implementation.

## License

See [LICENSE](LICENSE).
