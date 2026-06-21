# Pie

Pie is a programming language that compiles to native Linux x64 executables via NASM. It features ownership and borrowing without lifetime syntax, Result/Option error handling, a built-in package manager, and a VS Code extension with language server support.

## Status

Pie is under active development. The compiler (lexer, parser, semantic analysis, borrow checking, lowering, and x64 code generation) is functional with 240+ passing tests.

Currently supported:

- Functions, closures, generics with monomorphization
- Structs, enums, interfaces, type aliases
- Ownership, borrowing (`&T`, `&mut T`), regions, unsafe blocks
- Control flow: if/else, while, do-while, for, break, continue, defer, match, labels
- Operators: arithmetic, comparison, logical, bitwise, string concat, ternary, cast, range, inc/dec
- Collections: lists, maps, tuples, ranges
- Primitives: int, float, string, char, bool, Maybe/Option, Result, null, void
- String interpolation, string methods
- Module system with `require`
- Package manager: `pie new`, `pie add`, `pie remove`
- Standard library scaffold: io, print, assert, threads, format
- Match expressions with value semantics, `?`/`try` operators
- Borrow checker: move semantics, shared/mutable borrow tracking, escape analysis

## Quick start

```pie
require "io"

fn main() -> int:
    x: int -> 40 + 29
    println("hello from Pie: ", x)
    return 0
end
```

## Building from source

### Prerequisites

- Linux x86-64
- CMake >= 3.20
- NASM
- GCC or Clang (C11)
- Python 3 (for code generators)
- Ninja

### Build

```bash
cmake -B build -G Ninja
ninja -C build
```

This produces two executables:

- `build/piec` -- low-level compiler (emit-asm, emit-ir)
- `build/pie` -- CLI tool (build, run, test, check, new, add, remove)

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

## CLI usage

```
pie --version
pie features
pie check [input.pie]
pie build [input.pie] [-o output] [--keep-asm]
pie run [input.pie]
pie test [input.pie]
pie new app <name>
pie new lib <name>
pie add <package>[@version]
pie remove <package>
```

## Project structure

```
include/pie/            Public C headers
src/core/               Compiler core (lexer, parser, sema, IR, modules, diagnostics)
src/features/           Feature capsules (40 feature groups, ~141 source files)
src/middle/             Middle-end passes (borrow checker, lowering)
src/backend/asm/        x64 NASM code generation
runtime/                Pie runtime (C + NASM startup, print, memory, I/O)
tools/                  Python code generators and validators
tests/                  Test suite (170+ tests, fixtures, packages)
pie-vscode/             VS Code extension (syntax highlighting, language server)
docs/                   Documentation
```

## License

Apache License 2.0. See [LICENSE](LICENSE).
