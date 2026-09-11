# Dynphony C compiler — first version

A runnable Python compiler for a useful C subset. It emits **flat, big-endian Dynphony binaries** directly, using the supplied ISA in [docs/isa.txt](docs/isa.txt). No assembler or linker is needed.

The implementation separates syntax parsing, semantic analysis, typed syntax, IR lowering, optimization, instruction selection, and binary layout. It includes a reference emulator and automated execution/encoding tests.

## Run it

Requires Python 3.10+ and `pycparser`. From this project's directory:

```sh
python -m pip install -e .
python -m dynphony examples/demo.c -o demo.bin --run
```

Expected output includes `main returned 146`. Installing the package also provides the `dyncc` command. If `pycparser` is already installed, `python -m dynphony` works directly without installing the project.

Generate a position-independent image and execute it at another address:

```sh
python -m dynphony examples/demo.c -o demo.pic.bin \
  --pic --run --run-address 0x12345 \
  --emit-ir demo.ir --map demo.map.json
```

Compile for a fixed nonzero address, with configurable memory sizes:

```sh
python -m dynphony examples/demo.c -o demo.bin \
  --load-address 0x10000 --ram-size 0x100000 \
  --persistent-size 0x10000
```

The raw file begins with the first instruction; it is **not padded to the load address**. Load the file's first byte at the selected address and begin execution there. PIC images can instead run at any address where the image fits contiguously and leaves enough room for the stack. `--run-address` only controls emulator placement. JSON maps contain absolute symbols for fixed-address images and image-relative offsets for PIC images.

Run the tests:

```sh
python -m unittest discover -s tests -v
```

## Supported language

The complete support matrix, known limitations, and Dynphony-specific built-ins
are documented in [docs/c-language-support.md](docs/c-language-support.md).

- Plain `char` is unsigned; explicit signed/unsigned `char`, `short`, `int`, and `long` are supported.
- Pointers, pointers to pointers, function pointers, explicit integer/pointer casts, and `void` functions/pointers.
- Local variables and lexical scopes; file-scope globals, `static` globals/functions, static locals, external declarations resolved within this translation unit, and file/block-scope typedefs.
- Named and anonymous structures, self-referential structure pointers, natural member layout, `.`/`->`, nested structure/array members, and brace initialization for structure objects.
- Enumerations with implicit or integer-constant enumerator values.
- `const` objects and pointers with qualifier-preserving conversions and modification diagnostics.
- Decimal/octal/hex integer literals, character literals, ordinary single-byte strings, and comments.
- Arithmetic `+ - * / %`, bitwise operations, shifts, comparisons, logical operators, prefix/postfix increment/decrement, assignment and compound assignment.
- Short-circuit `&&`/`||`, conditional `?:`, comma expressions, and unevaluated `sizeof`.
- Conditional 96×40 ASCII text-screen support through literal-format `printf`,
  `screen_framebuffer`, and `screen_cursor`.
- `if`/`else`, `while`, `for`, `do`/`while`, `break`, `continue`.
- Functions, direct/indirect calls, recursion, and returns. The first six scalar arguments use registers; later scalar arguments are passed on the stack.
- Fixed-size and multidimensional arrays, inferred outer array bounds, brace/string initializers, array indexing/decay, pointer scaling/difference, dereference, and address-of.
- Zero-filled globals, partially initialized arrays, integer constant initializers, and symbolic pointer initializers such as `int *p = &a[2]`.
- Software multiplication and signed/unsigned division/remainder. Division is bounded to 32 iterations, including for large unsigned divisors.
- Header-free built-in functions for Dynphony I/O, time, and persistent memory.

Entry must be `int main(void)` or `int main()`. In this version, an empty parameter list is treated as exactly zero parameters. Falling off `main` returns zero. Other non-void functions also get a deterministic zero fallthrough, although callers must not rely on this for portable C.

## Machine and ABI

| Property | Value |
|---|---|
| Registers / byte addresses | 32 bits |
| Instruction immediates | 16 bits, unsigned |
| Byte order | Big-endian |
| Loads | 8/16/32 bits; narrow loads zero-extend |
| Unaligned memory access | Allowed |
| `char` / `short` / `int` / `long` | 1 / 2 / 4 / 4 bytes |
| Pointers | 4 bytes |
| Object alignment | Natural, capped at 4 bytes |
| Arguments / return | first six in `r1`–`r6`, later arguments on stack / `r1` |
| Caller-saved | `r1`–`r7`, `flags` |
| Callee-saved | `r8`–`r13` |
| Stack | `sp = 0` at startup, downward, 4-byte aligned |
| RAM | Unified; addresses wrap modulo configured power-of-two size |
| Default RAM / load address | 16 MiB / 0 |
| Termination | Infinite jump loop; main's result remains in `r1` |

