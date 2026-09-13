# C support in the self-hosted compiler

This document describes C accepted **by the compiler in `selfhost/`**, not the
larger subset accepted by the Python compiler that currently builds it. Every
new self-hosting language feature should be added here with an execution test.

## Current stage: self-hosting baseline

The compiler accepts one or more translation-unit records containing a single
linked `main` definition and can reproduce its own compiler image.
The original constant-return form remains supported:

```c
int main(void) {
    return (6 * 7) + (3 << 2) - 2;
}
```

`int main()` is accepted as an equivalent spelling. Function prototypes,
function definitions, and file-scope object declarations may surround it.

### Lexical support

- ASCII whitespace.
- `//` line comments and `/* ... */` block comments.
- The complete keyword/operator/punctuation vocabulary needed by the planned
  subset is tokenized, with source offsets and lengths.
- General identifiers and adjacent string literals are tokenized and consumed.
- Decimal, octal, and hexadecimal integer constants with `u`/`l` suffixes.
- Character constants and common single-character escapes.

The project preprocessor supports quoted and include-root `#include`, object
and function-like macros with up to eight parameters, macro redefinition,
`#undef`, project definitions, include guards,
`#pragma once`, recursive-include rejection, `#error`, and conditional groups.
Object replacements and function arguments are recursively expanded with
self-recursion suppression.
Backslash-newline logical-line continuation is supported in directives and
ordinary source lines.
Conditional groups support `#if` and `#elif` integer expressions, including
`defined`, unary, arithmetic, shift, comparison, bitwise, and logical operators.

### Grammar and semantics

- Exactly one return statement in `main`.
- Parenthesized expressions.
- Unary `+`, unary `-`, and bitwise complement `~`.
- Binary arithmetic, shifts, and bitwise operations with C precedence and
  left associativity.
- Comparisons, logical negation, short-circuit `&&`/`||`, conditional `?:`, and
  comma expressions.
- Values are evaluated as wrapping 32-bit unsigned integers.
- Division or remainder by zero is rejected.
- Shift counts greater than or equal to 32 are rejected.

The runtime-code path additionally supports:

- Stack-backed function-scope scalar locals declared as `int`, `unsigned int`,
  `signed int`, `char`, short/long integer spellings, or pointer-shaped scalar
  declarators, with optional `const` and initializers.
- `_Bool` and `<stdbool.h>` (`bool`, `true`, and `false`). Conversion from any
  scalar value produces exactly zero or one for casts, initializers,
  assignments, function returns, and register- or stack-passed parameters.
- Function-scope `static` scalar integer, pointer, and fixed-array objects with
  constant/brace, null, object-address, or string initialization; each
  declaration has independent data-image storage shared across calls.
- Basic scalar cast syntax and `sizeof` for scalar type names and locals.
  Pointer casts retain their target pointee size for subsequent arithmetic;
  non-boolean scalar casts do not yet emit narrowing/sign-extension
  conversions.
- Local reads and simple `=` assignment expressions.
- Compound blocks, expression statements, `if`/`else`, `while`, `do`, `for`,
  `break`, `continue`, and `return`.
- Prefix and postfix `++`/`--`, plus simple and compound assignments.
- Direct calls to `input`, `output`, `keyboard`, `screen`, `time`, `time_low`,
  `time_high`, `persistent_load`, and `persistent_store`.
- Multiple scalar function definitions, prototypes with named parameters,
  forward calls, nested calls, and recursion. The first six scalar arguments
  use `r1` through `r6`; later arguments are passed on the stack. Parameters and
  locals are saved in per-call stack frames.
- Fixed local arrays with constant bounds, array-to-pointer
  decay, address-of, pointer dereference, and scaled subscripting. Array element
  loads and stores honor `char`, `short`, and word widths.
- File-scope scalar and fixed-array objects, including
  `static`/`extern` declarations, constant scalar initializers, brace array
  initializers, and references declared before or after a function.
- String literals with common escapes and adjacent-literal concatenation.
- Static pointer initializers that refer to named objects or string literals.
- Scaled pointer addition, subtraction, increment, and decrement; subtracting
  compatible pointers returns an element count.
- File- and block-scope enum definitions with optional tags, implicit values,
  constant integer initializers, trailing commas, typed objects/parameters,
  casts and `sizeof`, and lexically scoped tags and enumerators.
