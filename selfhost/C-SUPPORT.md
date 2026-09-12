# C support in the self-hosted compiler

This document describes C accepted **by the compiler in `selfhost/`**, not the
larger subset accepted by the Python compiler that currently builds it. Every
new self-hosting language feature should be added here with an execution test.

## Current stage: stage 0.6

Stage 0.3 accepts exactly one translation unit containing one `main` definition.
The original constant-return form remains supported:

```c
int main(void) {
    return (6 * 7) + (3 << 2) - 2;
}
```

`int main()` is accepted as an equivalent spelling. No declaration, statement,
or token may appear before or after this definition.

### Lexical support

- ASCII whitespace.
- `//` line comments and `/* ... */` block comments.
- The complete keyword/operator/punctuation vocabulary needed by the planned
  subset is tokenized, with source offsets and lengths.
- General identifiers and string literals are tokenized, although the stage-0
  parser does not consume them yet.
- Decimal, octal, and hexadecimal integer constants with `u`/`l` suffixes.
- Character constants and common single-character escapes.

Macros and `#include` are not processed yet. String literals and arbitrary
identifiers currently produce a parse error when used in the stage-0 grammar.

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
- Runtime arithmetic (including software multiply/divide/remainder), shifts,
  bitwise operations, comparisons, logical operators, conditional expressions,
  and comma expressions.

Local scopes are not separated yet. Deep expressions that exhaust scratch
registers and calls with stack-passed arguments are rejected rather than
spilled. Arrays, pointer operations, structures, enums, typedefs, globals, and
multiple translation units remain unsupported.

### Pipeline and generated code

- The lexer and recursive-descent parser build an arena-backed expression AST.
- Semantic lowering retains the AST for unoptimized runtime code generation;
  a whole-program constant-return case is still evaluated during bootstrap.
- `dyn_optimize` is intentionally a no-op.
- The Dynphony backend emits direct, unoptimized ALU and branch instructions.
  Constant-return programs remain 27 bytes; dynamic image size depends on the
  source program.
- Dynamic images initialize `sp`, call `main` through the normal ABI, and emit
  frame prologues/epilogues and call/return-address fixups.
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
