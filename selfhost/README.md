# Symphony compiler in C: self-hosting baseline

This directory holds the self-hosting compiler: a C compiler written in the
subset it compiles. The Python compiler builds it once; after that it rebuilds
itself, and the build requires the self-built stages to be byte-identical. It
is still narrower than the Python compiler and deliberately unoptimized. Its
layout mirrors the Python implementation where that separation is already
useful:

- [C-SUPPORT.md](C-SUPPORT.md) is the exact language support matrix.
- [PROJECT-FORMAT.md](PROJECT-FORMAT.md) specifies persistent project input,
  compiler output, booting, and the future callable compiler API.
- [../ROADMAP.md](../ROADMAP.md) tracks the path to self-hosting and later
  optimization.

```text
selfhost/
  include/symphony/       shared stage interfaces
  src/frontend/           lexer, parser, and semantic evaluation
  src/middle/             tiny IR lowering and optimization
  src/target/symphony/    Symphony and Dynphony image emission
  src/compiler.c          pipeline driver
  src/main.c              DCC1/DCP1 persistent-storage protocol
  examples/               programs accepted by this stage
  tests/                  bootstrap execution tests
  tools/                  host-side bootstrap driver
```

The smallest demo program, `examples/answer.c`, is:

```c
int main(void) {
    return (6 * 7) + (3 << 2) - 2;
}
```

A whole-program constant return like this is folded at compile time into a
16-byte image; other programs go through the runtime code path described below.
The frontend builds an arena-backed AST on the runtime heap, and the backend
emits a runnable Symphony or Dynphony image for the requested 32-bit RAM load
address. [C-SUPPORT.md](C-SUPPORT.md) lists exactly what is accepted.

## Build and run

From this directory:

```sh
make stage0
make demo
make test
```

`make stage0` uses the current Python compiler to produce
`build/scc-stage0.bin`. `make demo` then runs that compiler image in the
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

Every host-side tool takes `--target symphony` (the default) or
`--target dynphony`. The compiler image and the project image must use the same
ISA.

For hardware that cannot export modified persistent storage, pack a project in
one-shot compile-and-run mode:

```sh
python tools/pack_project.py path/to/project -o build/project.pstore \
    --persistent-size 0x1000000 --load-address 0x80000 \
    --run-after-compile --target dynphony
```

Boot the compiler at RAM address 0 with that persistent image. After a
successful build it copies the generated image to `0x80000` and jumps there;
the selected address must not overlap the compiler image.

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
calls, and recursion use the normal Symphony-family ABI. The first seven scalar
arguments use registers and additional arguments use the caller's stack.
Aggregate arguments and returns are subsequent bootstrap layers.

Fixed local arrays now receive byte-accurate frame storage and support decay,
address-of, dereference, and scaled subscripting. This is the shared lvalue
foundation for pointers, structures, VLAs, and heap-backed compiler arenas.
Compound blocks also maintain lexical scope and support local shadowing.

Scalar type spellings, `_Bool`/`<stdbool.h>`, pointer declarators, basic casts
and `sizeof`, and all
low-level device intrinsics from `<symphony.h>` are also accepted. Pointer arithmetic is
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

From the repository root, `make selfhost` writes all three bootstrap artifacts:

- `selfhost/build/scc-stage0.bin`: the C compiler built by Python.
- `selfhost/build/scc-stage1.bin`: the compiler rebuilt by stage 0.
- `selfhost/build/scc-stage2.bin`: the compiler rebuilt by stage 1.

The stages target Symphony by default; pass `--target dynphony` to
`tools/build_stages.py` for Dynphony images. The repository `make selfhost`
target builds these compiler images for RAM address `0`, so load stage 0, stage 1, or stage 2 at address `0` and start the
CPU at address `0`. The compile-and-run project mode must still use a separate
child load address such as `0x80000` so the generated program does not overwrite
the compiler.

The build fails unless stage 1 and stage 2 are byte-identical. The stage numbers
here describe the produced files; older bootstrap documentation may call these
stages 1, 2, and 3 respectively.