- File- and block-scope typedef declarations for scalar integer, structure, and
  pointer types. Block aliases obey lexical lifetime and may shadow outer aliases;
  their names are accepted as declaration specifiers.
- Named structure definitions with naturally aligned scalar, pointer, fixed
  array, and previously defined structure members. Structure objects can use
  local or static/global storage. `.` and `->` produce member lvalues and may be
  chained through nested structures and self-referential pointers.
- Forward declarations for named structures, plus file-scope typedef names for
  scalar, structure, and pointer types. Opaque structure pointer typedefs are
  updated when the later definition supplies their element size.
- Fixed multidimensional arrays in local, global/static, and structure-member
  storage. Declarators retain every extent and row stride; intermediate
  subscripts decay to appropriately scaled row pointers.
- Local variable-length arrays, including multidimensional runtime bounds,
  runtime row-stride calculation, array-parameter decay, and runtime `sizeof`.
  Storage is allocated from the function stack when the declaration executes.
- Runtime arithmetic (including software multiply/divide/remainder), shifts,
  bitwise operations, comparisons, logical operators, conditional expressions,
  and comma expressions.
- Freestanding `malloc`, `free`, `calloc`, and `realloc` with aligned blocks,
  free-list reuse, splitting, adjacent-block coalescing, overflow checks, and
  data-preserving growth. `memcpy`, overlap-safe `memmove`, `memset`, and
  `memcmp` are exposed by `<string.h>`.

Compound statements introduce lexical scopes and inner locals may shadow outer
locals. Deep expressions that exhaust scratch registers are rejected rather
than spilled. VLA storage is currently reclaimed on function return rather
than at the end of its declaring block. Unions, anonymous structures, aggregate
assignment/arguments/returns, full scalar conversions, designated initializers,
and translation-unit-private linkage remain unsupported. Multiple translation
units are preprocessed, merged, and resolved in deterministic project order.

### Pipeline and generated code

- The lexer and recursive-descent parser build an arena-backed expression AST.
- A lexer prepass sizes AST, symbol, and dimension arenas from token/name counts
  rather than raw source bytes, keeping self-compilation within 16 MiB RAM.
- Semantic lowering retains the AST for unoptimized runtime code generation;
  a whole-program constant-return case is still evaluated during bootstrap.
- `dyn_optimize` is intentionally a no-op.
- The Dynphony backend emits direct, unoptimized ALU and branch instructions.
  Constant-return programs are 16 bytes when their halt address fits a 16-bit
  immediate and 27 bytes otherwise; dynamic image size depends on the source.
- Dynamic images initialize `sp`, call `main` through the normal ABI, and emit
  frame prologues/epilogues and call/return-address fixups.
- Static objects and pooled strings are appended with alignment after executable
  code, and address relocations are resolved against the requested load address.
- The compiler itself uses `malloc`, `calloc`, and `free` while running, so its
  AST and source buffers exercise the Dynphony heap.

## Current callable implementation interfaces

These are internal bootstrap interfaces, not a stable public ABI:

```c
void dyn_lexer_init(struct DynLexer *, const char *, unsigned int);
void dyn_lexer_next(struct DynLexer *);
int dyn_parse(const char *, unsigned int, struct DynAstProgram *);
int dyn_evaluate(const struct DynAstProgram *, unsigned int, unsigned int *);
int dyn_lower(const struct DynAstProgram *, struct DynIrModule *);
void dyn_optimize(struct DynIrModule *);
void dyn_emit_image(const struct DynIrModule *);
int dyn_compile_buffer(const char *, unsigned int);
```

The executable `main` reads DCC1 and DCP1 through persistent storage and writes
a loader-compatible executable record there. This stage merges translation-unit
records in deterministic DCP1 order and resolves ordinary external function and
object references over that merged program. General private file scope,
including automatic `static` isolation, remains pending.

## Definition of self-hostable

The compiler becomes self-hostable when it can consume the complete `selfhost/`
project bundle, preprocess and link all its translation units, and emit another
compiler image. The test is:

1. The Python compiler builds stage 1 from the C sources.
2. Stage 1 compiles the same source bundle into stage 2.
3. Stage 2 compiles the bundle into stage 3.
4. Stage 2 and stage 3 are byte-identical, or any intentional nondeterministic
   metadata is excluded from the comparison.

Optimization is not required for this milestone. Correct parsing, typing,
linking, lowering, layout, and deterministic code generation are required.
The automated bootstrap test performs these stages and requires stage 2 and
stage 3 to be byte-identical.
