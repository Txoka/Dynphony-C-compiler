# Dynphony C roadmap

The immediate objective is a correct, deterministic compiler written in the C
subset it compiles. Optimization follows self-hosting; it must not obscure the
bootstrap correctness work.

## Phase 0 — executable bootstrap

- [x] Establish a separate `selfhost/` tree with frontend, middle-end, target,
  and driver boundaries similar to the Python compiler.
- [x] Build the C compiler with the Python compiler and execute it in the native
  or reference Dynphony emulator.
- [x] Parse constant-return `main` programs, use the heap for source/AST storage,
  emit a runnable Dynphony image, and run that child image.
- [x] Document the exact current subset in
  [`selfhost/C-SUPPORT.md`](selfhost/C-SUPPORT.md).

## Phase 1 — persistent project transport and executable interface

- [x] Specify DCP1, a word-oriented virtual-filesystem bundle containing named
  translation units, headers, include roots, and preprocessor definitions.
- [x] Specify DCC1/DCO1 control, status, binary, and diagnostic records in
  persistent storage.
- [ ] Add `selfhost/tools/pack_project.py` to deterministically pack a directory
  and optional manifest into a persistent image containing DCC1 and DCP1.
- [ ] Add Python DCC1/DCP1/DCO1 codecs and malformed-image tests.
- [ ] Add emulator CLI options to load and save a persistent image.
- [ ] Replace stage 0's temporary `input()`/`output()` byte transport: executable
  `main` must read DCC1/DCP1 with `persistent_load` and write DCO1 plus final
  status with `persistent_store`.
- [ ] Introduce `dyn_compile_project(...)` as the device-independent core API;
  keep `_start -> main` as the standalone boot path.

The complete storage layout and boot choices are in
[`selfhost/PROJECT-FORMAT.md`](selfhost/PROJECT-FORMAT.md). A cold machine starts
at `_start`, not `main`, because runtime initialization must happen first.

## Phase 2 — enough C to express the compiler

Each increment must update `selfhost/C-SUPPORT.md` and add positive execution
tests plus malformed-source diagnostics.

- [ ] General identifiers, character/string literals, token locations, and
  recoverable diagnostics.
- [ ] Declarations, scalar types, qualifiers, typedef names, lexical scopes,
  initializers, lvalues, assignments, casts, and `sizeof`.
- [ ] Expression completeness: comparisons, logical operators, conditionals,
  comma expressions, increment/decrement, and short-circuit evaluation.
- [ ] Statements and control flow: blocks, `if`, loops, `break`, `continue`, and
  all return paths.
- [ ] Functions, prototypes, direct/indirect calls, recursion, and the Dynphony
  scalar calling convention.
- [ ] Pointers, fixed arrays, VLAs, strings, structures, enums, and static/global
  storage needed by the compiler sources.
- [ ] Runtime declarations through standard headers and target facilities through
  `<dynphony.h>`.
- [ ] A real typed IR and correctness-first Dynphony instruction selection,
  frames, calls, branches, static data, relocations, and image layout.

## Phase 3 — preprocessing and whole-project linking

- [ ] Resolve quoted includes relative to each DCP1 file and angle includes from
  DCP1 include roots; add guards and `#pragma once` behavior.
- [ ] Implement object/function macros, conditionals, `#define`, `#undef`, and
  `#error` to the level used by `selfhost/`.
- [ ] Compile every DCP1 translation unit in a private file scope.
- [ ] Resolve external symbols, preserve internal `static` linkage, diagnose
  conflicts/duplicates/undefined references, and lay out one flat image.
- [ ] Keep builds independent of host paths, timestamps, hash ordering, and
  input record ordering.

This phase lets the compiler compile and link the entire `selfhost/` folder even
though the Dynphony computer has no filesystem.

## Phase 4 — prove self-hosting

- [ ] Python compiler builds C stage 1.
- [ ] Stage 1 reads the packed `selfhost/` project from persistent storage and
  emits stage 2 there.
- [ ] Stage 2 consumes the same project and emits stage 3.
- [ ] Stage 2 and stage 3 are byte-identical.
- [ ] Run a representative conformance/demo suite with the self-hosted binary.
- [ ] Record image size, persistent/RAM/heap/stack requirements, compile
  instruction count, and emulator throughput.

## Phase 5 — optimize the self-hosted compiler

Begin this phase only after the unoptimized bootstrap is correct and
reproducible. Required legalization and layout are compiler correctness, not
optional optimization, and therefore belong in earlier phases.

- [ ] Establish IR dumps, generated-byte tests, semantic differential tests
  against the Python compiler, and size/speed benchmarks.
- [ ] Constant/copy propagation and constant folding.
- [ ] Dead instruction, unreachable block, dead function, and dead static-data
  elimination.
- [ ] Basic-block CFG simplification and branch threading/fallthrough selection.
- [ ] Immediate selection, constant materialization, algebraic identities, and
  power-of-two strength reduction.
- [ ] Stack-slot promotion, liveness, register allocation, and call-aware spills.
- [ ] Direct calls, safe inlining, tail calls, whole-program reachability, and
  interprocedural constant propagation.
- [ ] Branch relaxation and final image-size optimization.
- [ ] Compare stage 2/stage 3 again after each optimization family to protect
  deterministic self-hosting.

## Phase 6 — native in-machine development loop

- [ ] Define a stable exported compiler ABI or resident monitor ABI.
- [ ] Let a Dynphony editor or monitor construct DCC1/DCP1 directly in persistent
  memory without any host filesystem.
- [ ] Add a PIC/callable output mode that can be copied from persistent storage
  into non-overlapping program RAM.
- [ ] Define stack ownership and whether generated programs return to the monitor
  or boot and halt as standalone images.
- [ ] Optionally add an editor/shell and persistent project manager.

The first practical hardware workflow is simpler: preload persistent storage,
boot the compiler, let it write its result back to persistent storage, then copy
or reload that result into RAM and boot it.
