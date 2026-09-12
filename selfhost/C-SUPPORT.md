# C support in the self-hosted compiler

This document describes C accepted **by the compiler in `selfhost/`**, not the
larger subset accepted by the Python compiler that currently builds it. Every
new self-hosting language feature should be added here with an execution test.

## Current stage: stage 0

Stage 0 accepts exactly one translation unit containing one `main` definition:

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
- The keywords `int`, `void`, `main`, and `return`.
- Decimal, octal, and hexadecimal integer constants without suffixes.
- Punctuation `(`, `)`, `{`, `}`, and `;`.
- Operators `+`, `-`, `*`, `/`, `%`, `<<`, `>>`, `&`, `^`, `|`, and `~`.

Arbitrary identifiers, character and string literals, integer suffixes, macros,
and `#include` are not accepted yet.

### Grammar and semantics

- Exactly one return statement in `main`.
- Parenthesized expressions.
- Unary `+`, unary `-`, and bitwise complement `~`.
- Binary arithmetic, shifts, and bitwise operations with C precedence and
  left associativity.
- Values are evaluated as wrapping 32-bit unsigned integers.
- Division or remainder by zero is rejected.
- Shift counts greater than or equal to 32 are rejected.

There are currently no variables, assignments, local declarations, arrays,
pointers, control-flow statements, user-defined functions, function calls,
structures, enums, typedefs, or multiple translation units.

### Pipeline and generated code

- The lexer and recursive-descent parser build an arena-backed expression AST.
- Semantic lowering evaluates the expression into a one-value IR module.
- `dyn_optimize` is currently a no-op; the constant result comes from semantic
  evaluation rather than a general optimization pass.
- The Dynphony backend emits a fixed 16-byte program that materializes the
  32-bit result in `r1` and jumps forever at byte offset 12.
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

The current executable `main` reads one source buffer from the Dynphony input
device and emits bytes through the output device. This temporary byte protocol
will be replaced by the project protocol in [PROJECT-FORMAT.md](PROJECT-FORMAT.md).

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