`r12` is the frame pointer when a function needs a stack frame. Such functions save and restore it; frame-free leaf functions leave it alone and omit that prologue and epilogue work. `r8`–`r11` remain untouched. In PIC mode, startup obtains the image base in `r13` using `counter` at image offset zero; functions leave it unchanged. The `_start` IR root initializes `sp` when reachable code can use the stack; the optimizer removes that operation from fully stack-free images.

Large constants and all label addresses use fixed-width materialization, avoiding a 64 KiB code/address limit. PIC addresses add the runtime base. Startup initializes pointer-valued globals from symbol offsets on every entry; it never repeatedly adds a base to previously rebased values. Other mutable globals are not reset on reentry unless the image is reloaded.

RAM size participates in layout diagnostics and emulator configuration. Persistent size is validated and recorded in the map. It does not partition main RAM into persistent and volatile regions.

## Built-in device functions

These functions are always declared by the compiler. Source files do not need a
header, and each call emits the matching Dynphony instruction without ordinary
function-call overhead:

```c
unsigned int input(void);                         /* in */
void output(unsigned int value);                  /* out */
unsigned int keyboard(void);                      /* keyboard */
void screen(unsigned int setting, unsigned int value);
unsigned int time(void);                          /* low 32 bits */
unsigned int time_low(void);                      /* low 32 bits */
unsigned int time_high(void);                     /* high 32 bits */
unsigned int persistent_load(unsigned int address);
void persistent_store(unsigned int address, unsigned int value);
```

For example:

```c
int main(void) {
    unsigned int value = input();
    output(value + keyboard());
    screen(2, value);
    persistent_store(0, value);
    return time();
}
```

`input()` and `keyboard()` return zero in the reference emulator when their
queues are empty. `output()` appends to `machine.outputs`, and `screen()` appends
`(setting, value)` to `machine.screen_updates`. Set `persistent_size` on the
compiler target to record and validate the hardware size; pass the same size to
`Machine` for emulation. The CLI does this automatically when `--run` is used.
Device addresses retain the hardware's wrapping behavior. These names are
reserved and cannot be used for user-defined functions.

## Explicit first-version limits

This is a C subset compiler, not a conforming full C implementation. Unsupported constructs produce diagnostics where encountered:

- No preprocessor (`#include`, `#define`, etc.), headers, standard library, heap, or separate linking.
- No 64-bit `long long`, floating point, unions, bit-fields, or variadic functions.
- No `volatile` or `restrict`, local `extern`, designated initializers, `switch`, `goto`, or inline assembly. VLAs support an outermost runtime bound; VLA `sizeof` and inner runtime bounds remain incomplete.
- No aggregate arguments/returns or old-style function definitions.
- Structure assignment is not implemented. Aggregate initializers require nested braces; brace elision and designated initialization are not implemented. Array bounds must be compile-time constants. Multiple tentative global definitions are rejected rather than merged.
- Decimal literals above `2147483647` need an explicit `U` suffix because unsuffixed decimal values would require an unsupported 64-bit C type. Write the minimum signed integer as `(-2147483647 - 1)` or cast `0x80000000u`.
- Strings use ordinary single-byte characters and escapes; no wide/Unicode literal types. String literals reside in writable unified memory, but modifying one is still C undefined behavior.
- The optimizer promotes non-escaping scalar locals, propagates copies and constants, folds scalar expressions, simplifies control flow, rematerializes constants and addresses, selects immediate ALU forms, removes unused pure values and functions, strength-reduces power-of-two arithmetic, and includes only reachable arithmetic helpers.
- The compiler checks static image fit and a single largest frame, but cannot guarantee stack capacity across recursion or nested calls. Stack collision and out-of-bounds accesses are not trapped by generated code.

Signed overflow, invalid shifts, invalid pointer operations, and division by zero remain C undefined behavior. The compiler does not exploit signed-overflow UB for optimization. Software division by zero deterministically returns zero; signed minimum divided by minus one wraps in the helper. Those are implementation behaviors, not portable guarantees.

## Current optimization scope

The optimizer currently applies safe local and whole-program reductions:

