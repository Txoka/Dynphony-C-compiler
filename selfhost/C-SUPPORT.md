# C support in the self-hosted compiler

This document describes C accepted **by the compiler in `selfhost/`**, not the
larger subset accepted by the Python compiler that currently builds it. Every
new self-hosting language feature should be added here with an execution test.

## Current stage: stage 0.12

Stage 0.12 accepts exactly one translation unit containing a `main` definition.
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

Macros and `#include` are not processed yet.

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
- Basic scalar cast syntax and `sizeof` for scalar type names and locals. Casts
  do not yet emit narrowing/sign-extension conversions.
- Local reads and simple `=` assignment expressions.
- Compound blocks, expression statements, `if`/`else`, `while`, `do`, `for`,
  `break`, `continue`, and `return`.
- Prefix and postfix `++`/`--`, plus simple and compound assignments.
- Direct calls to `input`, `output`, `keyboard`, `screen`, `time`, `time_low`,
  `time_high`, `persistent_load`, and `persistent_store`.
- Multiple scalar function definitions, prototypes with named parameters,
  forward calls, nested calls, and recursion. Up to six scalar arguments use
  `r1` through `r6`; parameters and locals are saved in per-call stack frames.
- One-dimensional fixed local arrays with constant bounds, array-to-pointer
  decay, address-of, pointer dereference, and scaled subscripting. Array element
  loads and stores honor `char`, `short`, and word widths.
- File-scope scalar and one-dimensional fixed-array objects, including
  `static`/`extern` declarations, constant scalar initializers, brace array
  initializers, and references declared before or after a function.
- String literals with common escapes and adjacent-literal concatenation.
- Static pointer initializers that refer to named objects or string literals.
- Scaled pointer addition, subtraction, increment, and decrement; subtracting
  compatible pointers returns an element count.
- File-scope enum definitions with optional tags, implicit values, constant
  integer initializers, trailing commas, and use of enumerators in expressions.
- File-scope typedef declarations for scalar integer types. Their names are
  accepted as global, parameter, and local declaration specifiers.
- Named structure definitions with naturally aligned scalar, pointer, fixed
  array, and previously defined structure members. Structure objects can use
  local or static/global storage. `.` and `->` produce member lvalues and may be
  chained through nested structures and self-referential pointers.
- File-scope typedef names for previously defined structure types.
- Runtime arithmetic (including software multiply/divide/remainder), shifts,
  bitwise operations, comparisons, logical operators, conditional expressions,
  and comma expressions.

Compound statements introduce lexical scopes and inner locals may shadow outer
locals. Deep expressions that exhaust scratch registers and calls with
stack-passed arguments are rejected rather than spilled. Multidimensional
arrays, VLAs, unions, anonymous structures, forward structure declarations,
aggregate assignment/arguments/returns, enum-typed object declarations,
pointer typedefs, block-scope typedefs, full scalar conversions, designated
initializers, and multiple translation units remain unsupported.

### Pipeline and generated code

- The lexer and recursive-descent parser build an arena-backed expression AST.
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
a loader-compatible executable record there. This stage accepts exactly one
translation-unit record; multi-unit preprocessing and linking remain pending.

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
