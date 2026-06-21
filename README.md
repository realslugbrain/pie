# Pie

Pie is a programming language that compiles to native Linux x64 executables via NASM. It features ownership and borrowing without lifetime syntax, Result/Option error handling, a built-in package manager, and a VS Code extension with language server support.

## Status

Pie is under active development. The compiler is functional with 240+ passing tests.

Currently supported:

* Functions, closures, generics with monomorphization
* Structs, enums, interfaces, type aliases
* Ownership, borrowing, regions, unsafe blocks
* If/else, while, do-while, for, break, continue, defer, match, labels
* Arithmetic, comparison, logical, bitwise, string, ternary, cast, range, inc/dec operators
* Lists, maps, tuples, ranges
* int, float, string, char, bool, Maybe/Option, Result, null, void
* String interpolation and string methods
* Module system with `require`
* Package manager: `pie new`, `pie add`, `pie remove`
* Standard library scaffold: io, print, assert, threads, format
* Borrow checker with move semantics, borrow tracking, and escape analysis

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

* Linux x86-64
* CMake >= 3.20
* NASM
* GCC or Clang with C11 support
* Python 3
* Ninja

### Build the compiler

```bash
cmake -B build -G Ninja
ninja -C build
```

This produces:

* `build/piec` — low-level compiler
* `build/pie` — CLI tool

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

## CLI usage

```bash
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

## VS Code extension

The VS Code extension is in `pie-vscode/`.

### Build the extension

```bash
cd pie-vscode
npm install
npm run check
npm test
npm run bundle
```

### Package the extension

```bash
npm run package
```

This creates a `.vsix` file.

### Install locally

```bash
code --install-extension pie-language-*.vsix
```

The extension provides syntax highlighting, snippets, formatting, commands, diagnostics, and language-server support for `.pie` files.

For best diagnostics, make sure the Pie CLI is available as `pie` on your `PATH`, or configure the compiler path in VS Code:

```json
{
  "pie.compiler.path": "/absolute/path/to/build/pie"
}
```

## Project structure

```text
include/pie/            Public C headers
src/core/               Compiler core
src/features/           Feature capsules
src/middle/             Borrow checker and lowering
src/backend/asm/        x64 NASM code generation
runtime/                Pie runtime
tools/                  Python generators and validators
tests/                  Test suite
pie-vscode/             VS Code extension
docs/                   Documentation
```

## License

Apache License 2.0. See [LICENSE](LICENSE).