- Small integer constants use one immediate instruction; small negative constants use immediate subtraction from `zr`.
- Constants, global addresses, and local addresses are regenerated at their uses instead of occupying stack slots.
- ALU operations and comparisons use immediate forms when the operand fits 16 bits.
- Multiplication by a positive power of two becomes a left shift. Unsigned division and remainder by powers of two become a right shift and mask.
- Unused pure IR values are removed. Calls and memory/control-flow operations are retained.
- Non-escaping scalar locals become IR values. Copies and constants propagate within basic blocks; constant arithmetic, comparisons, casts, and branches are folded.
- Comparisons used only by a branch remain in flags and branch directly, without constructing, spilling, and retesting a Boolean value.
- Instructions after an unconditional transfer are removed through the next block boundary. Jumps are threaded through forwarding blocks, jumps to any immediately following label are removed, and conditional branches use the following block as fallthrough.
- Startup is represented by the `_start` IR root. Functions and software arithmetic helpers unreachable from it, calls, function pointers, or static relocations are omitted.
- Functions without locals, parameters, or live computed stack values omit the `r12` frame-pointer save/restore.
- Read-only parameters whose addresses are never taken become ordinary IR values. A local register allocator keeps straight-line leaf expressions in `r1`–`r7`, preferring their incoming argument registers. For example, `int add(int a, int b) { return a + b; }` begins with `add r1, r1, r2` and needs no frame.
- Functions with control flow assign up to four frequently used values to `r8`–`r11`. Each function saves and restores only the callee-saved registers it actually uses, so those values survive calls and loop backedges.
- Values that die at a call can use `r3`–`r6` without save/restore traffic. Call arguments are placed as a parallel assignment, including register-cycle breaking and a safe mixed-source fallback.
- A global Tier 1 fixed point alternates local/CFG simplification with call-graph reachability. Non-recursive functions with exactly one surviving direct call site are relocated into that site and their standalone body is deleted. This naturally absorbs `main` into `_start` when possible.
- Known-symbol calls use explicit direct-call IR, while function-pointer calls remain indirect. This gives reachability, inlining, and tail-call analysis the callee symbol directly.
- Explicit CFG construction records predecessors and successors, selects one-target conditional branches with an implicit fallthrough edge inside the fixed point, removes unreachable blocks after branch folding, and deletes unused labels. Straight-line intrinsic functions use the leaf allocator, so input parameters can remain in their incoming registers.
- Safe tail calls restore the current frame and jump directly to the callee. Functions with addressable local objects stay on the ordinary call path because a callee may receive a pointer into that frame.
- After final layout, all symbolic fixed-address branches and direct calls relax to their shortest legal immediate or register-target encoding. Shrinking is repeated until instruction sizes and label addresses are stable, with no unreachable padding retained.
- Termination repeats one jump instruction. Fixed low-address images use `jmp immediate`; PIC and high-address images materialize the target once outside the loop and repeat `jmp r7`.

The next substantial opportunities are dead-global elimination, immutable-global load folding, bounded compile-time evaluation, loop analysis, paired division/remainder, common-subexpression elimination, and control-flow-aware stack-slot reuse. Stack-slot reuse must use control-flow liveness rather than textual instruction intervals because loop backedges make the latter incorrect. The dependency-ordered checklist is in [docs/optimization-todos.md](docs/optimization-todos.md).

## Project structure

```text
dynphony/
  frontends/
    protocol.py      source-language frontend contract
    c/               C parsing, semantics, and common-IR lowering
  middle/
    model.py         shared types, symbols, typed nodes, and static objects
    ir.py            canonical language-neutral IR
    analysis/        CFG and reusable middle-end analyses
    passes/          fixed-point manager and optimization passes
  targets/dynphony/
    registers.py     architectural register names
    abi.py           C calling-convention roles
    isa.py           instruction names and byte encoders
    config.py        target options and image metadata
    assembler.py     symbols, relocations, and final relaxation
    backend.py       instruction selection, registers, and stack frames
  runtime/           device declarations and selectively linked helpers
  emulator/          independent reference machine and device model
  compiler.py        frontend-independent pipeline orchestrator
  cli.py             raw binary, IR dump, JSON map, optional execution
examples/       C source and precompiled demonstration binaries
```

Thin root-level compatibility modules preserve imports such as `dynphony.isa`
and `dynphony.frontend`. Tests live in `tests/`; the original ISA and design
notes live in `docs/`.

`examples/towers_of_hanoi.c` is a recursive controller for the Turing Complete
magnet puzzle. It reads the highest disk number, source, destination, and spare
locations from the first four inputs and emits the magnet-control sequence.

`examples/constant_folding.c` demonstrates whole-entry constant collapse. Its
three locals and `a + b * c` expression compile to `mov r1, 1466` followed by
the halt jump; no multiplication helper or standalone `main` remains.

`examples/interprocedural_constant_folding.c` computes the same value through a
sole-called `foo(int)`. Function relocation exposes its argument and locals to
the global fixed point, producing the identical 8-byte result.

`examples/arena_allocator.c` supplies bytewise memory set/copy functions and an
aligned bump allocator built from `struct Arena`. It demonstrates structures,
`const` pointers, `sizeof`, `void *` conversions, and allocation without an
operating system. A bundled, reference-on-demand `malloc`/memory runtime remains
on the roadmap; applications currently have to include their allocator source.

The API exposes each pipeline stage:

```python
from dynphony import Target, compile_source

result = compile_source("int main(void) { return 6 * 7; }", target=Target(pic=True))
raw_bytes = result.image.binary
print(result.ir.dump())
print(result.image.metadata())
# result.parsed: pycparser AST; result.typed: independent typed AST
```

The emulator verifies generated binaries, and encoding tests compare with the supplied ISA text. Its flags model implements the specified signed/unsigned branch relations without assuming a hardware flag bit layout. **Execution on the actual Turing Complete circuit has not been verified.** See [docs/design.md](docs/design.md) for extension points and validation details.
