# Dynphony compiler in C: stage 0

This directory is the start of the self-hosting compiler. Its layout mirrors the
Python implementation where that separation is already useful:

- [C-SUPPORT.md](C-SUPPORT.md) is the exact language support matrix.
- [PROJECT-FORMAT.md](PROJECT-FORMAT.md) specifies persistent project input,
  compiler output, booting, and the future callable compiler API.
- [../ROADMAP.md](../ROADMAP.md) tracks the path to self-hosting and later
  optimization.

```text
selfhost/
  include/dynphony/       shared stage interfaces
  src/frontend/           lexer, parser, and semantic evaluation
  src/middle/             tiny IR lowering and optimization
  src/target/dynphony/    Dynphony image emission
  src/compiler.c          pipeline driver
  src/main.c              Dynphony input/output protocol
  examples/               programs accepted by this stage
  tests/                  bootstrap execution tests
  tools/                  host-side bootstrap driver
```

This is deliberately a real but narrow first stage, not yet a duplicate of the
Python compiler. It accepts a complete program of this form:

```c
int main(void) {
    return (6 * 7) + (3 << 2) - 2;
}
```

The return expression supports decimal, octal, and hexadecimal integer
constants, parentheses, unary `+ - ~`, and binary `* / % + - << >> & ^ |` with
C precedence. It rejects division by zero and invalid shift counts. The frontend
builds an arena-backed AST on the Dynphony heap, the middle end folds it to a
constant-return IR module, and the backend emits a runnable 16-byte Dynphony
image.

## Build and run

From this directory:

```sh
make stage0
make demo
make test
```

`make stage0` uses the current Python compiler to produce
`build/dyncc-stage0.bin`. `make demo` then runs that compiler image in the
emulator, feeds it `examples/answer.c`, writes `build/answer.bin`, and runs the
generated program.

To compile another source through the C compiler image:

```sh
python tools/bootstrap.py path/to/program.c -o build/program.bin
```

For stage 0 only, the compiler image receives the source length as its first `input()`, followed
by one source byte per `input()`. On success it sends each generated binary byte
through `output()` and returns status 0. Status 1 is oversized input, 2 is heap
exhaustion, 4 is syntax/lexing failure, and 5 is an invalid constant expression.
This temporary transport will be replaced by the persistent DCC1/DCP1/DCO1
layout in [PROJECT-FORMAT.md](PROJECT-FORMAT.md).

## Next bootstrap stages

The next useful increments are declarations and local variables, statements and
control flow, functions, pointers/arrays/structures, then multiple translation
units and preprocessing. Once this C implementation accepts all constructs used
by its own sources, its emitted compiler can compile the same sources again; a
reproducible stage-1/stage-2 binary comparison will then establish self-hosting.
