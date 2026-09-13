# Dynphony compiler in C: self-hosting baseline

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

The compiler reads translation units from DCP1 in persistent storage and writes
the loader-compatible executable record back there. The
format and status protocol are specified in
[PROJECT-FORMAT.md](PROJECT-FORMAT.md). Multiple source records are
preprocessed and merged in deterministic order, providing cross-file symbol
resolution. General translation-unit-private file scope is not implemented yet.

The dynamic-code path supports stack-backed scalar locals, assignments, all basic
loop forms and loop control, runtime arithmetic/comparison/logical expressions,
and direct device intrinsics. Multiplication and unsigned division are expanded
into software instruction sequences. It is intentionally unoptimized and uses
correctness-first stack frames. Scalar functions, forward prototypes, nested
calls, and recursion use the normal Dynphony ABI. The first six scalar
arguments use registers and additional arguments use the caller's stack.
Aggregate arguments and returns are subsequent bootstrap layers.

Fixed local arrays now receive byte-accurate frame storage and support decay,
address-of, dereference, and scaled subscripting. This is the shared lvalue
foundation for pointers, structures, VLAs, and heap-backed compiler arenas.
Compound blocks also maintain lexical scope and support local shadowing.

Scalar type spellings, pointer declarators, basic casts and `sizeof`, and all
low-level Dynphony device intrinsics are also accepted. Pointer arithmetic is
scaled by the pointed-to type, including after pointer casts. File-scope scalar
and fixed-array objects,
`static`/`extern`, brace array initializers, string literals, and static pointer
relocations are emitted in an aligned data image after the code.
File-scope enum definitions provide implicit and explicitly initialized integer
constants, including references to earlier enumerators.
File-scope typedefs can name scalar integer types and are accepted in global,
parameter, and local declarations.
Named structures now have aligned member layout, local/global storage, fixed
array members, nested members, self-referential pointers, and chained `.`/`->`
lvalues. A previously defined structure can also receive a file-scope typedef.
Forward structure declarations and pointer typedefs are supported, including an
opaque pointer typedef completed by a later structure definition.
Fixed multidimensional arrays carry explicit per-dimension strides and work in
local frames, static storage, and structure members. Local variable-length
arrays compute their allocation size and multidimensional row strides at run
time; `sizeof` preserves those runtime sizes, including through array
parameters.

The freestanding runtime provides `malloc`, `free`, `calloc`, `realloc`,
`memcpy`, `memmove`, `memset`, and `memcmp`. Freed blocks are reused, adjacent
free blocks are coalesced, and growing `realloc` preserves existing bytes.

## Next bootstrap stages

The compiler is self-hosting: stage 1 emits stage 2, stage 2 emits stage 3, and
the automated parity test requires stages 2 and 3 to be byte-identical. The next
useful increments are translation-unit-private linkage, fuller aggregate and
scalar conversion semantics, function pointers, and exact block-scope VLA
lifetime.
