# Dynphony compiler in C: stage 0.7

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
  src/main.c              Dynphony persistent-storage protocol
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

The return expression supports integer and character constants, C arithmetic,
bitwise and comparison precedence, short-circuit logical operators, `?:`, and
comma expressions. It rejects division by zero and invalid shift counts. The
frontend builds an arena-backed AST on the Dynphony heap, the middle end folds
it to a constant-return IR module, and the backend emits a runnable image for
the requested 32-bit RAM load address.

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

`make pack` serializes the entire `selfhost/` source tree into
`build/selfhost.pstore` using DCC1/DCP1. The compiler CLI can attach such an
image while emulating with `--persistent-size`, `--persistent-load`, and
`--persistent-save`.

To compile another source through the C compiler image:

```sh
python tools/bootstrap.py path/to/program.c -o build/program.bin
```

For stage 0.3, the compiler reads one translation unit from DCP1 in persistent
storage and writes the loader-compatible executable record back there. The
format and status protocol are specified in
[PROJECT-FORMAT.md](PROJECT-FORMAT.md). Multi-file records can already be
packed, but compilation currently rejects bundles containing more than one
translation unit.

The dynamic-code path supports stack-backed scalar locals, assignments, all basic
loop forms and loop control, runtime arithmetic/comparison/logical expressions,
and direct `input()`/`output()` calls. Multiplication and unsigned division are
expanded into software instruction sequences. It is intentionally unoptimized
and uses correctness-first stack frames. Scalar functions, forward prototypes,
six register arguments, nested calls, and recursion use the normal Dynphony ABI.
Aggregate types and stack-passed arguments are subsequent bootstrap layers.

Fixed local arrays now receive byte-accurate frame storage and support decay,
address-of, dereference, and scaled subscripting. This is the shared lvalue
foundation for pointers, structures, VLAs, and heap-backed compiler arenas.

Scalar type spellings, pointer-shaped local declarators, basic casts and
`sizeof`, and all low-level Dynphony device intrinsics are also accepted. Full
pointer semantics and scalar conversion rules are not implemented yet.

## Next bootstrap stages

The next useful increments are declarations and local variables, statements and
control flow, functions, pointers/arrays/structures, then multiple translation
units and preprocessing. Once this C implementation accepts all constructs used
by its own sources, its emitted compiler can compile the same sources again; a
reproducible stage-1/stage-2 binary comparison will then establish self-hosting.
